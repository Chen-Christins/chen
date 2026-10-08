#include "application.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <set>

#include "config/config.h"
#include "daemon.h"
#include "game/game_server.h"
#include "http/http_server.h"
#include "http/ws_server.h"
#include "rpc/rpc_server.h"
#include "iomanager/worker.h"
#include "module/module.h"
#include "util/env.h"
#include "version.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<std::string>::ptr g_server_work_path =
    Config::Lookup("server.work_path", std::string(), "server work path");
static ConfigVar<std::string>::ptr g_server_pid_file =
    Config::Lookup("server.pid_file", std::string("chen.pid"), "server pid file");
static ConfigVar<std::vector<TcpServerConf>>::ptr g_servers_conf =
    Config::Lookup("servers", std::vector<TcpServerConf>(), "server conf");
static ConfigVar<uint64_t>::ptr g_drain_timeout_ms =
    Config::Lookup("server.drain_timeout_ms", (uint64_t)3000, "graceful shutdown drain timeout ms");
static ConfigVar<uint64_t>::ptr g_housekeeping_interval_ms =
    Config::Lookup("server.housekeeping_interval_ms", (uint64_t)500, "housekeeping timer interval ms");

static void RegisterBuiltInServers() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        TcpServerFactory::Register(
            "http", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server = std::make_shared<http::HttpServer>(conf.keepalive != 0,
                                                                  process_worker, io_worker, accept_worker);
                if (conf.negotiateH2) {
                    server->setNegotiateH2(true);
                }
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register(
            "http2", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server = std::make_shared<http::HttpServer>(conf.keepalive != 0,
                                                                  process_worker, io_worker, accept_worker);
                server->setType("http2");
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register(
            "ws", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                return TcpServer::ptr(new http::WSServer(process_worker, io_worker, accept_worker));
            });
        TcpServerFactory::Register(
            "rpc", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server = std::make_shared<rpc::RpcServer>(process_worker, io_worker, accept_worker);
                if (auto it = conf.args.find("relay"); it != conf.args.end() && it->second == "1") {
                    server->setRelay(true);
                }
                if (auto it = conf.args.find("relay_timeout"); it != conf.args.end()) {
                    server->setRelayTimeout(strtoul(it->second.c_str(), nullptr, 0));
                }
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register(
            "game", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                uint32_t magic = 0xC0DECAFE;
                size_t header_size = 16;
                if (auto it = conf.args.find("magic"); it != conf.args.end()) {
                    magic = static_cast<uint32_t>(strtoul(it->second.c_str(), nullptr, 0));
                }
                if (auto it = conf.args.find("header_size"); it != conf.args.end()) {
                    header_size = static_cast<size_t>(strtoul(it->second.c_str(), nullptr, 0));
                }
                auto parser = game::CreateLengthFieldParser(header_size, magic);
                return TcpServer::ptr(new game::GenericProtocolServer(parser, process_worker, io_worker, accept_worker));
            });
    });
}

Application* Application::m_instance = nullptr;
std::atomic<bool> Application::s_shutdownSignaled{false};
std::atomic<bool> Application::s_reloadSignaled{false};

Application::Application() {
    m_instance = this;
}

