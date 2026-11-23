#include "Application.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sys/wait.h>
#include <thread>

#include "Config/Config.hpp"
#include "Daemon.hpp"
#include "Game/GameServer.hpp"
#include "Http/HttpServer.hpp"
#include "Http/WSServer.hpp"
#include "IOManager/Worker.hpp"
#include "Module/Module.hpp"
#include "Rock/RockServer.hpp"
#include "Rpc/RpcServer.hpp"
#include "Streams/StaticServiceDiscovery.hpp"
#include "Util/Env.hpp"
#include "Version.hpp"

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

static ConfigVar<uint64_t>::ptr g_worker_ready_timeout_ms =
    Config::Lookup("server.worker_ready_timeout_ms", (uint64_t)30000, "master waits worker ready timeout ms");

static ConfigVar<uint32_t>::ptr g_worker_restart_interval_ms =
    Config::Lookup("server.worker_restart_interval_ms", (uint32_t)2000, "master restarts crashed worker interval ms");

static ConfigVar<uint64_t>::ptr g_worker_shutdown_timeout_ms = Config::Lookup("server.worker_shutdown_timeout_ms",
    (uint64_t)10000,
    "master waits worker exit before SIGKILL ms (should exceed drain + shutdown grace)");

static ConfigVar<uint64_t>::ptr g_shutdown_grace_timeout_ms = Config::Lookup("server.shutdown_grace_timeout_ms",
    (uint64_t)3000,
    "worker force-exit if graceful shutdown exceeds drain + this grace period ms");

static ConfigVar<std::string>::ptr g_server_process_name = Config::Lookup("server.process_name",
    std::string(),
    "process title base name, default is the executable file name");

static void RegisterBuiltInServers()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        TcpServerFactory::Register("http",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server =
                    std::make_shared<http::HttpServer>(conf.keepalive != 0, process_worker, io_worker, accept_worker);
                if (conf.negotiate_h2) {
                    server->setNegotiateH2(true);
                }
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register("http2",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server =
                    std::make_shared<http::HttpServer>(conf.keepalive != 0, process_worker, io_worker, accept_worker);
                server->setType("http2");
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register("ws",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                return TcpServer::ptr(new http::WSServer(process_worker, io_worker, accept_worker));
            });
        TcpServerFactory::Register("rpc",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server = std::make_shared<rpc::RpcServer>(process_worker, io_worker, accept_worker);
                if (auto it = conf.args.find("relay"); it != conf.args.end() && it->second == "1") {
                    server->setRelay(true);
                }
                if (auto it = conf.args.find("relay_timeout"); it != conf.args.end()) {
                    server->setRelayTimeout(strtoul(it->second.c_str(), nullptr, 0));
                }
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register("game",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                uint32_t magic = 0xC0DECAFE;
                size_t header_size = 16;
                if (auto it = conf.args.find("magic"); it != conf.args.end()) {
                    magic = static_cast<uint32_t>(strtoul(it->second.c_str(), nullptr, 0));
                }
                if (auto it = conf.args.find("header_size"); it != conf.args.end()) {
                    header_size = strtoul(it->second.c_str(), nullptr, 0);
                }
                auto parser = game::CreateLengthFieldParser(header_size, magic);
                return TcpServer::ptr(
                    new game::GenericProtocolServer(parser, process_worker, io_worker, accept_worker));
            });
        TcpServerFactory::Register("rock",
            [](const TcpServerConf&, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                return TcpServer::ptr(new RockServer("rock", process_worker, io_worker, accept_worker));
            });
    });
}

Application* Application::kInstance = nullptr;
std::atomic<bool> Application::kShutdownSignaled{false};

Application::Application()
{
    kInstance = this;
}

bool Application::init(int argc, char** argv)
{
    argc_ = argc;
    argv_ = argv;

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

    EnvMgr::GetInstance()->setProcTitleBase(g_server_process_name->getValue());

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
        FATAL(logger) << "create work path [" << g_server_work_path->getValue() << " errno=" << errno
                      << " errstr=" << std::strerror(errno);
        return false;
    }
    return true;
}

