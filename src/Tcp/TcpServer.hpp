/**
 * @file TcpServer.hpp
 * @brief 封装了一个TCP服务器
 * @author Christins
 * @date 2024-11-24
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "../Config/Config.hpp"
#include "../IOManager/IOManager.hpp"
#include "../RateLimiter/ConnLimiter.hpp"
#include "../Socket/Address.hpp"
#include "../Socket/Socket.hpp"
#include "../Util/Noncopyable.hpp"

namespace chen {

/**
 * @brief TcpServer(HTTP, WebSocket) 的结构
 */
struct TcpServerConf {
    typedef std::shared_ptr<TcpServerConf> ptr;

    std::vector<std::string> address;
    int keepalive = 0;
    int timeout = 1000 * 2 * 60;
    int ssl = 0;
    std::string id;
    std::string type = "http";
    std::string name;
    std::string cert_file;
    std::string key_file;
    std::string accept_worker;
    std::string io_worker;
    std::string process_worker;
    int negotiate_h2 = 0;
    std::map<std::string, std::string> args;

    bool isValid() const { return !address.empty(); }

    bool operator==(const TcpServerConf& oth) const;
};

/**
 * @brief 模版全特化
 * @tparam  std::string -> TcpServerConf
 */
template <>
class LexicalCast<std::string, TcpServerConf> {
public:
    TcpServerConf operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        TcpServerConf conf;
        conf.id = node["id"].as<std::string>(conf.id);
        conf.type = node["type"].as<std::string>(conf.type);
        conf.keepalive = node["keepalive"].as<int>(conf.keepalive);
        conf.timeout = node["timeout"].as<int>(conf.timeout);
        conf.name = node["name"].as<std::string>(conf.name);
        conf.ssl = node["ssl"].as<int>(conf.ssl);
        conf.cert_file = node["cert_file"].as<std::string>(conf.cert_file);
        conf.key_file = node["key_file"].as<std::string>(conf.key_file);
        conf.accept_worker = node["accept_worker"].as<std::string>();
        conf.io_worker = node["io_worker"].as<std::string>();
        conf.process_worker = node["process_worker"].as<std::string>();
        conf.negotiate_h2 = node["negotiateH2"].as<int>(conf.negotiate_h2);
        if (node["args"].IsDefined()) {
            std::stringstream aa;
            aa << node["args"];
            conf.args = LexicalCast<std::string, std::map<std::string, std::string>>()(aa.str());
        }
        if (node["address"].IsDefined()) {
            for (size_t i = 0; i < node["address"].size(); ++i) {
                conf.address.push_back(node["address"][i].as<std::string>());
            }
        }
        return conf;
    }
};

/**
 * @brief 模版全特化
 * @tparam  TcpServerConf -> std::string
 */
