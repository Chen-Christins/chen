/**
 * @file WorkerRuntime.cc
 * @brief worker 进程运行时实现
 * @author Christins
 * @date 2026-02-06
 * @copyright GPL-3.0
 */
#include "WorkerRuntime.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <signal.h>
#include <thread>
#include <unistd.h>

#include "Application.hpp"
#include "Config/Config.hpp"
#include "IOManager/Worker.hpp"
#include "Module/Module.hpp"
#include "Streams/StaticServiceDiscovery.hpp"
#include "Util/Env.hpp"
#include "Util/TimeUtil.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<uint64_t>::ptr g_drain_timeout_ms =
    Config::Lookup("server.drain_timeout_ms", (uint64_t)3000, "graceful shutdown drain timeout ms");

static ConfigVar<uint64_t>::ptr g_housekeeping_interval_ms =
    Config::Lookup("server.housekeeping_interval_ms", (uint64_t)500, "housekeeping timer interval ms");

static ConfigVar<uint64_t>::ptr g_shutdown_grace_timeout_ms = Config::Lookup("server.shutdown_grace_timeout_ms",
    (uint64_t)3000,
    "worker force-exit if graceful shutdown exceeds drain + this grace period ms");

std::atomic<bool> WorkerRuntime::kShutdownSignaled{false};

WorkerRuntime::WorkerRuntime(ServerRegistry::ptr registry)
    : registry_(std::move(registry))
{
}

void WorkerRuntime::setArgs(int argc, char** argv)
{
    argc_ = argc;
    argv_ = argv;
}

void WorkerRuntime::signalHandler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT) {
        kShutdownSignaled.store(true, std::memory_order_release);
    }
}

void WorkerRuntime::registerSignals()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &WorkerRuntime::signalHandler;
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

void WorkerRuntime::run(int ready_fd)
{
    EnvMgr::GetInstance()->setProcTitleRole("worker process");

    registerSignals();
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, nullptr);

    ready_fd_ = ready_fd;

    // use_caller=true：main IOManager 就运行在当前（worker 主）线程上，
    // 模块 onTick 的定时器也挂在它上面，不再单独起 tick 线程
    main_iomanager_.reset(new IOManager(1, true, "main"));
    main_iomanager_->schedule(std::bind(&WorkerRuntime::runFiber, this));

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

    main_iomanager_.reset();
    _exit(0);
}

void WorkerRuntime::onHousekeeping()
{
    // 检查优雅关闭信号：只置标志，真正的关闭在协程里做
    if (kShutdownSignaled.load(std::memory_order_acquire)) {
        if (!is_shutting_down_) {
            is_shutting_down_ = true;
            INFO(logger) << "received shutdown signal, starting graceful shutdown...";
            doGracefulShutdown();
        }
    }
}

void WorkerRuntime::startConfigWatcher()
{
    config_watcher_.reset(new FileWatcher());
    std::string conf_path = EnvMgr::GetInstance()->getAbsolutePath(EnvMgr::GetInstance()->getConfigPath());

    config_watcher_->watchDir(conf_path, [this](const std::string& file, uint32_t events) {
        INFO(logger) << "inotify: config file event file=" << file << " mask=0x" << std::hex << events;
        doConfigReload(file);
    });
}

void WorkerRuntime::doConfigReload(const std::string& file)
{
    INFO(logger) << "config reload: reloading file=" << file;
    Config::LoadFromFile(file);
}

void WorkerRuntime::doGracefulShutdown()
{
    EnvMgr::GetInstance()->setProcTitleRole("worker process (draining)");

    // 1. 停止 accept；支持协作式 drain 的协议保留存量连接等待在途请求
    INFO(logger) << "graceful shutdown: begin draining servers...";
    registry_->forEach([](const std::string& type, const TcpServer::ptr& server) {
        INFO(logger) << "graceful shutdown: drain server type=" << type << " name=" << server->getName()
                     << " graceful=" << server->supportsGracefulDrain();
        server->beginDrain();
        if (!server->supportsGracefulDrain()) {
            // 长连接协议（WS/Rock/RPC/Game）没有请求边界，无法判断在途请求，保持立即关闭
            server->closeAllClients();
        }
    });

    uint64_t drain_ms = g_drain_timeout_ms->getValue();
    INFO(logger) << "graceful shutdown: waiting up to " << drain_ms << "ms for in-flight requests...";

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

    // 2. 轮询等待在途请求结束：关闭空闲连接 -> 检查在途数 -> 未到 deadline 就 sleep 20ms。
    //    这里 sleep 的是 IOManager 的协程（hook 过的 usleep），不会阻塞事件循环。
    const uint64_t deadline_ms = TimeUtil::GetCurrentMs() + drain_ms;
    size_t remaining = 0;
    while (true) {
        remaining = 0;
        registry_->forEach([&remaining](const std::string&, const TcpServer::ptr& server) {
            server->closeIdleClients();
            remaining += server->getInFlightClientCount();
        });

        if (remaining == 0) {
            break;
        }
        if (TimeUtil::GetCurrentMs() >= deadline_ms) {
            WARN(logger) << "graceful shutdown: drain timeout after " << drain_ms << "ms, forcing close of "
                         << remaining << " connection(s) with in-flight requests";
            registry_->forEach([](const std::string&, const TcpServer::ptr& server) {
                server->closeAllClients();
            });
            break;
        }
        usleep(20 * 1000);
    }
    INFO(logger) << "graceful shutdown: drain finished, in_flight=" << remaining;

    finishShutdown();
}