bool Application::run()
{
    bool is_daemon = EnvMgr::GetInstance()->has("d");

    bool ret = start_daemon(
        argc_, argv_,
        [this](int, char**) {
            return masterMain();
        },
        is_daemon);

    return ret == 0;
}

void Application::signalHandler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT) {
        kShutdownSignaled.store(true, std::memory_order_release);
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
    if (kShutdownSignaled.load(std::memory_order_acquire)) {
        if (!is_shutting_down_) {
            is_shutting_down_ = true;
            INFO(logger) << "received shutdown signal, starting graceful shutdown...";
            doGracefulShutdown();
        }
    }
}

void Application::startConfigWatcher()
{
    config_watcher_.reset(new FileWatcher());
    std::string conf_path = EnvMgr::GetInstance()->getAbsolutePath(EnvMgr::GetInstance()->getConfigPath());

    config_watcher_->watchDir(conf_path, [this](const std::string& file, uint32_t events) {
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
    EnvMgr::GetInstance()->setProcTitleRole(is_worker_ ? "worker process (draining)" : "master process (draining)");

    INFO(logger) << "graceful shutdown: stopping all servers...";
    for (auto& [type, servers] : servers_) {
        for (auto& server : servers) {
            INFO(logger) << "graceful shutdown: stopping server type=" << type << " name=" << server->getName();
            server->stop();
        }
    }

    INFO(logger) << "graceful shutdown: closing all client connections...";
    for (auto& [type, servers] : servers_) {
        for (auto& server : servers) {
            server->closeAllClients();
        }
    }

    uint64_t drain_ms = g_drain_timeout_ms->getValue();
    INFO(logger) << "graceful shutdown: waiting " << drain_ms << "ms for in-flight fibers to unwind...";

    // 硬看门狗：优雅退出可能卡在模块卸载 / Scheduler::stop() / 锁等待上，
    // 这里用独立线程兜底，超过 drain + grace 后无条件退出，避免 worker 永远不退出。
    const uint64_t grace_ms = g_shutdown_grace_timeout_ms->getValue();
    if (grace_ms > 0) {
        try {
            std::thread([drain_ms, grace_ms]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(drain_ms + grace_ms));
                ERROR(logger) << "graceful shutdown exceeded " << (drain_ms + grace_ms)
                              << "ms, forcing exit (possible deadlock in unload/scheduler stop)";
                std::_Exit(0);
            }).detach();
        } catch (const std::exception& e) {
            WARN(logger) << "graceful shutdown: failed to start watchdog thread: " << e.what();
        }
    }

    auto self = std::weak_ptr<IOManager>(main_iomanager_);
    main_iomanager_->addTimer(drain_ms, [this, self]() {
        if (self.expired()) {
            return;
        }

        INFO(logger) << "graceful shutdown: cancelling tick timers...";
        for (auto& timer : tick_timers_) {
            if (timer) {
                timer->cancel();
            }
        }
        tick_timers_.clear();

        if (housekeeping_timer_) {
            housekeeping_timer_->cancel();
            housekeeping_timer_.reset();
        }

        INFO(logger) << "graceful shutdown: stopping tick IOManager...";
        tick_iomanager_->stop();

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

        if (auto sd = getServiceDiscovery()) {
            INFO(logger) << "graceful shutdown: stopping service discovery...";
            sd->stop();
        }

        INFO(logger) << "graceful shutdown: stopping WorkerMgr...";
        WorkerMgr::GetInstance()->stop();

        // worker 不清理 pid 文件（由 master 负责）
        if (!is_worker_ && !pidfile_.empty()) {
            FSUtil::Unlink(pidfile_, true);
            INFO(logger) << "graceful shutdown: removed pid file " << pidfile_;
        }

        if (config_watcher_) {
            INFO(logger) << "graceful shutdown: stopping config watcher...";
            config_watcher_->stop();
        }

        INFO(logger) << "graceful shutdown complete, server exiting normally";

        // 直接 _Exit，跳过静态析构：在协程/退出路径里跑全局析构容易再次死锁
        std::_Exit(0);
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
        servers_[i.type].push_back(server);
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
    workers_.push_back(w);
    int idx = static_cast<int>(workers_.size()) - 1;

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
    while (remain_ns > 0 && !is_master_stopping_) {
        struct timespec ts;
        ts.tv_sec = static_cast<time_t>(remain_ns / 1000000000ULL);
        ts.tv_nsec = static_cast<long>(remain_ns % 1000000000ULL);

        int sig = sigtimedwait(&master_sigset_, nullptr, &ts);
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
            is_master_stopping_ = true;
            return;
        }
        if (sig == SIGHUP) {
            pending_reload_ = true;
        } else if (sig == SIGCHLD) {
            pending_reap_ = true;
        }
    }
}

void Application::reapWorkers()
{
    int status = 0;
    pid_t pid = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        bool draining = false;
        for (auto it = workers_.begin(); it != workers_.end(); ++it) {
            if (it->pid == pid) {
                draining = it->draining;
                if (it->ready_fd >= 0) {
                    close(it->ready_fd);
                }
                workers_.erase(it);
                break;
            }
        }

        if (WIFEXITED(status)) {
            INFO(logger) << "master: worker pid=" << pid << " exited code=" << WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            WARN(logger) << "master: worker pid=" << pid << " killed signal=" << WTERMSIG(status);
        }

        if (is_master_stopping_ || draining) {
            continue;
        }

        // 非预期退出（崩溃）→ 重启，维持服务
        ERROR(logger) << "master: worker pid=" << pid << " crashed unexpectedly, restarting";
        ProcessInfoMgr::GetInstance()->restart_count += 1;
        uint32_t interval = g_worker_restart_interval_ms->getValue();
        if (interval > 0) {
            sleepInterruptible(interval);
        }
        if (is_master_stopping_) {
            return;
        }
        int idx = spawnWorker();
        if (idx < 0) {
            ERROR(logger) << "master: failed to respawn worker";
            continue;
        }
        if (!waitWorkerReady(workers_[idx], g_worker_ready_timeout_ms->getValue())) {
            ERROR(logger) << "master: respawned worker pid=" << workers_[idx].pid << " not ready";
        }
    }
}

