/**
 * @file application.h
 * @brief 应用模块
 * @author Christins
 * @date 2025-03-12
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>

#include "tcp/tcp_server.h"
#include "timer/timer.h"
#include "watcher/file_watcher.h"

namespace chen {

class Application {
public:
    /**
     * @brief 构造函数
     */
    Application();
    /**
     * @brief 获取实例
     * @return Application*
     */
    static Application* GetInstance() { return m_instance; }

    /**
     * @brief 初始化传入参数
     * @param argc 参数个数
     * @param argv 参数值
     * @return bool 是否初始化成功
     */
    bool init(int argc, char** argv);

    /**
     * @brief 程序运行
     * @return bool 是否成功运行
     */
    bool run();

    /**
     * @brief 在所有server中获取名称为name的Server
     * @param name server名称
     * @param svrs 拿到的server
     * @return bool 是否获取成功
     */
    bool getServer(const std::string& name, std::vector<TcpServer::ptr>& svrs);

    /**
     * @brief 获取所有类型的 Server（不含类型区分）
     * @param svrs 拿到的全部 server
     */
    void getAllServers(std::vector<TcpServer::ptr>& svrs);

    /**
     * @brief 信号处理器（SIGINT/SIGTERM/SIGQUIT → worker 优雅关闭）
     * @details 通过原子标志通知 housekeeping 定时器执行实际操作
     */
    static void signalHandler(int sig);

    /**
     * @brief 向运行中的实例发送信号（类似 nginx -s reload）
     * @param sig_name 信号名称："reload"、"stop" 或 "quit"
     * @return 0 成功，-1 失败
     */
    static int sendSignal(const std::string& sig_name);

private:
    /**
     * @brief worker 进程记录（master 使用）
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
     * @brief master 主入口：绑定监听、写 pidfile、监督 worker
     * @return int 进程退出码
     */
    int masterMain();

    /**
     * @brief master 解析 servers 配置并创建/绑定所有 server（worker 指针留空）
     * @details fork 后由 worker 继承已绑定好的监听 socket
     * @return bool 是否全部成功
     */
    bool setupServers();

    /**
     * @brief master fork 一个 worker 进程
     * @return int 新 worker 在 m_workers 中的下标，失败返回 -1
     */
    int spawnWorker();

    /**
     * @brief master 等待指定 worker 就绪
     * @param w worker 记录
     * @param timeout_ms 超时毫秒数
     * @return bool 是否在超时前就绪
     */
    bool waitWorkerReady(WorkerProc& w, uint64_t timeout_ms);

    /**
     * @brief master 收割退出的 worker（SIGCHLD 触发）
     * @details draining/停服中的 worker 直接移除；非预期崩溃则重启
     */
    void reapWorkers();

    /**
     * @brief 可被信号打断的等待（用于崩溃重启 backoff）
     * @details 在 m_masterSigset 上 sigtimedwait；收到 SIGTERM/SIGINT 立即置
     *          m_isMasterStopping 并返回，SIGHUP/SIGCHLD 记为待处理
     * @param ms 等待毫秒数
     */
    void sleepInterruptible(uint32_t ms);

    /**
     * @brief master 执行热重载：fork 新 worker 就绪后让旧 worker drain
     */
    void handleReload();

    /**
     * @brief master 停止所有 worker 并等待退出
     */
    void shutdownWorkers();

    /**
     * @brief worker 进程入口：创建调度器并运行
     * @param ready_fd 就绪通知管道写端
     */
    void workerMain(int ready_fd);

    /**
     * @brief worker 启动完成后通知 master 就绪
     */
    void reportWorkerReady();

    /**
     * @brief worker 启动失败时通知 master
     */
    void reportWorkerFailed();

    /**
     * @brief 启动协程，worker 主要的程序执行逻辑
     * @return int
     */
    int run_fiber();

    /**
     * @brief 注册 worker 信号处理器
     * @details 注册 SIGINT/SIGTERM/SIGQUIT（优雅关闭），忽略 SIGHUP/SIGPIPE
     */
    void registerSignals();

    /**
     * @brief housekeeping 定时回调：检查信号标志并触发相应操作
     */
    void onHousekeeping();

    /**
     * @brief 执行 worker 优雅关闭流程
     * @details 1. 停止所有 TcpServer 接收新连接
     *          2. 限时等待现有连接排空
     *          3. 卸载所有模块
     *          4. 停止所有工作线程
     *          5. 退出（pid 文件由 master 负责清理）
     */
    void doGracefulShutdown();

    /**
     * @brief 执行配置文件重载（inotify 触发，单文件）
     * @param file 变更的配置文件路径
     */
    void doConfigReload(const std::string& file);

    /**
     * @brief 启动配置文件 inotify 监听器
     */
    void startConfigWatcher();

private:
    /// 参数个数
    int m_argc;
    /// 参数值
    char** m_argv = nullptr;
    /// 主协程调度器
    IOManager::ptr m_mainIOManager;
    /// tick 专用调度器（独立于主调度器，避免 tick 逻辑阻塞 IO 事件）
    IOManager::ptr m_tickIOManager;
    /// tick 定时器列表（用于关闭时取消）
    std::vector<Timer::ptr> m_tickTimers;
    /// housekeeping 定时器
    Timer::ptr m_housekeepingTimer;
    /// 配置文件 inotify 监听器
    FileWatcher::ptr m_configWatcher;
    /// pid 文件路径
    std::string m_pidfile;
    /// 是否正在关闭（防止重复执行）
    bool m_isShuttingDown = false;
    /// worker 进程列表（master 使用）
    std::vector<WorkerProc> m_workers;
    /// 当前进程是否为 worker
    bool m_isWorker = false;
    /// master 是否正在停止（停止期间不再重启崩溃的 worker）
    bool m_isMasterStopping = false;
    /// master 屏蔽并统一处理的信号集（SIGHUP/SIGTERM/SIGINT/SIGCHLD）
    sigset_t m_masterSigset;
    /// 等待期间收到 SIGHUP，待主循环处理的重载请求
    bool m_pendingReload = false;
    /// 等待期间收到 SIGCHLD，待主循环处理的收割请求
    bool m_pendingReap = false;
    /// worker 就绪通知管道写端（worker 使用，-1 表示已关闭）
    int m_readyFd = -1;
    /// 所有server列表 — 必须声明在 IOManager 之后，确保 ~Application() 时
    /// IOManager 比 m_servers 更晚析构，TcpServer::close() 调用时 FdMgr 等存活
    std::map<std::string, std::vector<TcpServer::ptr>> m_servers;
    /// 程序实例
    static Application* m_instance;
    /// 优雅关闭信号标志
    static std::atomic<bool> s_shutdownSignaled;
};

} // namespace chen