template <>
class LexicalCast<TcpServerConf, std::string> {
public:
    std::string operator()(const TcpServerConf& conf)
    {
        YAML::Node node;
        node["id"] = conf.id;
        node["type"] = conf.type;
        node["keepalive"] = conf.keepalive;
        node["timeout"] = conf.timeout;
        node["name"] = conf.name;
        node["ssl"] = conf.ssl;
        node["cert_file"] = conf.cert_file;
        node["key_file"] = conf.key_file;
        node["accept_worker"] = conf.accept_worker;
        node["io_worker"] = conf.io_worker;
        node["process_worker"] = conf.process_worker;
        node["negotiateH2"] = conf.negotiate_h2;
        node["args"] = YAML::Load(LexicalCast<std::map<std::string, std::string>, std::string>()(conf.args));
        for (auto& i : conf.address) {
            node["address"].push_back(i);
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

class TcpServer : public std::enable_shared_from_this<TcpServer>, Noncopyable {
public:
    typedef std::shared_ptr<TcpServer> ptr;
    /**
     * @brief 构造函数
     * @param worker 主要的工作线程池
     * @param accept_worker 用于接收连接的线程池
     */
    TcpServer(IOManager* worker = IOManager::GetThis(), IOManager* io_worker = IOManager::GetThis(), IOManager* accept_worker = IOManager::GetThis());

    /**
     * @brief 析构函数
     */
    virtual ~TcpServer();

    /**
     * @brief 绑定单个地址
     * @param[in] addr 地址信息
     * @return bool 成功与否
     */
    virtual bool bind(Address::ptr addr, bool ssl = false);

    /**
     * @brief 绑定多个地址信息
     * @param[in] addrs 地址信息集合
     * @param[out] fails 存绑定失败了的地址信息
     * @return bool 成功与否
     */
    virtual bool bind(const std::vector<Address::ptr>& addrs, std::vector<Address::ptr>& fails, bool ssl = false);

    /**
     * @brief 开启TCPServer
     * @return bool 成功与否
     */
    virtual bool start();

    /**
     * @brief 停止TcpServer
     */
    virtual void stop();

    /**
     * @brief 设置 server 使用的三个 IOManager
     * @details 供多进程模型使用：master 创建/绑定 server 时以空 worker 构造，
     *          worker fork 后按配置解析出真实 worker 再注入，随后才能 start()。
     * @param process_worker 业务处理线程池
     * @param io_worker IO 事件线程池
     * @param accept_worker 接收连接线程池
     */
    void setWorkers(IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker);

    /**
     * @brief 获取接收的超时时间
     */
    uint64_t getRecvTimeout() const { return recv_timeout_; }

    /**
     * @brief 获取当前server的名称
     */
    std::string getName() const { return name_; }

    /**
     * @brief 设置当前的接收超时时间
     */
    void setRecvTimeout(uint64_t v) { recv_timeout_ = v; }

    /**
     * @brief 设置server名称
     */
    virtual void setName(const std::string& v) { name_ = v; }

    /**
     * @brief 获取server类型
     */
    const std::string& getType() const { return type_; }
    void setType(const std::string& v) { type_ = v; }

    /**
     * @brief 当前server是否停止运行
     */
    bool isStop() const { return is_stop_; }

    /**
     * @brief 获取当前的TcpServerConf
     */
    TcpServerConf::ptr getConf() { return conf_; }

    /**
     * @brief 获取所有监听 socket
     * @details 用于服务发现注册等场景读取实际绑定的地址；返回拷贝避免与 stop() 并发
     */
    std::vector<Socket::ptr> getSocks() const { return socks_; }

    /**
     * @brief 获取连接准入限流器
     * @return ConnLimiter::ptr 未启用限流时返回 nullptr
     */
    ConnLimiter::ptr getConnLimiter() const { return conn_limiter_; }

    /**
     * @brief 设置配置文件对象
     */
    void setConf(TcpServerConf::ptr v) { conf_ = v; }
    void setConf(const TcpServerConf& v);

    /**
     * @brief 主动关闭所有活跃客户端连接
     * @details 类似 nginx 的 ngx_close_listening_sockets 之后对 ngx_connection_t 的遍历关闭。
     *          在优雅关闭流程中，先由各模块 closeAllConnections() 关闭 WebSocket，
     *          再调用此方法关闭残余的 HTTP keepalive 等连接，确保所有连接被精确关闭。
     */
    void closeAllClients();

    /**
     * @brief 是否支持协作式 drain（停止 accept 后等待在途请求处理完）
     * @details HTTP/1.1 请求边界清晰，可在 handleClient 内标记 beginRequest/endRequest；
     *          长连接协议（WS/Rock/RPC/Game）没有请求边界，默认返回 false，
     *          由关闭流程直接 closeAllClients()。
     */
    virtual bool supportsGracefulDrain() const { return false; }

    /**
     * @brief 进入 draining：停止 accept，但不主动关闭存量连接
     * @details 之后由 Application 周期性调用 closeIdleClients() 关闭空闲连接，
     *          等 getInFlightClientCount() 归零或超时后再强制关闭。
     */
    void beginDrain();

    /**
     * @brief 是否处于 draining 状态
     */
    bool isDraining() const { return is_draining_; }

    /**
     * @brief 关闭空闲（没有在途请求）的连接
     * @return 本次关闭的连接数
     */
    size_t closeIdleClients();

    /**
     * @brief 存在在途请求的连接数
     */
    size_t getInFlightClientCount();

    /**
     * @brief 获取当前活跃客户端连接数
     * @return size_t 活跃连接数量
     */
    size_t getActiveClientCount();

    /**
     * @brief 加载https证书
     * @param cert_file
     * @param key_file
     * @return bool
     */
    bool loadCertificates(const std::string& cert_file, const std::string& key_file);

    /**
     * @brief 打印当前server的状态日志
     * @param prefix 前缀
     * @return 状态文本
     */
    std::string toString(const std::string& prefix);

protected:
    /**
     * @brief 进入 draining 时的回调，子类可在此通知长连接主动收尾
     * @details 调用时机：beginDrain() 停止 accept 之后、Application 开始轮询之前。
     *          例如 HttpServer 会向所有活跃 HTTP/2 会话发送 GOAWAY。
     */
    virtual void onDrain() {}

    /**
     * @brief 标记一个请求开始（in_flight++）
     */
    void beginRequest(const Socket::ptr& client);

    /**
     * @brief 标记一个请求结束（in_flight--，draining 且空闲时关闭连接）
     */
    void endRequest(const Socket::ptr& client);

    /**
     * @brief 请求级 RAII 标记
     * @details 构造时 in_flight++，析构时 in_flight--；若此时正在 draining 且连接已空闲，
     *          顺手关闭连接。协议实现只需在「开始处理一个请求」的作用域里放一个 guard。
     */
    class RequestGuard : public Noncopyable {
    public:
        RequestGuard(TcpServer* server, Socket::ptr client)
            : server_(server)
            , client_(std::move(client))
        {
            server_->beginRequest(client_);
        }

        ~RequestGuard() { server_->endRequest(client_); }

    private:
        TcpServer* server_;
        Socket::ptr client_;
    };

    /**
     * @brief 处理客户端请求
     * @param client 客户端的socket信息
     */
    virtual void handleClient(Socket::ptr client);

    /**
     * @brief 开始接收客户端连接
     * @param sock 客户端socket信息
     */
    virtual void startAccept(Socket::ptr sock);

protected:
    /// 工作线程池
    IOManager* worker_;
    IOManager* io_worker_;
    /// 接收连接线程池
    IOManager* accept_worker_;
    /// 存储socket信息, 让这个server可以同时监听多地址
    std::vector<Socket::ptr> socks_;
    /// 为session设置超时
    uint64_t recv_timeout_;
    /// 这个 server 的名称
    std::string name_;
    /// 描述这server是不是停止了
    bool is_stop_;
    /// https服务是否开启
    bool ssl_ = false;
    /// 服务器类型
    std::string type_ = "tcp";
    /// TcpServerConf
    TcpServerConf::ptr conf_;
    /// 单个连接的 drain 状态
    struct ClientState {
        /// 在途请求数（>0 表示不能直接关闭）
        int in_flight = 0;
        /// 是否已被关闭流程摘除
        bool closing = false;
    };
    /// 当前活跃的客户端连接：socket -> 状态（key 持有 shared_ptr，保证遍历期间连接存活）
    std::map<Socket::ptr, ClientState> active_clients_;
    /// 保护 active_clients_ 的互斥锁
    std::mutex clients_mutex_;
    /// 是否处于 draining（停止接收新连接、等待在途请求）
    std::atomic<bool> is_draining_{false};
    /// 连接准入限流器（qps + 并发上限）
    ConnLimiter::ptr conn_limiter_;
};

class TcpServerFactory {
public:
    using Creator =
        std::function<TcpServer::ptr(const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker)>;

    /**
     * @brief 注册服务器创建器
     * @param type 服务器类型名（对应配置中的type）
     * @param cb 创建回调
     * @return bool 注册是否成功（类型名重复时返回false）
     */
    static bool Register(const std::string& type, Creator cb);

    /**
     * @brief 取消注册服务器创建器
     */
    static bool Unregister(const std::string& type);

    /**
     * @brief 根据配置创建服务器
     */
    static TcpServer::ptr Create(const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker);

    /**
     * @brief 列出已注册的类型
     */
    static std::vector<std::string> ListTypes();
};

} // namespace chen
