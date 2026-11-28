/**
 * @file WorkerRuntime.hpp
 * @brief worker 进程运行时：协程调度器、模块生命周期、优雅关闭
 * @author Christins
 * @date 2026-02-06
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "IOManager/IOManager.hpp"
#include "ServerRegistry.hpp"
#include "Timer/Timer.hpp"
#include "Watcher/FileWatcher.hpp"

namespace chen {

/**
 * @brief 单个 worker 进程内的运行时
 * @details 负责：一个 IOManager（main）、模块生命周期、服务发现启停、
 *          配置文件 inotify 热加载、信号触发的优雅关闭与 drain。
 *          master 进程不会创建它。
 */
class WorkerRuntime {
public:
    /// 智能指针类型
    typedef std::shared_ptr<WorkerRuntime> ptr;

    /**
     * @brief 构造函数
     * @param registry server 注册表（由 master fork 前填充，worker 继承）
     */
    explicit WorkerRuntime(ServerRegistry::ptr registry);

    /**
     * @brief 设置进程参数（模块 onBeforeArgsParse/onAfterArgsParse 使用）
     */
    void setArgs(int argc, char** argv);

    /**
     * @brief worker 进程入口：创建调度器、加载模块、启动 server，然后进入事件循环
     * @param ready_fd 就绪通知管道写端
     * @note 本函数不返回，结束时直接 _exit
     */
    void run(int ready_fd);

    /**
     * @brief 信号处理器：仅置优雅关闭标志
     * @details SIGINT/SIGTERM/SIGQUIT 由 housekeeping 定时器消费，真正关闭在协程里执行
     */
    static void signalHandler(int sig);

private:
    /**
     * @brief 注册 worker 信号处理器（SIGHUP/SIGPIPE 忽略）
     */
    void registerSignals();

    /**
     * @brief 启动协程，worker 主要的程序执行逻辑
     * @return int
     */
    int runFiber();

    /**
     * @brief worker 启动完成后通知 master 就绪
     */
    void reportWorkerReady();

    /**
     * @brief worker 启动失败时通知 master
     */
    void reportWorkerFailed();

    /**
     * @brief housekeeping 定时回调：检查信号标志并触发优雅关闭
     */
    void onHousekeeping();

    /**
     * @brief 执行优雅关闭：beginDrain → 等待在途请求 → finishShutdown
     */
    void doGracefulShutdown();

    /**
     * @brief drain 完成后卸载模块、停线程池并退出
     */
    void finishShutdown();

    /**
     * @brief 启动配置文件 inotify 监听器
     */
    void startConfigWatcher();

    /**
     * @brief 执行配置文件重载（inotify 触发，单文件）
     * @param file 变更的配置文件路径
     */
    void doConfigReload(const std::string& file);

private:
    /// server 注册表
    ServerRegistry::ptr registry_;
    /// 参数个数
    int argc_ = 0;
    /// 参数值
    char** argv_ = nullptr;
    /// 主协程调度器：IO、业务协程与模块 onTick 定时器共用（不再单独起 tick 线程）
    IOManager::ptr main_iomanager_;
    /// tick 定时器列表（用于关闭时取消）
    std::vector<Timer::ptr> tick_timers_;
    /// housekeeping 定时器
    Timer::ptr housekeeping_timer_;
    /// 配置文件 inotify 监听器
    FileWatcher::ptr config_watcher_;
    /// 就绪通知管道写端（-1 表示已关闭）
    int ready_fd_ = -1;
    /// 是否正在关闭（防止重复执行）
    bool is_shutting_down_ = false;

    /// 优雅关闭信号标志
    static std::atomic<bool> kShutdownSignaled;
};

} // namespace chen
