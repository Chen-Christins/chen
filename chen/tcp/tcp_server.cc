#include "tcp_server.h"

#include <mutex>
#include <utility>

#include "../config/config.h"
#include "../log/log.h"

namespace chen {

namespace {

struct TcpServerFactoryRegistry {
    std::map<std::string, TcpServerFactory::Creator> creators;
    std::mutex mutex;
};

TcpServerFactoryRegistry& GetServerFactoryRegistry() {
    static TcpServerFactoryRegistry instance;
    return instance;
}

/**
 * @brief 获取 IOManager 显示名，未设置/空名返回 "-"
 */
static std::string WorkerName(const IOManager* iom) {
    if (!iom) {
        return "-";
    }
    const std::string& name = iom->getName();
    return name.empty() ? "-" : name;
}

}

static ConfigVar<uint64_t>::ptr g_tcp_server_read_timeout =
    Config::Lookup("tcp_server.read_timeout", (uint64_t)(60 * 1000 * 2), "tcp server read timeout");

static Logger::ptr logger = LOG_NAME("system");

TcpServer::TcpServer(IOManager* worker, IOManager* io_worker, IOManager* accept_worker)
    :m_worker(worker)
    ,m_ioWorker(io_worker)
    ,m_acceptWorker(accept_worker)
    ,m_recvTimeout(g_tcp_server_read_timeout->getValue())
    ,m_name("chen/1.0.0")
    ,m_isStop(true) {
}

TcpServer::~TcpServer() {
    for (auto& i : m_socks) {
        i->close();
    }
    m_socks.clear();
}

bool TcpServer::bind(Address::ptr addr, bool ssl) {
    std::vector<Address::ptr> addrs;
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails, ssl);
}

bool TcpServer::bind(const std::vector<Address::ptr>& addrs, std::vector<Address::ptr>& fails, bool ssl) {
    m_ssl = ssl;
    for (auto& addr : addrs) {
        Socket::ptr sock = ssl ? SSLSocket::CreateTCP(addr) : Socket::CreateTCP(addr);
        if (!sock->bind(addr)) {
            ERROR(logger) << "bind fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue ;
        }
        if (!sock->listen()) {
            ERROR(logger) << "listen fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue ;
        }
        m_socks.push_back(sock);
    }

    // 如果存在失败的，直接清空所有的socket
    if (!fails.empty()) {
        m_socks.clear();
        return false;
    }
    for (auto& i : m_socks) {
        INFO(logger) << "type=" << m_type << " name=" << m_name
            << " ssl=" << m_ssl << " server bind success: " << *i;
    }
    return true;
}

bool TcpServer::start() {
    // 是开启的，就不用操作了
    if (!m_isStop) {
        return true;
    }
    m_isStop = false;
    for (auto& sock : m_socks) {
        m_acceptWorker->schedule(std::bind(&TcpServer::startAccept, shared_from_this(), sock));
    }
    return true;
}

void TcpServer::stop() {
    m_isStop = true;
    auto self = shared_from_this();
    // 将this和self作为参数传递给异步任务的lambda函数，以确保异步任务执行期间当前对象的shared_ptr一直有效
    // 避免析构
    m_acceptWorker->schedule([this, self]() {
        for (auto& sock : m_socks) {
            // 取消socket上的所有事件
            sock->cancelAll();
            // 关闭socket
            sock->close();
        }
        // 清空
        m_socks.clear();
    });
}

void TcpServer::handleClient(Socket::ptr client) {
    INFO(logger) << "handleClient: " << *client;
}

void TcpServer::closeAllClients() {
    std::set<Socket::ptr> clients;
    {
        std::lock_guard lock(m_clientsMutex);
        clients.swap(m_activeClients);
    }
    INFO(logger) << "closeAllClients: closing " << clients.size()
        << " active connections for server " << m_name << " type=" << m_type;
    for (auto& client : clients) {
        if (client) {
            client->close();
        }
    }
}