bool Application::init(int argc, char** argv) {
    m_argc = argc;
    m_argv = argv;

    EnvMgr::GetInstance()->addHelp("s", "start in terminal, or -s reload|stop|quit to signal a running instance");
    EnvMgr::GetInstance()->addHelp("d", "run as daemon");
    EnvMgr::GetInstance()->addHelp("c", "conf path default: ./conf");
    EnvMgr::GetInstance()->addHelp("v", "print version, e.g. -v or --version");
    EnvMgr::GetInstance()->addHelp("p", "print help");

    bool is_print_help = false;
    if (!EnvMgr::GetInstance()->init(argc, argv)) {
        is_print_help = true;
    }

    if (EnvMgr::GetInstance()->has("v") || EnvMgr::GetInstance()->has("version")) {
        std::cout << "chen/" << CHEN_VERSION << std::endl;
        return false;
    }

    if (EnvMgr::GetInstance()->has("p")) {
        is_print_help = true;
    }

    if (is_print_help) {
        EnvMgr::GetInstance()->printHelp();
        return false;
    }

    std::string conf_path = EnvMgr::GetInstance()->getConfigPath();
    Config::LoadFromConfDir(conf_path);
    INFO(logger) << "load conf path:" << conf_path;

    // 信号命令: -s reload / -s stop / -s quit（类似 nginx -s）
    if (EnvMgr::GetInstance()->has("s")) {
        std::string sig_name = EnvMgr::GetInstance()->get("s");
        if (!sig_name.empty()) {
            sendSignal(sig_name);
            return false;
        }
    }

    ModuleMgr::GetInstance()->init();
    std::vector<Module::ptr> modules;
    ModuleMgr::GetInstance()->listAll(modules);

    for (auto i : modules) {
        i->onBeforeArgsParse(argc, argv);
    }

    for (auto i : modules) {
        i->onAfterArgsParse(argc, argv);
    }
    modules.clear();

    int run_type = 0;
    if (EnvMgr::GetInstance()->has("s")) {
        run_type = 1;
    }
    if (EnvMgr::GetInstance()->has("d")) {
        run_type = 2;
    }

    if (run_type == 0) {
        EnvMgr::GetInstance()->printHelp();
        return false;
    }

    if (g_server_work_path->getValue().empty()) {
        FATAL(logger) << "server work path is empty";
        return false;
    }

    std::string pid_file = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();
    if (FSUtil::IsRunningPidfile(pid_file)) {
        ERROR(logger) << "server is running: " << pid_file;
        return false;
    }
    if (!FSUtil::Mkdir(g_server_work_path->getValue())) {
        FATAL(logger) << "create work path [" << g_server_work_path->getValue() << " errno=" << errno
            << " errstr=" << std::strerror(errno);
        return false;
    }
    return true;
}

bool Application::run() {
    bool is_daemon = EnvMgr::GetInstance()->has("d");
    return start_daemon(m_argc, m_argv,
        std::bind(&Application::main, this, std::placeholders::_1, std::placeholders::_2), is_daemon);
}

void Application::signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        s_shutdownSignaled.store(true, std::memory_order_release);
    } else if (sig == SIGHUP) {
        s_reloadSignaled.store(true, std::memory_order_release);
    }
}

int Application::sendSignal(const std::string& sig_name) {
    std::string pidfile = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();

    std::string content = FSUtil::ReadFileToString(pidfile);
    if (content.empty()) {
        ERROR(logger) << "failed to read pid file: " << pidfile;
        return -1;
    }
    pid_t pid = atoi(content.c_str());
    if (pid <= 1) {
        ERROR(logger) << "invalid pid in pid file: " << content;
        return -1;
    }

    if (kill(pid, 0) != 0) {
        ERROR(logger) << "process " << pid << " is not running, cleaning up stale pid file";
        FSUtil::Unlink(pidfile, true);
        return -1;
    }

    int sig;
    if (sig_name == "reload") {
        sig = SIGHUP;
    } else if (sig_name == "stop" || sig_name == "quit") {
        sig = SIGTERM;
    } else {
        ERROR(logger) << "unknown signal: " << sig_name << " (expected: reload, stop, quit)";
        return -1;
    }

    if (kill(pid, sig) != 0) {
        ERROR(logger) << "failed to send signal to pid " << pid
            << ": " << strerror(errno);
        return -1;
    }

    INFO(logger) << "sent " << sig_name << " signal to process " << pid;
    return 0;
}

void Application::registerSignals() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &Application::signalHandler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    // 忽略 SIGPIPE，防止写入已关闭的 socket 导致进程退出
    signal(SIGPIPE, SIG_IGN);
    INFO(logger) << "registered signal handlers (SIGINT/SIGTERM: shutdown, SIGHUP: reload)";
}

void Application::onHousekeeping() {
    // 检查优雅关闭信号
    if (s_shutdownSignaled.load(std::memory_order_acquire)) {
        if (!m_isShuttingDown) {
            m_isShuttingDown = true;
            INFO(logger) << "received shutdown signal, starting graceful shutdown...";
            doGracefulShutdown();
        }
        return;
    }

    // 检查热重载信号
    if (s_reloadSignaled.load(std::memory_order_acquire)) {
        s_reloadSignaled.store(false, std::memory_order_release);
        INFO(logger) << "received reload signal (SIGHUP), starting hot reload...";
        doHotReload();
    }
}

