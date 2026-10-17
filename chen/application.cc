#include "application.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sys/wait.h>

#include "config/config.h"
#include "daemon.h"
#include "game/game_server.h"
#include "http/http_server.h"
#include "http/ws_server.h"
#include "iomanager/worker.h"
#include "module/module.h"
#include "rpc/rpc_server.h"
#include "util/env.h"
#include "version.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<std::string>::ptr g_server_work_path = Config::Lookup("server.work_path", std::string(), "server work path");
static ConfigVar<std::string>::ptr g_server_pid_file = Config::Lookup("server.pid_file", std::string("chen.pid"), "server pid file");
static ConfigVar<std::vector<TcpServerConf>>::ptr g_servers_conf = Config::Lookup("servers", std::vector<TcpServerConf>(), "server conf");
static ConfigVar<uint64_t>::ptr g_drain_timeout_ms = Config::Lookup("server.drain_timeout_ms", (uint64_t)3000, "graceful shutdown drain timeout ms");
static ConfigVar<uint64_t>::ptr g_housekeeping_interval_ms =
    Config::Lookup("server.housekeeping_interval_ms", (uint64_t)500, "housekeeping timer interval ms");
static ConfigVar<uint64_t>::ptr g_worker_ready_timeout_ms =
    Config::Lookup("server.worker_ready_timeout_ms", (uint64_t)30000, "master waits worker ready timeout ms");
static ConfigVar<uint32_t>::ptr g_worker_restart_interval_ms =
    Config::Lookup("server.worker_restart_interval_ms", (uint32_t)2000, "master restarts crashed worker interval ms");

static void RegisterBuiltInServers()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        TcpServerFactory::Register("http", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
            auto server = std::make_shared<http::HttpServer>(conf.keepalive != 0, process_worker, io_worker, accept_worker);
            if (conf.negotiateH2) {
                server->setNegotiateH2(true);
            }
            return TcpServer::ptr(server);
        });
        TcpServerFactory::Register("http2", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
            auto server = std::make_shared<http::HttpServer>(conf.keepalive != 0, process_worker, io_worker, accept_worker);
            server->setType("http2");
            return TcpServer::ptr(server);
        });
        TcpServerFactory::Register("ws", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
            return TcpServer::ptr(new http::WSServer(process_worker, io_worker, accept_worker));
        });
        TcpServerFactory::Register("rpc", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
            auto server = std::make_shared<rpc::RpcServer>(process_worker, io_worker, accept_worker);
            if (auto it = conf.args.find("relay"); it != conf.args.end() && it->second == "1") {
                server->setRelay(true);
            }
            if (auto it = conf.args.find("relay_timeout"); it != conf.args.end()) {
                server->setRelayTimeout(strtoul(it->second.c_str(), nullptr, 0));
            }
            return TcpServer::ptr(server);
        });
        TcpServerFactory::Register("game", [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
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

Application::Application()
{
    m_instance = this;
}

bool Application::init(int argc, char** argv)
{
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
    if (!FSUtil::Exists(conf_path)) {
        FATAL(logger) << "conf_path is invalid errno=" << errno << ", errstr=" << strerror(errno);
        return false;
    }

    Config::LoadFromConfDir(conf_path);

    // 信号命令: -s reload / -s stop / -s quit（类似 nginx -s）
    if (EnvMgr::GetInstance()->has("s")) {
        std::string sig_name = EnvMgr::GetInstance()->get("s");
        if (!sig_name.empty()) {
            sendSignal(sig_name);
            return false;
        }
    }

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
        FATAL(logger) << "create work path [" << g_server_work_path->getValue() << " errno=" << errno << " errstr=" << std::strerror(errno);
        return false;
    }
    return true;
}

bool Application::run()
{
    bool is_daemon = EnvMgr::GetInstance()->has("d");
    return start_daemon(
               m_argc, m_argv,
               [this](int, char**) {
                   return masterMain();
               },
               is_daemon)
        == 0;
}

void Application::signalHandler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT) {
        s_shutdownSignaled.store(true, std::memory_order_release);
    }
}

int Application::sendSignal(const std::string& sig_name)
{
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
        ERROR(logger) << "failed to send signal to pid " << pid << ": " << strerror(errno);
        return -1;
    }

    INFO(logger) << "sent " << sig_name << " signal to process " << pid;
    return 0;
}

