#include "TcpServer.hpp"

#include <mutex>
#include <utility>

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"

namespace chen {

bool TcpServerConf::operator==(const TcpServerConf& oth) const
{
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
        && process_worker == oth.process_worker && negotiate_h2 == oth.negotiate_h2 && args == oth.args;
}

namespace {

struct TcpServerFactoryRegistry {
    std::map<std::string, TcpServerFactory::Creator> creators;
    std::mutex mutex;
};

TcpServerFactoryRegistry& GetServerFactoryRegistry()
{
    static TcpServerFactoryRegistry instance;
    return instance;
}

/**
 * @brief 获取 IOManager 显示名，未设置/空名返回 "-"
 */
std::string WorkerName(const IOManager* iom)
{
    if (!iom) {
        return "-";
    }
    const std::string& name = iom->getName();
    return name.empty() ? "-" : name;
}

} // namespace

static ConfigVar<uint64_t>::ptr g_tcp_server_read_timeout =
    Config::Lookup("tcp_server.read_timeout", (uint64_t)(60 * 1000 * 2), "tcp server read timeout");

static Logger::ptr logger = LOG_NAME("system");

TcpServer::TcpServer(IOManager* worker, IOManager* io_worker, IOManager* accept_worker)
    : worker_(worker)
    , io_worker_(io_worker)
    , accept_worker_(accept_worker)
    , recv_timeout_(g_tcp_server_read_timeout->getValue())
    , name_("chen/1.0.0")
    , is_stop_(true)
{
}

TcpServer::~TcpServer()
{
    for (auto& i : socks_) {
        i->close();
    }
    socks_.clear();
}

bool TcpServer::bind(Address::ptr addr, bool ssl)
{
    std::vector<Address::ptr> addrs;
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails, ssl);
}