void Application::startConfigWatcher() {
    m_configWatcher.reset(new FileWatcher());
    std::string conf_path = EnvMgr::GetInstance()->getAbsolutePath(EnvMgr::GetInstance()->getConfigPath());

    m_configWatcher->watchDir(conf_path, [this](const std::string& file, uint32_t events) {
        INFO(logger) << "inotify: config file event file=" << file
            << " mask=0x" << std::hex << events;
        doConfigReload(file);
    });
}

void Application::doConfigReload(const std::string& file) {
    INFO(logger) << "config reload: reloading file=" << file;
    Config::LoadFromFile(file);
}

void Application::doGracefulShutdown() {
    INFO(logger) << "graceful shutdown: stopping all servers...";
    for (auto& [type, servers] : m_servers) {
        for (auto& server : servers) {
            INFO(logger) << "graceful shutdown: stopping server type=" << type
                << " name=" << server->getName();
            server->stop();
        }
    }

    INFO(logger) << "graceful shutdown: closing all client connections...";
    for (auto& [type, servers] : m_servers) {
        for (auto& server : servers) {
            server->closeAllClients();
        }
    }

    uint64_t drain_ms = g_drain_timeout_ms->getValue();
    INFO(logger) << "graceful shutdown: waiting " << drain_ms << "ms for in-flight fibers to unwind...";
    auto self = std::weak_ptr<IOManager>(m_mainIOManager);
    m_mainIOManager->addTimer(drain_ms, [this, self]() {
        if (self.expired()) {
            return;
        }

        INFO(logger) << "graceful shutdown: cancelling tick timers...";
        for (auto& timer : m_tickTimers) {
            if (timer) {
                timer->cancel();
            }
        }
        m_tickTimers.clear();

        if (m_housekeepingTimer) {
            m_housekeepingTimer->cancel();
            m_housekeepingTimer.reset();
        }

        INFO(logger) << "graceful shutdown: stopping tick IOManager...";
        m_tickIOManager->stop();

        INFO(logger) << "graceful shutdown: unloading modules...";
        {
            std::vector<Module::ptr> modules;
            ModuleMgr::GetInstance()->listAll(modules);
            for (auto& m : modules) {
                m->onDeactivate();
            }
            for (auto& m : modules) {
                m->onUnload();
            }
        }

        INFO(logger) << "graceful shutdown: stopping WorkerMgr...";
        WorkerMgr::GetInstance()->stop();

        if (!m_pidfile.empty()) {
            FSUtil::Unlink(m_pidfile, true);
            INFO(logger) << "graceful shutdown: removed pid file " << m_pidfile;
        }

        if (m_configWatcher) {
            INFO(logger) << "graceful shutdown: stopping config watcher...";
            m_configWatcher->stop();
        }

        INFO(logger) << "graceful shutdown complete, server exiting normally";

        std::exit(0);
    });
}

