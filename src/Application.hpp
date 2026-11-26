/**
 * @file Application.hpp
 * @brief 应用模块（框架门面）
 * @author Christins
 * @date 2025-03-12
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Streams/ServiceDiscovery.hpp"
#include "Tcp/TcpServer.hpp"

namespace chen {

class ServerRegistry;
class WorkerRuntime;

/**
 * @brief 应用入口
 * @details 只负责参数解析、配置加载、daemon 化与对外门面；具体职责已拆到：
 *          - ProcessSupervisor：master 的 fork/监督/热重载/停机
 *          - WorkerRuntime：worker 的调度器、模块生命周期、drain
 *          - ServerRegistry：TcpServer 的创建/绑定/查找
 */
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
    static Application* GetInstance() { return kInstance; }

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
     * @brief 在所有 server 中获取配置 type 为 name 的 Server
     * @param name server type（如 "http"）
     * @param svrs 拿到的 server
     * @return bool 是否获取成功
     */
    bool getServer(const std::string& name, std::vector<TcpServer::ptr>& svrs);

    /**
     * @brief 获取所有类型的 Server（不含类型区分）
     * @param svrs 拿到的全部 server
     */
    void getAllServers(std::vector<TcpServer::ptr>& svrs);

    /**
     * @brief 获取服务发现实例
     * @details 模块可在 onLoad/onServerReady 中通过 registerService 使用
     */
    IServiceDiscovery::ptr getServiceDiscovery() const;

    /**
     * @brief 设置服务发现实例
     * @details 需要在使用方（如模块 onLoad）中设置；worker 会在模块 onServerReady
     *          之后调用 start()，在模块 onUnload 之后调用 stop()
     */
    void setServiceDiscovery(IServiceDiscovery::ptr sd);

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
    /// 参数个数
    int argc_ = 0;
    /// 参数值
    char** argv_ = nullptr;
    /// pid 文件路径
    std::string pidfile_;
    /// worker 运行时（持有 IOManager，必须比 server 注册表更晚析构）
    /// @note 用 std::shared_ptr 而非 WorkerRuntime::ptr：这里只有前向声明，
    ///       取嵌套 typedef 需要完整类型；shared_ptr 支持不完整类型
    std::shared_ptr<WorkerRuntime> worker_runtime_;
    /// server 注册表（先于 IOManager 析构）
    std::shared_ptr<ServerRegistry> server_registry_;
    /// 服务发现实例（模块可注册/查询）
    IServiceDiscovery::ptr service_discovery_;
    /// 保护 service_discovery_
    mutable std::mutex service_discovery_mutex_;
    /// 程序实例
    static Application* kInstance;
};

} // namespace chen
