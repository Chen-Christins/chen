/**
 * @file ProcessSupervisor.cc
 * @brief master 进程实现
 * @author Christins
 * @date 2026-02-06
 * @copyright GPL-3.0
 */
#include "ProcessSupervisor.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Config/Config.hpp"
#include "Daemon.hpp"
#include "Util/Env.hpp"
#include "Util/FSUtil.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<uint64_t>::ptr g_worker_ready_timeout_ms =
    Config::Lookup("server.worker_ready_timeout_ms", (uint64_t)30000, "master waits worker ready timeout ms");

static ConfigVar<uint32_t>::ptr g_worker_restart_interval_ms =
    Config::Lookup("server.worker_restart_interval_ms", (uint32_t)2000, "master restarts crashed worker interval ms");

static ConfigVar<uint64_t>::ptr g_worker_shutdown_timeout_ms = Config::Lookup("server.worker_shutdown_timeout_ms",
    (uint64_t)10000,
    "master waits worker exit before SIGKILL ms (should exceed drain + shutdown grace)");

ProcessSupervisor::ProcessSupervisor(ServerRegistry::ptr registry, WorkerRuntime::ptr worker)
    : registry_(std::move(registry))
    , worker_(std::move(worker))
{
    sigemptyset(&sigset_);
}

int ProcessSupervisor::run(const std::string& pidfile)
{
    pidfile_ = pidfile;

    EnvMgr::GetInstance()->setProcTitleRole("master process");

    if (!registry_->createFromConfigured()) {
        ERROR(logger) << "master: setup servers failed";
        return 1;
    }

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

    sigemptyset(&sigset_);
    sigaddset(&sigset_, SIGHUP);
    sigaddset(&sigset_, SIGTERM);
    sigaddset(&sigset_, SIGINT);
    sigaddset(&sigset_, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigset_, nullptr);

    int idx = spawnWorker();
    if (idx < 0 || !waitWorkerReady(workers_[idx], g_worker_ready_timeout_ms->getValue())) {
        ERROR(logger) << "master: initial worker failed to start";
        shutdownWorkers();
        if (!pidfile_.empty()) {
            FSUtil::Unlink(pidfile_, true);
        }
        return 1;
    }

    while (!is_stopping_) {
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

        int sig = sigwaitinfo(&sigset_, nullptr);
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
            is_stopping_ = true;
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

int ProcessSupervisor::spawnWorker()
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
        worker_->run(pipefd[1]);
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

bool ProcessSupervisor::waitWorkerReady(WorkerProc& w, uint64_t timeout_ms)
{
    if (w.ready_fd < 0) {
        return false;
    }

    pollfd pfd;
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

void ProcessSupervisor::sleepInterruptible(uint32_t ms)
{
    uint64_t remain_ns = static_cast<uint64_t>(ms) * 1000000ULL;
    while (remain_ns > 0 && !is_stopping_) {
        timespec ts;
        ts.tv_sec = static_cast<time_t>(remain_ns / 1000000000ULL);
        ts.tv_nsec = static_cast<long>(remain_ns % 1000000000ULL);

        int sig = sigtimedwait(&sigset_, nullptr, &ts);
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
            is_stopping_ = true;
            return;
        }
        if (sig == SIGHUP) {
            pending_reload_ = true;
        } else if (sig == SIGCHLD) {
            pending_reap_ = true;
        }
    }
}

void ProcessSupervisor::reapWorkers()
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

        if (is_stopping_ || draining) {
            continue;
        }

        // 非预期退出（崩溃）→ 重启，维持服务
        ERROR(logger) << "master: worker pid=" << pid << " crashed unexpectedly, restarting";
        ProcessInfoMgr::GetInstance()->restart_count += 1;
        uint32_t interval = g_worker_restart_interval_ms->getValue();
        if (interval > 0) {
            sleepInterruptible(interval);
        }
        if (is_stopping_) {
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

void ProcessSupervisor::handleReload()
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

void ProcessSupervisor::shutdownWorkers()
{
    is_stopping_ = true;
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

} // namespace chen