void Application::registerSignals()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &Application::signalHandler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);

    // worker 忽略 SIGHUP（重载由 master fork 新 worker 处理）
    signal(SIGHUP, SIG_IGN);
    // 忽略 SIGPIPE，防止写入已关闭的 socket 导致进程退出
    signal(SIGPIPE, SIG_IGN);

    INFO(logger) << "registered signal handlers (SIGINT/SIGTERM/SIGQUIT: shutdown)";
}

void Application::onHousekeeping()
{
    // 检查优雅关闭信号
    if (s_shutdownSignaled.load(std::memory_order_acquire)) {
        if (!m_isShuttingDown) {
            m_isShuttingDown = true;
            INFO(logger) << "received shutdown signal, starting graceful shutdown...";
            doGracefulShutdown();
        }
    }
}

void Application::startConfigWatcher()
{
    m_configWatcher.reset(new FileWatcher());
    std::string conf_path = EnvMgr::GetInstance()->getAbsolutePath(EnvMgr::GetInstance()->getConfigPath());

    m_configWatcher->watchDir(conf_path, [this](const std::string& file, uint32_t events) {
        INFO(logger) << "inotify: config file event file=" << file << " mask=0x" << std::hex << events;
        doConfigReload(file);
    });
}

void Application::doConfigReload(const std::string& file)
{
    INFO(logger) << "config reload: reloading file=" << file;
    Config::LoadFromFile(file);
}