void Application::doHotReload() {
    auto module_path = EnvMgr::GetInstance()->getAbsolutePath(GetModulePath());

    std::vector<std::string> files;
    FSUtil::ListAllFile(files, module_path, ".so");

    std::vector<Module::ptr> pending_drain;
    std::vector<Module::ptr> newly_loaded;
    for (auto& file : files) {
        INFO(logger) << "hot reload: attempting to reload module " << file;
        Module::ptr old_mod;
        Module::ptr new_mod = ModuleMgr::GetInstance()->reloadModule(file, &old_mod);
        if (new_mod) {
            INFO(logger) << "hot reload: module loaded successfully: " << file;
            newly_loaded.push_back(std::move(new_mod));
        } else {
            ERROR(logger) << "hot reload: failed to reload module: " << file;
        }
        if (old_mod) {
            pending_drain.push_back(std::move(old_mod));
        }
    }

    // 仅当有模块被重载时，执行双缓冲切换
    if (!newly_loaded.empty()) {
        // 收集新加载模块的 filename，用于只对新模块调 onServerUp
        std::set<std::string> new_filenames;
        for (auto& m : newly_loaded) {
            new_filenames.insert(m->getFilename());
        }

        INFO(logger) << "hot reload: preparing dispatch for " << newly_loaded.size() << " reloaded module(s)...";
        for (auto& [type, servers] : m_servers) {
            for (auto& server : servers) {
                server->prepareDispatch();
            }
        }

        {
            std::vector<Module::ptr> modules;
            ModuleMgr::GetInstance()->listAll(modules);
            for (auto& m : modules) {
                m->onServerReady();
            }
        }

        INFO(logger) << "hot reload: committing dispatch...";
        for (auto& [type, servers] : m_servers) {
            for (auto& server : servers) {
                server->commitDispatch();
            }
        }

        // 只对新加载的模块调 onServerUp（不变模块不需要重复通知）
        {
            std::vector<Module::ptr> modules;
            ModuleMgr::GetInstance()->listAll(modules);
            for (auto& m : modules) {
                if (new_filenames.count(m->getFilename())) {
                    m->onServerUp();
                }
            }
        }

        for (auto& timer : m_tickTimers) {
            if (timer) {
                timer->cancel();
            }
        }
        m_tickTimers.clear();

        std::vector<Module::ptr> modules;
        ModuleMgr::GetInstance()->listAll(modules);
        for (auto& m : modules) {
            uint64_t interval_ms = m->getTickIntervalMs();
            if (interval_ms > 0) {
                std::weak_ptr<Module> weak_m = m;
                auto timer = m_tickIOManager->addTimer(interval_ms, [weak_m]() {
                    auto mod = weak_m.lock();
                    if (mod) {
                        mod->onTick();
                    }
                }, true);
                m_tickTimers.push_back(timer);
            }
        }
    }

    for (auto& old_mod : pending_drain) {
        auto old_name = old_mod->getName();
        auto old_version = old_mod->getVersion();
        INFO(logger) << "hot reload: deactivating old module name=" << old_name
            << " version=" << old_version;
        old_mod->onDeactivate();
    }
    pending_drain.clear();

    INFO(logger) << "hot reload complete";
}

int Application::main(int argc, char** argv) {
    DEBUG(logger) << "main";
    std::string conf_path = EnvMgr::GetInstance()->getConfigPath();
    Config::LoadFromConfDir(conf_path);
    {
        m_pidfile = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();
        std::ofstream ofs(m_pidfile);
        if (!ofs) {
            ERROR(logger) << "open pidfile " << m_pidfile << " failed";
            return false;
        }
        ofs << getpid();
    }

    // 注册信号处理器
    registerSignals();

    // 启动协程
    m_mainIOManager.reset(new IOManager(1, true, "main"));
    m_tickIOManager.reset(new IOManager(1, false, "tick"));
    m_mainIOManager->schedule(std::bind(&Application::run_fiber, this));

    // housekeeping 定时器：仅处理信号检测
    uint64_t housekeeping_ms = g_housekeeping_interval_ms->getValue();
    m_housekeepingTimer = m_mainIOManager->addTimer(housekeeping_ms, [this]() {
        onHousekeeping();
    }, true);

    // inotify 监听配置文件变更
    startConfigWatcher();

    m_mainIOManager->stop();
    m_tickIOManager->stop();

    // 显式释放 IOManager，确保 ~Scheduler/~IOManager 和 fiber 析构
    m_mainIOManager.reset();
    m_tickIOManager.reset();

    return 0;
}