void Application::handleReload()
{
    EnvMgr::GetInstance()->setProcTitleRole("master process (reloading)");
    // 无论成功失败，返回时恢复 master 角色
    struct TitleRestore {
        ~TitleRestore() { EnvMgr::GetInstance()->setProcTitleRole("master process"); }
    } title_restore;

    INFO(logger) << "master: received SIGHUP, reloading modules...";

    int idx = spawnWorker();
    if (idx < 0) {
        ERROR(logger) << "master: reload failed to spawn worker, keeping current workers";
        return;
    }

    WorkerProc& new_worker = workers_[idx];
    if (!waitWorkerReady(new_worker, g_worker_ready_timeout_ms->getValue())) {
        ERROR(logger) << "master: new worker pid=" << new_worker.pid
                      << " failed to become ready, killing it and keeping old workers";
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
    for (auto& w : workers_) {
        if (w.pid == new_worker.pid || w.draining) {
            continue;
        }
        INFO(logger) << "master: sending SIGQUIT to old worker pid=" << w.pid;
        if (kill(w.pid, SIGQUIT) != 0) {
            ERROR(logger) << "master: kill SIGQUIT pid=" << w.pid << " failed errno=" << errno
                          << " errstr=" << strerror(errno);
        }
        w.draining = true;
    }
}

void Application::shutdownWorkers()
{
    is_master_stopping_ = true;
    for (auto& w : workers_) {
        if (w.pid > 0) {
            INFO(logger) << "master: sending SIGTERM to worker pid=" << w.pid;
            kill(w.pid, SIGTERM);
            w.draining = true;
        }
    }

    const uint64_t timeout_ms = g_worker_shutdown_timeout_ms->getValue();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    bool timed_out = false;
    while (!workers_.empty()) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            for (auto it = workers_.begin(); it != workers_.end(); ++it) {
                if (it->pid == pid) {
                    if (it->ready_fd >= 0) {
                        close(it->ready_fd);
                    }
                    workers_.erase(it);
                    break;
                }
            }
            INFO(logger) << "master: worker pid=" << pid << " stopped";
            continue;
        }
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            ERROR(logger) << "master: waitpid failed errno=" << errno << " errstr=" << strerror(errno);
            break;
        }
        // pid == 0：子进程还没退出
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            break;
        }
        poll(nullptr, 0, 50);
    }

    if (!workers_.empty()) {
        if (timed_out) {
            WARN(logger) << "master: workers not exited within " << timeout_ms << "ms, sending SIGKILL";
        } else {
            WARN(logger) << "master: reaping remaining workers with SIGKILL";
        }
        for (auto& w : workers_) {
            if (w.pid > 0) {
                kill(w.pid, SIGKILL);
            }
        }
        // SIGKILL 后必须收尸，否则会留下僵尸进程
        while (!workers_.empty()) {
            int status = 0;
            pid_t pid = waitpid(-1, &status, 0);
            if (pid > 0) {
                for (auto it = workers_.begin(); it != workers_.end(); ++it) {
                    if (it->pid == pid) {
                        if (it->ready_fd >= 0) {
                            close(it->ready_fd);
                        }
                        workers_.erase(it);
                        break;
                    }
                }
                WARN(logger) << "master: worker pid=" << pid << " killed";
                continue;
            }
            if (pid < 0 && errno == EINTR) {
                continue;
            }
            ERROR(logger) << "master: waitpid after SIGKILL failed errno=" << errno << " errstr=" << strerror(errno);
            break;
        }
    }

    workers_.clear();
}