bool TcpServer::bind(const std::vector<Address::ptr>& addrs, std::vector<Address::ptr>& fails, bool ssl)
{
    ssl_ = ssl;
    for (auto& addr : addrs) {
        Socket::ptr sock = ssl ? SSLSocket::CreateTCP(addr) : Socket::CreateTCP(addr);
        if (!sock->bind(addr)) {
            ERROR(logger) << "bind fail errno=" << errno << " errstr=" << strerror(errno) << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        if (!sock->listen()) {
            ERROR(logger) << "listen fail errno=" << errno << " errstr=" << strerror(errno) << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        socks_.push_back(sock);
    }

    // 如果存在失败的，直接清空所有的socket
    if (!fails.empty()) {
        socks_.clear();
        return false;
    }
    for (auto& i : socks_) {
        INFO(logger) << "type=" << type_ << " name=" << name_ << " ssl=" << ssl_ << " server bind success: " << *i;
    }
    return true;
}

bool TcpServer::start()
{
    // 是开启的，就不用操作了
    if (!is_stop_) {
        return true;
    }
    is_stop_ = false;
    for (auto& sock : socks_) {
        accept_worker_->schedule(std::bind(&TcpServer::startAccept, shared_from_this(), sock));
    }
    return true;
}

void TcpServer::stop()
{
    is_stop_ = true;
    auto self = shared_from_this();
    // 将this和self作为参数传递给异步任务的lambda函数，以确保异步任务执行期间当前对象的shared_ptr一直有效
    // 避免析构
    accept_worker_->schedule([this, self]() {
        for (auto& sock : socks_) {
            // 取消socket上的所有事件
            sock->cancelAll();
            // 关闭socket
            sock->close();
        }
        // 清空
        socks_.clear();
    });
}

void TcpServer::setWorkers(IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker)
{
    worker_ = process_worker;
    io_worker_ = io_worker;
    accept_worker_ = accept_worker;
}

void TcpServer::handleClient(Socket::ptr client)
{
    INFO(logger) << "handleClient: " << *client;
}

size_t TcpServer::getActiveClientCount()
{
    std::lock_guard lock(clients_mutex_);
    return active_clients_.size();
}

void TcpServer::closeAllClients()
{
    std::set<Socket::ptr> clients;
    {
        std::lock_guard lock(clients_mutex_);
        clients.swap(active_clients_);
    }
    INFO(logger) << "closeAllClients: closing " << clients.size() << " active connections for server " << name_
                 << " type=" << type_;
    for (auto& client : clients) {
        if (client) {
            client->close();
        }
    }
}

void TcpServer::startAccept(Socket::ptr sock)
{
    while (!is_stop_) {
        Socket::ptr client = sock->accept();
        if (client) {
            if (conn_limiter_ && !conn_limiter_->tryAcquire()) {
                WARN(logger) << "rate limited, close client=" << *client << " current=" << conn_limiter_->current()
                             << " rejected=" << conn_limiter_->getRejectedCount();
                client->close();
                continue;
            }
            client->setRecvTimeout(recv_timeout_);
            {
                std::lock_guard lock(clients_mutex_);
                active_clients_.insert(client);
            }
            io_worker_->schedule([this, self = shared_from_this(), client]() {
                handleClient(client);
                {
                    std::lock_guard lock(clients_mutex_);
                    active_clients_.erase(client);
                }
                if (conn_limiter_) {
                    conn_limiter_->release();
                }
            });
        } else {
            // stop() 关闭了监听 socket，accept 返回 EBADF 是预期行为，不打错误日志
            // EAGAIN/EWOULDBLOCK 在非阻塞 socket 上是正常行为
            if (!is_stop_ && errno != 0 && errno != EBADF && errno != EAGAIN && errno != EWOULDBLOCK) {
                ERROR(logger) << "accept errno=" << errno << " errstr=" << strerror(errno);
            }
        }
    }
}

void TcpServer::setConf(const TcpServerConf& v)
{
    conf_.reset(new TcpServerConf(v));
    if (v.timeout > 0) {
        recv_timeout_ = static_cast<uint64_t>(v.timeout);
    }
    conn_limiter_ = ConnLimiter::CreateFromArgs(v.args);
    DEBUG(logger) << "apply server conf"
                  << " name=" << (v.name.empty() ? name_ : v.name) << " type=" << v.type
                  << " conf_timeout_ms=" << v.timeout << " effective_recv_timeout_ms=" << recv_timeout_;
}

bool TcpServer::loadCertificates(const std::string& cert_file, const std::string& key_file)
{
    for (auto& i : socks_) {
        auto ssl_sock = std::dynamic_pointer_cast<SSLSocket>(i);
        if (ssl_sock) {
            if (!ssl_sock->loadCertificates(cert_file, key_file)) {
                return false;
            }
        }
    }
    return true;
}

std::string TcpServer::toString(const std::string& prefix)
{
    std::stringstream ss;
    ss << prefix << "[type=" << type_ << " name=" << (name_.empty() ? "-" : name_) << " worker=" << WorkerName(worker_)
       << " io=" << WorkerName(io_worker_) << " accept=" << WorkerName(accept_worker_)
       << " recv_timeout=" << recv_timeout_ << " ssl=" << (ssl_ ? 1 : 0);

    if (conn_limiter_ && conn_limiter_->isLimited()) {
        ss << " conn=" << conn_limiter_->current() << "/" << (int64_t)conn_limiter_->getMaxConn()
           << " rejected=" << conn_limiter_->getRejectedCount();
    }
    if (!socks_.empty()) {
        ss << " listen=[";
        for (size_t i = 0; i < socks_.size(); ++i) {
            if (i > 0) {
                ss << ",";
            }
            auto addr = socks_[i]->getLocalAddress();
            ss << (addr ? addr->toString() : "unknown");
        }
        ss << "]";
    }
    ss << "]";
    return ss.str();
}

bool TcpServerFactory::Register(const std::string& type, Creator cb)
{
    if (type.empty() || !cb) {
        return false;
    }
    auto& registry = GetServerFactoryRegistry();
    std::lock_guard lock(registry.mutex);
    return registry.creators.emplace(type, std::move(cb)).second;
}

bool TcpServerFactory::Unregister(const std::string& type)
{
    auto& registry = GetServerFactoryRegistry();
    std::lock_guard lock(registry.mutex);
    return registry.creators.erase(type) > 0;
}

TcpServer::ptr TcpServerFactory::Create(const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker)
{
    auto& registry = GetServerFactoryRegistry();
    std::lock_guard lock(registry.mutex);
    auto it = registry.creators.find(conf.type);
    if (it == registry.creators.end()) {
        return nullptr;
    }
    return it->second(conf, process_worker, io_worker, accept_worker);
}

std::vector<std::string> TcpServerFactory::ListTypes()
{
    auto& registry = GetServerFactoryRegistry();
    std::lock_guard lock(registry.mutex);
    std::vector<std::string> types;
    types.reserve(registry.creators.size());
    for (auto& [k, _] : registry.creators) {
        types.push_back(k);
    }
    return types;
}

} // namespace chen