int Application::run_fiber() {
    std::vector<Module::ptr> modules;
    ModuleMgr::GetInstance()->listAll(modules);
    bool has_error = false;
    for (auto& i : modules) {
        if (!i->onLoad()) {
            ERROR(logger) << "module name=" << i->getName() << " version=" << i->getVersion() << " filename=" << i->getFilename();
            has_error = true;
        }
    }
    if (has_error) {
        _exit(0);
    }

    WorkerMgr::GetInstance()->init();

    RegisterBuiltInServers();

    auto conf = g_servers_conf->getValue();
    std::vector<TcpServer::ptr> svrs;
    for (auto& i : conf) {
        DEBUG(logger) << std::endl << LexicalCast<TcpServerConf, std::string>()(i);

        std::vector<Address::ptr> address;
        for (auto& a : i.address) {
            size_t pos = a.find(":");
            if (pos == std::string::npos) {
                // ERROR(logger) << "invalid address: " << a;
                address.push_back(UnixAddress::ptr(std::make_shared<UnixAddress>(a)));
                continue;
            }
            int32_t port = atoi(a.substr(pos + 1).c_str());
            auto addr = IPAddress::Create(a.substr(0, pos).c_str(), port);
            if (addr) {
                address.push_back(addr);
                continue;
            }
            std::vector<std::pair<Address::ptr, uint32_t>> result;
            if (Address::GetInterfaceAddresses(result, a.substr(0, pos))) {
                for (auto& x : result) {
                    auto ipaddr = std::dynamic_pointer_cast<IPAddress>(x.first);
                    if (ipaddr) {
                        ipaddr->setPort(atoi(a.substr(pos + 1).c_str()));
                    }
                    address.push_back(ipaddr);
                }
                continue;
            }

            if (auto aaddr = Address::LookupAny(a)) {
                address.push_back(aaddr);
                continue;
            }
            ERROR(logger) << "invalid address: " << a;
            _exit(0);
        }

        IOManager* accept_worker = IOManager::GetThis();
        IOManager* io_worker = IOManager::GetThis();
        IOManager* process_worker = IOManager::GetThis();

        if (!i.accept_worker.empty()) {
            accept_worker = WorkerMgr::GetInstance()->getAsIOManager(i.accept_worker).get();
            if (!accept_worker) {
                ERROR(logger) << "accept_worker: " << i.accept_worker << " not exists";
                _exit(0);
            }
        }
        if (!i.io_worker.empty()) {
            io_worker = WorkerMgr::GetInstance()->getAsIOManager(i.io_worker).get();
            if (!io_worker) {
                ERROR(logger) << "io_worker: " << i.io_worker << " not exists";
                _exit(0);
            }
        }
        if (!i.process_worker.empty()) {
            process_worker = WorkerMgr::GetInstance()->getAsIOManager(i.process_worker).get();
            if (!process_worker) {
                ERROR(logger) << "process_worker: " << i.process_worker << " not exists";
                _exit(0);
            }
        }

        TcpServer::ptr server = TcpServerFactory::Create(i, process_worker, io_worker, accept_worker);
        if (!server) {
            ERROR(logger) << "invalid server type=" << i.type << LexicalCast<TcpServerConf, std::string>()(i);
            _exit(0);
        }
        if (!i.name.empty()) {
            server->setName(i.name);
            EnvMgr::GetInstance()->setEnv("server", i.name);
        }
        if (std::vector<Address::ptr> fails; !server->bind(address, fails, i.ssl)) {
            for (auto& x : fails) {
                ERROR(logger) << "bind address fail:" << *x;
            }
            _exit(0);
        }
        if (i.ssl) {
            if (!server->loadCertificates(i.cert_file, i.key_file)) {
                ERROR(logger) << "loadCertificates fail, cert_file=" << i.cert_file << " key_file=" << i.key_file;
            }
        }
        server->setConf(i);
        m_servers[i.type].push_back(server);
        svrs.push_back(server);
    }
    for (auto& i : modules) {
        i->onServerReady();
    }
    for (auto& i : svrs) {
        i->start();
    }
    for (auto& i : modules) {
        i->onServerUp();
    }
    // 为每个声明了 tick 间隔的模块注册定时器
    for (auto& i : modules) {
        uint64_t interval_ms = i->getTickIntervalMs();
        if (interval_ms > 0) {
            // 使用 weak_ptr，避免定时器阻止模块热重载时释放
            std::weak_ptr<Module> weak_i = i;
            auto timer = m_tickIOManager->addTimer(interval_ms, [weak_i]() {
                auto mod = weak_i.lock();
                if (mod) {
                    mod->onTick();
                }
            }, true);
            m_tickTimers.push_back(timer);
        }
    }
    return 0;
}

bool Application::getServer(const std::string& name, std::vector<TcpServer::ptr>& svrs) {
    auto it = m_servers.find(name);
    if (it == m_servers.end()) {
        return false;
    }
    svrs = it->second;
    return true;
}

void Application::getAllServers(std::vector<TcpServer::ptr>& svrs) {
    for (auto& [type, servers] : m_servers) {
        svrs.insert(svrs.end(), servers.begin(), servers.end());
    }
}

} // namespace chen
