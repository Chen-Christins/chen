/**
 * @file ProcessSupervisor.hpp
 * @brief master 进程：fork/监督 worker、热重载、停机
 * @author Christins
 * @date 2026-02-06
 * @copyright GPL-3.0
 */
#pragma once

#include <signal.h>
#include <string>
#include <vector>

#include "ServerRegistry.hpp"
#include "WorkerRuntime.hpp"

namespace chen {

/**
 * @brief master 进程的 worker 监督者
 * @details master 只做三件事：创建/绑定监听 socket、fork 并监督 worker、转发信号。
 *          监听 fd 由 worker 继承，业务逻辑全部在 worker 里跑；热重载 = fork 新 worker
 *          就绪后再让旧 worker drain，旧 worker 退出后不再重启。
 */
class ProcessSupervisor {
public:
    /// 智能指针类型
    typedef std::shared_ptr<ProcessSupervisor> ptr;

    /**
     * @brief 构造函数
     * @param registry server 注册表
     * @param worker worker 运行时（fork 出子进程后进入其 run()）
     */
    ProcessSupervisor(ServerRegistry::ptr registry, WorkerRuntime::ptr worker);

    /**
     * @brief master 主入口：创建 server、写 pidfile、fork 并监督 worker
     * @param pidfile pid 文件路径（启动时写入，退出时删除）
     * @return int 进程退出码
     */
    int run(const std::string& pidfile);

private:
    /**
     * @brief worker 进程记录
     */
    struct WorkerProc {
        /// worker 进程 id
        pid_t pid = -1;
        /// 就绪通知管道读端（-1 表示已关闭）
        int ready_fd = -1;
        /// 是否正在 drain（旧 worker，退出后不再重启）
        bool draining = false;
    };

    /**
     * @brief fork 一个 worker 进程
     * @return int 新 worker 在 workers_ 中的下标，失败返回 -1
     */
    int spawnWorker();

    /**
     * @brief 等待指定 worker 就绪
     * @param w worker 记录
     * @param timeout_ms 超时毫秒数
     * @return bool 是否在超时前就绪
     */
    bool waitWorkerReady(WorkerProc& w, uint64_t timeout_ms);

    /**
     * @brief 收割退出的 worker（SIGCHLD 触发）
     * @details draining/停服中的 worker 直接移除；非预期崩溃则重启
     */
    void reapWorkers();

    /**
     * @brief 可被信号打断的等待（用于崩溃重启 backoff）
     */
    void sleepInterruptible(uint32_t ms);

    /**
     * @brief 执行热重载：fork 新 worker 就绪后让旧 worker drain
     */
    void handleReload();

    /**
     * @brief 停止所有 worker 并等待退出（超时 SIGKILL）
     */
    void shutdownWorkers();

private:
    /// server 注册表
    ServerRegistry::ptr registry_;
    /// worker 运行时
    WorkerRuntime::ptr worker_;
    /// worker 进程列表
    std::vector<WorkerProc> workers_;
    /// pid 文件路径
    std::string pidfile_;
    /// master 是否正在停止（停止期间不再重启崩溃的 worker）
    bool is_stopping_ = false;
    /// master 屏蔽并统一处理的信号集（SIGHUP/SIGTERM/SIGINT/SIGCHLD）
    sigset_t sigset_;
    /// 等待期间收到 SIGHUP，待主循环处理的重载请求
    bool pending_reload_ = false;
    /// 等待期间收到 SIGCHLD，待主循环处理的收割请求
    bool pending_reap_ = false;
};

} // namespace chen
