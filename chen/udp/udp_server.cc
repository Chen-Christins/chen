#include "udp_server.h"

#include <sys/socket.h>

#include <mutex>
#include <utility>

#include "../config/config.h"
#include "../log/log.h"

namespace chen {

namespace {

struct UdpServerFactoryRegistry {
    std::map<std::string, UdpServerFactory::Creator> creators;
    std::mutex mutex;
};

UdpServerFactoryRegistry& GetUdpServerFactoryRegistry() {
    static UdpServerFactoryRegistry instance;
    return instance;
}

}

static ConfigVar<uint64_t>::ptr g_udp_recv_buf_size =
    Config::Lookup("udp_server.recv_buf_size", (uint64_t)(64 * 1024), "udp server recv buf size");

static Logger::ptr logger = LOG_NAME("system");

UdpServer::UdpServer(IOManager* worker, IOManager* io_worker)
    :m_worker(worker)
    ,m_ioWorker(io_worker)
    ,m_name("chen/1.0.0")
    ,m_isStop(true)
    ,m_recvBufSize(g_udp_recv_buf_size->getValue()) {
}

UdpServer::~UdpServer() {
    for (auto& i : m_socks) {
        i->close();
    }
    m_socks.clear();
}

bool UdpServer::bind(Address::ptr addr) {
    std::vector<Address::ptr> addrs;
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails);
}

bool UdpServer::bind(const std::vector<Address::ptr>& addrs, std::vector<Address::ptr>& fails) {
    for (auto& addr : addrs) {
        Socket::ptr sock = Socket::CreateUDP(addr);
        if (!sock->bind(addr)) {
            ERROR(logger) << "bind fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        m_socks.push_back(sock);
    }

    if (!fails.empty()) {
        m_socks.clear();
        return false;
    }
    for (auto& i : m_socks) {
        INFO(logger) << "type=" << m_type << " name=" << m_name
            << " server bind success: " << *i;
    }
    return true;
}

bool UdpServer::start() {
    if (!m_isStop) {
        return true;
    }
    m_isStop = false;
    for (auto& sock : m_socks) {
        m_worker->schedule(std::bind(&UdpServer::startRecv, shared_from_this(), sock));
    }
    return true;
}

void UdpServer::stop() {
    m_isStop = true;
    auto self = shared_from_this();
    m_worker->schedule([this, self]() {
        for (auto& sock : m_socks) {
            sock->cancelAll();
            sock->close();
        }
        m_socks.clear();
    });
}

void UdpServer::startRecv(Socket::ptr sock) {
    std::vector<char> buffer(m_recvBufSize);
    struct sockaddr_storage addr;
    socklen_t addrlen;
    while (!m_isStop) {
        addrlen = sizeof(addr);
        int n = ::recvfrom(sock->getSocket(), buffer.data(), buffer.size(), 0, (struct sockaddr*)&addr, &addrlen);
        if (n > 0) {
            auto from = Address::Create((sockaddr*)&addr, addrlen);
            std::vector<char> data(buffer.begin(), buffer.begin() + n);
            m_ioWorker->schedule([this, self = shared_from_this(), sock, data = std::move(data), from]() {
                handleRecv(sock, data.data(), data.size(), from);
            });
        } else if (!m_isStop) {
            ERROR(logger) << "recvfrom errno=" << errno << " errstr=" << strerror(errno);
        }
    }
}

void UdpServer::handleRecv(Socket::ptr sock, const char* data, size_t len, Address::ptr from) {
    INFO(logger) << "handleRecv from " << from->toString() << " len=" << len;
}

void UdpServer::setConf(const UdpServerConf& v) {
    m_conf.reset(new UdpServerConf(v));
    if (v.recv_buf_size > 0) {
        m_recvBufSize = v.recv_buf_size;
    }
    if (v.timeout > 0) {
        // UDP doesn't have a per-connection timeout, but set for consistency
    }
    DEBUG(logger) << "apply udp server conf"
        << " name=" << (v.name.empty() ? m_name : v.name)
        << " type=" << v.type;
}

std::string UdpServer::toString(const std::string& prefix) {
    std::stringstream ss;
    ss << prefix << "[type=" << m_type
       << " name=" << m_name
       << " worker=" << (m_worker ? m_worker->getName() : "")
       << " recv_buf_size=" << m_recvBufSize;
    std::string pre = prefix.empty() ? "    " : prefix;
    for (auto& i : m_socks) {
        ss << pre << pre << *i << std::endl;
    }
    return ss.str();
}

bool UdpServerFactory::Register(const std::string& type, Creator cb) {
    if (type.empty() || !cb) {
        return false;
    }
    auto& registry = GetUdpServerFactoryRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.creators.emplace(type, std::move(cb)).second;
}

bool UdpServerFactory::Unregister(const std::string& type) {
    auto& registry = GetUdpServerFactoryRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.creators.erase(type) > 0;
}

UdpServer::ptr UdpServerFactory::Create(const UdpServerConf& conf, IOManager* process_worker, IOManager* io_worker) {
    auto& registry = GetUdpServerFactoryRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto it = registry.creators.find(conf.type);
    if (it == registry.creators.end()) {
        return nullptr;
    }
    return it->second(conf, process_worker, io_worker);
}

std::vector<std::string> UdpServerFactory::ListTypes() {
    auto& registry = GetUdpServerFactoryRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    std::vector<std::string> types;
    types.reserve(registry.creators.size());
    for (auto& [k, _] : registry.creators) {
        types.push_back(k);
    }
    return types;
}

}