void TcpServer::startAccept(Socket::ptr sock) {
    while (!m_isStop) {
        Socket::ptr client = sock->accept();
        if (client) {
            if (m_connLimiter && !m_connLimiter->tryAcquire()) {
                WARN(logger) << "rate limited, close client=" << *client
                    << " current=" << m_connLimiter->current()
                    << " rejected=" << m_connLimiter->getRejectedCount();
                client->close();
                continue;
            }
            client->setRecvTimeout(m_recvTimeout);
            {
                std::lock_guard lock(m_clientsMutex);
                m_activeClients.insert(client);
            }
            m_ioWorker->schedule([this, self = shared_from_this(), client]() {
                handleClient(client);
                {
                    std::lock_guard lock(m_clientsMutex);
                    m_activeClients.erase(client);
                }
                if (m_connLimiter) {
                    m_connLimiter->release();
                }
            });
        } else {
            // stop() 关闭了监听 socket，accept 返回 EBADF 是预期行为，不打错误日志
            // EAGAIN/EWOULDBLOCK 在非阻塞 socket 上是正常行为
            if (!m_isStop && errno != 0 && errno != EBADF && errno != EAGAIN && errno != EWOULDBLOCK) {
                ERROR(logger) << "accept errno=" << errno << " errstr=" << strerror(errno);
            }
        }
    }
}

void TcpServer::setConf(const TcpServerConf& v) {
    m_conf.reset(new TcpServerConf(v));
    if (v.timeout > 0) {
        m_recvTimeout = static_cast<uint64_t>(v.timeout);
    }
    m_connLimiter = ConnLimiter::CreateFromArgs(v.args);
    DEBUG(logger) << "apply server conf"
        << " name=" << (v.name.empty() ? m_name : v.name)
        << " type=" << v.type
        << " conf_timeout_ms=" << v.timeout
        << " effective_recv_timeout_ms=" << m_recvTimeout;
}

bool TcpServer::loadCertificates(const std::string& cert_file, const std::string& key_file) {
	for (auto& i : m_socks) {
		auto ssl_sock = std::dynamic_pointer_cast<SSLSocket>(i);
		if (ssl_sock) {
			if (!ssl_sock->loadCertificates(cert_file, key_file)) {
				return false;
			}
		}
	}
    return true;
}

std::string TcpServer::toString(const std::string& prefix) {
    std::stringstream ss;
    ss << prefix << "[type=" << m_type
       << " name=" << (m_name.empty() ? "-" : m_name)
       << " worker=" << WorkerName(m_worker)
       << " io=" << WorkerName(m_ioWorker)
       << " accept=" << WorkerName(m_acceptWorker)
       << " recv_timeout=" << m_recvTimeout
       << " ssl=" << (m_ssl ? 1 : 0);
    if (m_connLimiter && m_connLimiter->isLimited()) {
        ss << " conn=" << m_connLimiter->current()
           << "/" << (int64_t)m_connLimiter->getMaxConn()
           << " rejected=" << m_connLimiter->getRejectedCount();
    }
    if (!m_socks.empty()) {
        ss << " listen=[";
        for (size_t i = 0; i < m_socks.size(); ++i) {
            if (i > 0) {
                ss << ",";
            }
            auto addr = m_socks[i]->getLocalAddress();
            ss << (addr ? addr->toString() : "unknown");
        }
        ss << "]";
    }
    ss << "]";
    return ss.str();
}

bool TcpServerFactory::Register(const std::string& type, Creator cb) {
    if (type.empty() || !cb) {
        return false;
    }
    auto& registry = GetServerFactoryRegistry();
    std::lock_guard lock(registry.mutex);
    return registry.creators.emplace(type, std::move(cb)).second;
}

bool TcpServerFactory::Unregister(const std::string& type) {
    auto& registry = GetServerFactoryRegistry();
    std::lock_guard lock(registry.mutex);
    return registry.creators.erase(type) > 0;
}

TcpServer::ptr TcpServerFactory::Create(const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker,
                                        IOManager* accept_worker) {
    auto& registry = GetServerFactoryRegistry();
    std::lock_guard lock(registry.mutex);
    auto it = registry.creators.find(conf.type);
    if (it == registry.creators.end()) {
        return nullptr;
    }
    return it->second(conf, process_worker, io_worker, accept_worker);
}

std::vector<std::string> TcpServerFactory::ListTypes() {
    auto& registry = GetServerFactoryRegistry();
    std::lock_guard lock(registry.mutex);
    std::vector<std::string> types;
    types.reserve(registry.creators.size());
    for (auto& [k, _] : registry.creators) {
        types.push_back(k);
    }
    return types;
}

}