int Application::masterMain()
{
    EnvMgr::GetInstance()->setProcTitleRole("master process");

    if (!setupServers()) {
        ERROR(logger) << "master: setup servers failed";
        return 1;
    }

    pidfile_ = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();
    {
        std::ofstream ofs(pidfile_);
        if (!ofs) {
            ERROR(logger) << "master: open pidfile " << pidfile_ << " failed";
            return 1;
        }
        ofs << getpid();
    }

    INFO(logger) << "master started pid=" << getpid() << " pidfile=" << pidfile_;

    signal(SIGPIPE, SIG_IGN);

    sigemptyset(&master_sigset_);
    sigaddset(&master_sigset_, SIGHUP);
    sigaddset(&master_sigset_, SIGTERM);
    sigaddset(&master_sigset_, SIGINT);
    sigaddset(&master_sigset_, SIGCHLD);
    sigprocmask(SIG_BLOCK, &master_sigset_, nullptr);

    int idx = spawnWorker();
    if (idx < 0 || !waitWorkerReady(workers_[idx], g_worker_ready_timeout_ms->getValue())) {
        ERROR(logger) << "master: initial worker failed to start";
        shutdownWorkers();
        if (!pidfile_.empty()) {
            FSUtil::Unlink(pidfile_, true);
        }
        return 1;
    }

    while (!is_master_stopping_) {
        // 先处理 backoff 期间被消费的信号
        if (pending_reload_) {
            pending_reload_ = false;
            handleReload();
            continue;
        }
        if (pending_reap_) {
            pending_reap_ = false;
            reapWorkers();
            continue;
        }

        int sig = sigwaitinfo(&master_sigset_, nullptr);
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
            is_master_stopping_ = true;
            break;
        } else if (sig == SIGCHLD) {
            reapWorkers();
        }
    }

    shutdownWorkers();

    if (!pidfile_.empty()) {
        FSUtil::Unlink(pidfile_, true);
        INFO(logger) << "master: removed pid file " << pidfile_;
    }

    INFO(logger) << "master: exited";

    return 0;
}