void WorkerRuntime::finishShutdown()
{
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

    Application* app = Application::GetInstance();
    if (app) {
        if (auto sd = app->getServiceDiscovery()) {
            INFO(logger) << "graceful shutdown: stopping service discovery...";
            sd->stop();
        }
    }

    INFO(logger) << "graceful shutdown: stopping WorkerMgr...";
    WorkerMgr::GetInstance()->stop();

    // worker 不清理 pid 文件（由 master 负责）

    if (config_watcher_) {
        INFO(logger) << "graceful shutdown: stopping config watcher...";
        config_watcher_->stop();
    }

    INFO(logger) << "graceful shutdown complete, server exiting normally";

    // 直接 _Exit，跳过静态析构：在协程/退出路径里跑全局析构容易再次死锁
    std::_Exit(0);
}

void WorkerRuntime::reportWorkerReady()
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

void WorkerRuntime::reportWorkerFailed()
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

int WorkerRuntime::runFiber()
{
    Application* app = Application::GetInstance();

    // 配置了静态服务发现时自动创建，模块可在 onLoad/onServerReady 中 registerService
    if (app && !app->getServiceDiscovery()) {
        if (auto sd = StaticServiceDiscovery::CreateFromConfig()) {
            app->setServiceDiscovery(sd);
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
    bool worker_inject_failed = false;
    registry_->forEach([&worker_inject_failed](const std::string& type, const TcpServer::ptr& server) {
        auto conf = server->getConf();
        if (!conf) {
            ERROR(logger) << "server has no conf type=" << type;
            worker_inject_failed = true;
            return;
        }

        IOManager* accept_worker = IOManager::GetThis();
        IOManager* io_worker = IOManager::GetThis();
        IOManager* process_worker = IOManager::GetThis();

        if (!conf->accept_worker.empty()) {
            accept_worker = WorkerMgr::GetInstance()->getAsIOManager(conf->accept_worker).get();
            if (!accept_worker) {
                ERROR(logger) << "accept_worker: " << conf->accept_worker << " not exists";
                worker_inject_failed = true;
                return;
            }
        }
        if (!conf->io_worker.empty()) {
            io_worker = WorkerMgr::GetInstance()->getAsIOManager(conf->io_worker).get();
            if (!io_worker) {
                ERROR(logger) << "io_worker: " << conf->io_worker << " not exists";
                worker_inject_failed = true;
                return;
            }
        }
        if (!conf->process_worker.empty()) {
            process_worker = WorkerMgr::GetInstance()->getAsIOManager(conf->process_worker).get();
            if (!process_worker) {
                ERROR(logger) << "process_worker: " << conf->process_worker << " not exists";
                worker_inject_failed = true;
                return;
            }
        }

        server->setWorkers(process_worker, io_worker, accept_worker);
    });
    if (worker_inject_failed) {
        reportWorkerFailed();
        _exit(1);
    }

    for (auto& i : modules) {
        i->onServerReady();
    }

    // 模块注册完成后再发布服务实例，确保 registerService 补录的地址可见
    if (app) {
        if (auto sd = app->getServiceDiscovery()) {
            sd->start();
        }
    }

    registry_->forEach([](const std::string&, const TcpServer::ptr& server) {
        server->start();
    });

    for (auto& i : modules) {
        i->onServerUp();
    }

    // 为每个声明了 tick 间隔的模块注册定时器
    for (auto& i : modules) {
        uint64_t interval_ms = i->getTickIntervalMs();
        if (interval_ms > 0) {
            // 使用 weak_ptr，避免定时器阻止模块释放
            std::weak_ptr<Module> weak_i = i;
            auto timer = main_iomanager_->addTimer(
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

    INFO(logger) << "worker ready: " << registry_->typeCount() << " server type(s), " << modules.size() << " module(s)";

    reportWorkerReady();

    return 0;
}

} // namespace chen
