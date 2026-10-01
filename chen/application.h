/**
 * @file application.h
 * @brief 应用模块
 * @author Christins
 * @date 2025-03-12
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>
#include <csignal>

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
     * @brief 信号处理器（SIGINT/SIGTERM → 关闭，SIGHUP → 重载）
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
     * @brief 执行main函数，run的逻辑包含这个main
     * @param argc 参数个数
     * @param argv 参数值
     * @return int
     */
    int main(int argc, char** argv);

    /**
     * @brief 启动协程，主要的程序执行逻辑
     * @return int
     */
    int run_fiber();

    /**
     * @brief 注册信号处理器
     * @details 注册 SIGINT/SIGTERM（优雅关闭）和 SIGHUP（热重载）
     */
    void registerSignals();

    /**
     * @brief housekeeping 定时回调：检查信号标志并触发相应操作
     */
    void onHousekeeping();

    /**
     * @brief 执行优雅关闭流程
     * @details 1. 停止所有 TcpServer 接收新连接
     *          2. 等待现有连接排空
     *          3. 卸载所有模块
     *          4. 停止所有工作线程
     *          5. 清理 pid 文件
     */
    void doGracefulShutdown();

    /**
     * @brief 执行热重载流程（仅模块重载，SIGHUP 触发）
     * @details 扫描模块目录并重载变更的模块
     */
    void doHotReload();

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
    /// 所有server列表 — 必须声明在 IOManager 之后，确保 ~Application() 时
    /// IOManager 比 m_servers 更晚析构，TcpServer::close() 调用时 FdMgr 等存活
    std::map<std::string, std::vector<TcpServer::ptr>> m_servers;
    /// 程序实例
    static Application* m_instance;
    /// 优雅关闭信号标志
    static std::atomic<bool> s_shutdownSignaled;
    /// 热重载信号标志
    static std::atomic<bool> s_reloadSignaled;
};

} // namespace chen