void Application::doGracefulShutdown()
{
    INFO(logger) << "graceful shutdown: stopping all servers...";
    for (auto& [type, servers] : m_servers) {
        for (auto& server : servers) {
            INFO(logger) << "graceful shutdown: stopping server type=" << type << " name=" << server->getName();
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

        // worker 不清理 pid 文件（由 master 负责）
        if (!m_isWorker && !m_pidfile.empty()) {
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

bool Application::setupServers()
{
    RegisterBuiltInServers();

    auto conf = g_servers_conf->getValue();
    for (auto& i : conf) {
        DEBUG(logger) << std::endl << LexicalCast<TcpServerConf, std::string>()(i);

        std::vector<Address::ptr> address;
        for (auto& a : i.address) {
            size_t pos = a.find(":");
            if (pos == std::string::npos) {
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
            return false;
        }

        // master 以空 worker 构造，fork 后由 worker 通过 setWorkers() 注入真实线程池
        TcpServer::ptr server = TcpServerFactory::Create(i, nullptr, nullptr, nullptr);
        if (!server) {
            ERROR(logger) << "invalid server type=" << i.type << LexicalCast<TcpServerConf, std::string>()(i);
            return false;
        }
        if (!i.name.empty()) {
            server->setName(i.name);
            EnvMgr::GetInstance()->setEnv("server", i.name);
        }
        if (std::vector<Address::ptr> fails; !server->bind(address, fails, i.ssl)) {
            for (auto& x : fails) {
                ERROR(logger) << "bind address fail:" << *x;
            }
            return false;
        }
        if (i.ssl) {
            if (!server->loadCertificates(i.cert_file, i.key_file)) {
                ERROR(logger) << "loadCertificates fail, cert_file=" << i.cert_file << " key_file=" << i.key_file;
                return false;
            }
        }
        server->setConf(i);
        m_servers[i.type].push_back(server);
    }
    return true;
}

int Application::spawnWorker()
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        ERROR(logger) << "master: pipe failed errno=" << errno << " errstr=" << strerror(errno);
        return -1;
    }

    // fork 前刷新所有日志缓冲，避免子进程继承未刷新内容导致 pre-fork 日志重复
    LoggerMgr::GetInstance()->flushAll();

    pid_t pid = fork();
    if (pid < 0) {
        ERROR(logger) << "master: fork failed errno=" << errno << " errstr=" << strerror(errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // 子进程 = worker：关闭读端，进入 worker 主流程（不返回）
        close(pipefd[0]);
        workerMain(pipefd[1]);
        _exit(0);
    }

    // 父进程 = master：关闭写端，记录 worker
    close(pipefd[1]);

    WorkerProc w;
    w.pid = pid;
    w.ready_fd = pipefd[0];
    w.draining = false;
    m_workers.push_back(w);
    int idx = static_cast<int>(m_workers.size()) - 1;

    INFO(logger) << "master: spawned worker pid=" << pid;

    return idx;
}

bool Application::waitWorkerReady(WorkerProc& w, uint64_t timeout_ms)
{
    if (w.ready_fd < 0) {
        return false;
    }

    struct pollfd pfd;
    pfd.fd = w.ready_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int r = poll(&pfd, 1, static_cast<int>(timeout_ms));
    if (r <= 0) {
        if (r == 0) {
            ERROR(logger) << "master: worker pid=" << w.pid << " ready timeout";
        } else {
            ERROR(logger) << "master: poll worker pid=" << w.pid << " failed errno=" << errno;
        }
        close(w.ready_fd);
        w.ready_fd = -1;
        return false;
    }

    char c = 0;
    ssize_t n = read(w.ready_fd, &c, 1);
    close(w.ready_fd);
    w.ready_fd = -1;
    return n == 1 && c == '1';
}

void Application::sleepInterruptible(uint32_t ms)
{
    uint64_t remain_ns = static_cast<uint64_t>(ms) * 1000000ULL;
    while (remain_ns > 0 && !m_isMasterStopping) {
        struct timespec ts;
        ts.tv_sec = static_cast<time_t>(remain_ns / 1000000000ULL);
        ts.tv_nsec = static_cast<long>(remain_ns % 1000000000ULL);

        int sig = sigtimedwait(&m_masterSigset, nullptr, &ts);
        if (sig < 0) {
            if (errno == EAGAIN) {
                break; // 超时
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (sig == SIGTERM || sig == SIGINT) {
            INFO(logger) << "master: received shutdown signal during worker restart backoff";
            m_isMasterStopping = true;
            return;
        }
        if (sig == SIGHUP) {
            m_pendingReload = true;
        } else if (sig == SIGCHLD) {
            m_pendingReap = true;
        }
    }
}

void Application::reapWorkers()
{
    int status = 0;
    pid_t pid = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        bool draining = false;
        for (auto it = m_workers.begin(); it != m_workers.end(); ++it) {
            if (it->pid == pid) {
                draining = it->draining;
                if (it->ready_fd >= 0) {
                    close(it->ready_fd);
                }
                m_workers.erase(it);
                break;
            }
        }

        if (WIFEXITED(status)) {
            INFO(logger) << "master: worker pid=" << pid << " exited code=" << WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            WARN(logger) << "master: worker pid=" << pid << " killed signal=" << WTERMSIG(status);
        }

        if (m_isMasterStopping || draining) {
            continue;
        }

        // 非预期退出（崩溃）→ 重启，维持服务
        ERROR(logger) << "master: worker pid=" << pid << " crashed unexpectedly, restarting";
        ProcessInfoMgr::GetInstance()->restart_count += 1;
        uint32_t interval = g_worker_restart_interval_ms->getValue();
        if (interval > 0) {
            sleepInterruptible(interval);
        }
        if (m_isMasterStopping) {
            return;
        }
        int idx = spawnWorker();
        if (idx < 0) {
            ERROR(logger) << "master: failed to respawn worker";
            continue;
        }
        if (!waitWorkerReady(m_workers[idx], g_worker_ready_timeout_ms->getValue())) {
            ERROR(logger) << "master: respawned worker pid=" << m_workers[idx].pid << " not ready";
        }
    }
}

void Application::handleReload()
{
    INFO(logger) << "master: received SIGHUP, reloading modules...";

    int idx = spawnWorker();
    if (idx < 0) {
        ERROR(logger) << "master: reload failed to spawn worker, keeping current workers";
        return;
    }

    WorkerProc& new_worker = m_workers[idx];
    if (!waitWorkerReady(new_worker, g_worker_ready_timeout_ms->getValue())) {
        ERROR(logger) << "master: new worker pid=" << new_worker.pid << " failed to become ready, killing it and keeping old workers";
        kill(new_worker.pid, SIGKILL);
        // 标记 draining，避免 reapWorkers 把它当崩溃重启
        new_worker.draining = true;
        if (new_worker.ready_fd >= 0) {
            close(new_worker.ready_fd);
            new_worker.ready_fd = -1;
        }
        return;
    }

    INFO(logger) << "master: new worker pid=" << new_worker.pid << " ready, draining old workers";
    for (auto& w : m_workers) {
        if (w.pid == new_worker.pid || w.draining) {
            continue;
        }
        INFO(logger) << "master: sending SIGQUIT to old worker pid=" << w.pid;
        if (kill(w.pid, SIGQUIT) != 0) {
            ERROR(logger) << "master: kill SIGQUIT pid=" << w.pid << " failed errno=" << errno << " errstr=" << strerror(errno);
        }
        w.draining = true;
    }
}

void Application::shutdownWorkers()
{
    m_isMasterStopping = true;
    for (auto& w : m_workers) {
        if (w.pid > 0) {
            INFO(logger) << "master: sending SIGTERM to worker pid=" << w.pid;
            kill(w.pid, SIGTERM);
            w.draining = true;
        }
    }

    while (!m_workers.empty()) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            ERROR(logger) << "master: waitpid failed errno=" << errno << " errstr=" << strerror(errno);
            break;
        }
        for (auto it = m_workers.begin(); it != m_workers.end(); ++it) {
            if (it->pid == pid) {
                if (it->ready_fd >= 0) {
                    close(it->ready_fd);
                }
                m_workers.erase(it);
                break;
            }
        }
        INFO(logger) << "master: worker pid=" << pid << " stopped";
    }
}

int Application::masterMain()
{
    if (!setupServers()) {
        ERROR(logger) << "master: setup servers failed";
        return 1;
    }

    m_pidfile = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();
    {
        std::ofstream ofs(m_pidfile);
        if (!ofs) {
            ERROR(logger) << "master: open pidfile " << m_pidfile << " failed";
            return 1;
        }
        ofs << getpid();
    }

    INFO(logger) << "master started pid=" << getpid() << " pidfile=" << m_pidfile;

    signal(SIGPIPE, SIG_IGN);

    // 屏蔽信号，用 sigwaitinfo/sigtimedwait 同步处理（master 单线程，避免异步 handler）
    sigemptyset(&m_masterSigset);
    sigaddset(&m_masterSigset, SIGHUP);
    sigaddset(&m_masterSigset, SIGTERM);
    sigaddset(&m_masterSigset, SIGINT);
    sigaddset(&m_masterSigset, SIGCHLD);
    sigprocmask(SIG_BLOCK, &m_masterSigset, nullptr);

    // 启动首个 worker 并等待就绪
    int idx = spawnWorker();
    if (idx < 0 || !waitWorkerReady(m_workers[idx], g_worker_ready_timeout_ms->getValue())) {
        ERROR(logger) << "master: initial worker failed to start";
        shutdownWorkers();
        if (!m_pidfile.empty()) {
            FSUtil::Unlink(m_pidfile, true);
        }
        return 1;
    }

    while (!m_isMasterStopping) {
        // 先处理 backoff 期间被消费的信号
        if (m_pendingReload) {
            m_pendingReload = false;
            handleReload();
            continue;
        }
        if (m_pendingReap) {
            m_pendingReap = false;
            reapWorkers();
            continue;
        }

        int sig = sigwaitinfo(&m_masterSigset, nullptr);
        if (sig < 0) {
            if (errno == EINTR) {
                continue;
            }
            ERROR(logger) << "master: sigwaitinfo failed errno=" << errno << " errstr=" << strerror(errno);
            break;
        }
        if (sig == SIGHUP) {
            handleReload();
        } else if (sig == SIGTERM || sig == SIGINT) {
            INFO(logger) << "master: received shutdown signal, stopping workers...";
            m_isMasterStopping = true;
            break;
        } else if (sig == SIGCHLD) {
            reapWorkers();
        }
    }

    shutdownWorkers();

    if (!m_pidfile.empty()) {
        FSUtil::Unlink(m_pidfile, true);
        INFO(logger) << "master: removed pid file " << m_pidfile;
    }

    INFO(logger) << "master: exited";

    return 0;
}

void Application::workerMain(int ready_fd)
{
    // 先安装处理器，再解除 master 继承的信号屏蔽
    registerSignals();
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, nullptr);

    m_isWorker = true;
    m_readyFd = ready_fd;

    m_mainIOManager.reset(new IOManager(1, true, "main"));
    m_tickIOManager.reset(new IOManager(1, false, "tick"));
    m_mainIOManager->schedule(std::bind(&Application::run_fiber, this));

    // housekeeping 定时器：仅处理信号检测
    uint64_t housekeeping_ms = g_housekeeping_interval_ms->getValue();
    m_housekeepingTimer = m_mainIOManager->addTimer(
        housekeeping_ms,
        [this]() {
            onHousekeeping();
        },
        true);

    // inotify 监听配置文件变更
    startConfigWatcher();

    m_mainIOManager->stop();
    m_tickIOManager->stop();

    // 显式释放 IOManager，确保 ~Scheduler/~IOManager 和 fiber 析构
    m_mainIOManager.reset();
    m_tickIOManager.reset();
    _exit(0);
}

void Application::reportWorkerReady()
{
    if (m_readyFd < 0) {
        return;
    }

    char c = '1';
    ssize_t n = write(m_readyFd, &c, 1);
    if (n != 1) {
        WARN(logger) << "worker: failed to notify master ready, errno=" << errno;
    }

    close(m_readyFd);
    m_readyFd = -1;
}

void Application::reportWorkerFailed()
{
    if (m_readyFd < 0) {
        return;
    }

    char c = '0';
    ssize_t n = write(m_readyFd, &c, 1);
    if (n != 1) {
        WARN(logger) << "worker: failed to notify master failure, errno=" << errno;
    }

    close(m_readyFd);
    m_readyFd = -1;
}

int Application::run_fiber()
{
    // worker 从磁盘加载模块（含最新版本）
    ModuleMgr::GetInstance()->init();

    std::vector<Module::ptr> modules;
    ModuleMgr::GetInstance()->listAll(modules);

    for (auto& i : modules) {
        i->onBeforeArgsParse(m_argc, m_argv);
    }
    for (auto& i : modules) {
        i->onAfterArgsParse(m_argc, m_argv);
    }

    bool has_error = false;
    for (auto& i : modules) {
        if (!i->onLoad()) {
            ERROR(logger) << "module name=" << i->getName() << " version=" << i->getVersion() << " filename=" << i->getFilename();
            has_error = true;
        }
    }
    if (has_error) {
        reportWorkerFailed();
        _exit(1);
    }

    WorkerMgr::GetInstance()->init();

    // server 已由 master 创建并绑定监听 socket，fork 后由此 worker 继承；
    // 按配置解析出真实 worker 并注入，之后才能 start()
    for (auto& [type, servers] : m_servers) {
        for (auto& server : servers) {
            auto conf = server->getConf();
            if (!conf) {
                ERROR(logger) << "server has no conf type=" << type;
                reportWorkerFailed();
                _exit(1);
            }

            IOManager* accept_worker = IOManager::GetThis();
            IOManager* io_worker = IOManager::GetThis();
            IOManager* process_worker = IOManager::GetThis();

            if (!conf->accept_worker.empty()) {
                accept_worker = WorkerMgr::GetInstance()->getAsIOManager(conf->accept_worker).get();
                if (!accept_worker) {
                    ERROR(logger) << "accept_worker: " << conf->accept_worker << " not exists";
                    reportWorkerFailed();
                    _exit(1);
                }
            }
            if (!conf->io_worker.empty()) {
                io_worker = WorkerMgr::GetInstance()->getAsIOManager(conf->io_worker).get();
                if (!io_worker) {
                    ERROR(logger) << "io_worker: " << conf->io_worker << " not exists";
                    reportWorkerFailed();
                    _exit(1);
                }
            }
            if (!conf->process_worker.empty()) {
                process_worker = WorkerMgr::GetInstance()->getAsIOManager(conf->process_worker).get();
                if (!process_worker) {
                    ERROR(logger) << "process_worker: " << conf->process_worker << " not exists";
                    reportWorkerFailed();
                    _exit(1);
                }
            }

            server->setWorkers(process_worker, io_worker, accept_worker);
        }
    }

    for (auto& i : modules) {
        i->onServerReady();
    }

    for (auto& [type, servers] : m_servers) {
        for (auto& server : servers) {
            server->start();
        }
    }

    for (auto& i : modules) {
        i->onServerUp();
    }

    // 为每个声明了 tick 间隔的模块注册定时器
    for (auto& i : modules) {
        uint64_t interval_ms = i->getTickIntervalMs();
        if (interval_ms > 0) {
            // 使用 weak_ptr，避免定时器阻止模块释放
            std::weak_ptr<Module> weak_i = i;
            auto timer = m_tickIOManager->addTimer(
                interval_ms,
                [weak_i]() {
                    auto mod = weak_i.lock();
                    if (mod) {
                        mod->onTick();
                    }
                },
                true);
            m_tickTimers.push_back(timer);
        }
    }

    INFO(logger) << "worker ready: " << m_servers.size() << " server type(s), " << modules.size() << " module(s)";

    reportWorkerReady();

    return 0;
}

bool Application::getServer(const std::string& name, std::vector<TcpServer::ptr>& svrs)
{
    auto it = m_servers.find(name);
    if (it == m_servers.end()) {
        return false;
    }
    svrs = it->second;
    return true;
}

void Application::getAllServers(std::vector<TcpServer::ptr>& svrs)
{
    for (auto& [type, servers] : m_servers) {
        svrs.insert(svrs.end(), servers.begin(), servers.end());
    }
}

} // namespace chen