void Application::workerMain(int ready_fd)
{
    EnvMgr::GetInstance()->setProcTitleRole("worker process");

    registerSignals();
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, nullptr);

    is_worker_ = true;
    ready_fd_ = ready_fd;

    main_iomanager_.reset(new IOManager(1, true, "main"));
    tick_iomanager_.reset(new IOManager(1, false, "tick"));
    main_iomanager_->schedule(std::bind(&Application::run_fiber, this));

    // housekeeping 定时器：仅处理信号检测
    uint64_t housekeeping_ms = g_housekeeping_interval_ms->getValue();
    housekeeping_timer_ = main_iomanager_->addTimer(
        housekeeping_ms,
        [this]() {
            onHousekeeping();
        },
        true);

    // inotify 监听配置文件变更
    startConfigWatcher();

    main_iomanager_->stop();
    tick_iomanager_->stop();

    main_iomanager_.reset();
    tick_iomanager_.reset();
    _exit(0);
}

void Application::reportWorkerReady()
{
    if (ready_fd_ < 0) {
        return;
    }

    char c = '1';
    ssize_t n = write(ready_fd_, &c, 1);
    if (n != 1) {
        WARN(logger) << "worker: failed to notify master ready, errno=" << errno;
    }

    close(ready_fd_);
    ready_fd_ = -1;
}

void Application::reportWorkerFailed()
{
    if (ready_fd_ < 0) {
        return;
    }

    char c = '0';
    ssize_t n = write(ready_fd_, &c, 1);
    if (n != 1) {
        WARN(logger) << "worker: failed to notify master failure, errno=" << errno;
    }

    close(ready_fd_);
    ready_fd_ = -1;
}

int Application::run_fiber()
{
    // 配置了静态服务发现时自动创建，模块可在 onLoad/onServerReady 中 registerService
    if (!getServiceDiscovery()) {
        if (auto sd = StaticServiceDiscovery::CreateFromConfig()) {
            setServiceDiscovery(sd);
            INFO(logger) << "created StaticServiceDiscovery from config";
        }
    }

    // worker 从磁盘加载模块（含最新版本）
    ModuleMgr::GetInstance()->init();

    std::vector<Module::ptr> modules;
    ModuleMgr::GetInstance()->listAll(modules);

    for (auto& i : modules) {
        i->onBeforeArgsParse(argc_, argv_);
    }
    for (auto& i : modules) {
        i->onAfterArgsParse(argc_, argv_);
    }

    bool has_error = false;
    for (auto& i : modules) {
        if (!i->onLoad()) {
            ERROR(logger) << "module name=" << i->getName() << " version=" << i->getVersion()
                          << " filename=" << i->getFilename();
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
    for (auto& [type, servers] : servers_) {
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

    // 模块注册完成后再发布服务实例，确保 registerService 补录的地址可见
    if (auto sd = getServiceDiscovery()) {
        sd->start();
    }

    for (auto& [type, servers] : servers_) {
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
            auto timer = tick_iomanager_->addTimer(
                interval_ms,
                [weak_i]() {
                    auto mod = weak_i.lock();
                    if (mod) {
                        mod->onTick();
                    }
                },
                true);
            tick_timers_.push_back(timer);
        }
    }

    INFO(logger) << "worker ready: " << servers_.size() << " server type(s), " << modules.size() << " module(s)";

    reportWorkerReady();

    return 0;
}

bool Application::getServer(const std::string& name, std::vector<TcpServer::ptr>& svrs)
{
    auto it = servers_.find(name);
    if (it == servers_.end()) {
        return false;
    }
    svrs = it->second;
    return true;
}

void Application::getAllServers(std::vector<TcpServer::ptr>& svrs)
{
    for (auto& [type, servers] : servers_) {
        svrs.insert(svrs.end(), servers.begin(), servers.end());
    }
}

IServiceDiscovery::ptr Application::getServiceDiscovery() const
{
    std::lock_guard lock(service_discovery_mutex_);
    return service_discovery_;
}

void Application::setServiceDiscovery(IServiceDiscovery::ptr sd)
{
    std::lock_guard lock(service_discovery_mutex_);
    service_discovery_ = std::move(sd);
}

} // namespace chen
