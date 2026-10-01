/**
 * @file tcp_server.h
 * @brief 封装了一个TCP服务器
 * @author Christins
 * @date 2024-11-24
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include "../config/config.h"
#include "../iomanager/iomanager.h"
#include "../rate_limiter/conn_limiter.h"
#include "../socket/address.h"
#include "../socket/socket.h"
#include "../util/noncopyable.h"

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
    int negotiateH2 = 0;
    std::map<std::string, std::string> args;

    bool isValid() const {
        return !address.empty();
    }

    bool operator==(const TcpServerConf& oth) const {
        return address == oth.address
            && keepalive == oth.keepalive
            && timeout == oth.timeout
            && ssl == oth.ssl
            && id == oth.id
            && type == oth.type
            && name == oth.name
            && cert_file == oth.cert_file
            && key_file == oth.key_file
            && accept_worker == oth.accept_worker
            && io_worker == oth.io_worker
            && process_worker == oth.process_worker
            && negotiateH2 == oth.negotiateH2
            && args == oth.args;
    }
};

/**
 * @brief 模版全特化
 * @tparam  std::string -> TcpServerConf
 */
template <>
class LexicalCast<std::string, TcpServerConf> {
public:
    TcpServerConf operator()(const std::string& v) {
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
        conf.negotiateH2 = node["negotiateH2"].as<int>(conf.negotiateH2);
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
    std::string operator()(const TcpServerConf& conf) {
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
        node["negotiateH2"] = conf.negotiateH2;
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
    TcpServer(IOManager* worker = IOManager::GetThis()
            ,IOManager* io_worker = IOManager::GetThis()
            ,IOManager* accept_worker = IOManager::GetThis());

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
    virtual bool bind(const std::vector<Address::ptr>& addrs
                    ,std::vector<Address::ptr>& fails
                    ,bool ssl = false);
    
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
     * @brief 准备新的 dispatch 表（热重载双缓冲）
     * @details 创建空的 pending dispatch，供新模块注册 handler。
     *          默认实现为空（不支持双缓冲的 server 类型无需覆盖）。
     */
    virtual void prepareDispatch() {}

    /**
     * @brief 提交新的 dispatch 表（热重载双缓冲，原子切换）
     * @details 将 pending dispatch 切换为 active dispatch。
     *          切换后新请求走新 dispatch，旧 dispatch 通过 shared_ptr 引用计数自然释放。
     *          默认实现为空。
     */
    virtual void commitDispatch() {}

    /**
     * @brief 获取接收的超时时间
     */
    uint64_t getRecvTimeout() const { return m_recvTimeout; }

    /**
     * @brief 获取当前server的名称
     */
    std::string getName() const { return m_name; }

    /**
     * @brief 设置当前的接收超时时间
     */
    void setRecvTimeout(uint64_t v) { m_recvTimeout = v; }

    /**
     * @brief 设置server名称
     */
    virtual void setName(const std::string& v) { m_name = v; }

    /**
     * @brief 获取server类型
     */
    const std::string& getType() const { return m_type; }
    void setType(const std::string& v) { m_type = v; }

    /**
     * @brief 当前server是否停止运行
     */
    bool isStop() const { return m_isStop; }

    /**
     * @brief 获取当前的TcpServerConf
     */
    TcpServerConf::ptr getConf() { return m_conf; }

    /**
     * @brief 获取连接准入限流器
     * @return ConnLimiter::ptr 未启用限流时返回 nullptr
     */
    ConnLimiter::ptr getConnLimiter() const { return m_connLimiter; }

    /**
     * @brief 设置配置文件对象
     */
    void setConf(TcpServerConf::ptr v) { m_conf = v; }
    void setConf(const TcpServerConf& v);

    /**
     * @brief 主动关闭所有活跃客户端连接
     * @details 类似 nginx 的 ngx_close_listening_sockets 之后对 ngx_connection_t 的遍历关闭。
     *          在优雅关闭流程中，先由各模块 closeAllConnections() 关闭 WebSocket，
     *          再调用此方法关闭残余的 HTTP keepalive 等连接，确保所有连接被精确关闭。
     */
    void closeAllClients();

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
    IOManager* m_worker;
    IOManager* m_ioWorker;
    /// 接收连接线程池
    IOManager* m_acceptWorker;
    /// 存储socket信息, 让这个server可以同时监听多地址
    std::vector<Socket::ptr> m_socks;
    /// 为session设置超时
    uint64_t m_recvTimeout;
    /// 这个 server 的名称
    std::string m_name;
    /// 描述这server是不是停止了
    bool m_isStop;
    /// https服务是否开启
    bool m_ssl = false;
    /// 服务器类型
    std::string m_type = "tcp";
    /// TcpServerConf
    TcpServerConf::ptr m_conf;
    /// 当前活跃的客户端连接集合（用于优雅关闭时遍历关闭所有连接）
    std::set<Socket::ptr> m_activeClients;
    /// 保护 m_activeClients 的互斥锁
    std::mutex m_clientsMutex;
    /// 连接准入限流器（qps + 并发上限）
    ConnLimiter::ptr m_connLimiter;
};

class TcpServerFactory {
public:
    using Creator = std::function<TcpServer::ptr(const TcpServerConf& conf,
                                                 IOManager* process_worker,
                                                 IOManager* io_worker,
                                                 IOManager* accept_worker)>;

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
    static TcpServer::ptr Create(const TcpServerConf& conf,
                                 IOManager* process_worker,
                                 IOManager* io_worker,
                                 IOManager* accept_worker);

    /**
     * @brief 列出已注册的类型
     */
    static std::vector<std::string> ListTypes();
};

}
