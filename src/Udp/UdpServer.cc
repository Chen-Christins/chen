#include "UdpServer.hpp"

#include <sys/socket.h>

#include <mutex>
#include <utility>

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"

namespace chen {

bool UdpServerConf::operator==(const UdpServerConf& oth) const
{
    return address == oth.address
        && timeout == oth.timeout
        && id == oth.id
        && type == oth.type
        && name == oth.name
        && io_worker == oth.io_worker
        && process_worker == oth.process_worker
        && recv_buf_size == oth.recv_buf_size
        && args == oth.args;
}

namespace {

struct UdpServerFactoryRegistry {
    std::map<std::string, UdpServerFactory::Creator> creators;
    std::mutex mutex;
};

UdpServerFactoryRegistry& GetUdpServerFactoryRegistry()
{
    static UdpServerFactoryRegistry instance;
    return instance;
}

} // namespace

static ConfigVar<uint64_t>::ptr g_udp_recv_buf_size = Config::Lookup("udp_server.recv_buf_size", (uint64_t)(64 * 1024), "udp server recv buf size");

static Logger::ptr logger = LOG_NAME("system");

UdpServer::UdpServer(IOManager* worker, IOManager* io_worker)
    : worker_(worker)
    , io_worker_(io_worker)
    , name_("chen/1.0.0")
    , is_stop_(true)
    , recv_buf_size_(g_udp_recv_buf_size->getValue())
{
}

UdpServer::~UdpServer()
{
    for (auto& i : socks_) {
        i->close();
    }
    socks_.clear();
}

void UdpServer::clearRegistrations()
{
}

bool UdpServer::bind(Address::ptr addr)
{
    std::vector<Address::ptr> addrs;
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails);
}

bool UdpServer::bind(const std::vector<Address::ptr>& addrs, std::vector<Address::ptr>& fails)
{
    for (auto& addr : addrs) {
        Socket::ptr sock = Socket::CreateUDP(addr);
        if (!sock->bind(addr)) {
            ERROR(logger) << "bind fail errno=" << errno << " errstr=" << strerror(errno) << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        socks_.push_back(sock);
    }

    if (!fails.empty()) {
        socks_.clear();
        return false;
    }
    for (auto& i : socks_) {
        INFO(logger) << "type=" << type_ << " name=" << name_ << " server bind success: " << *i;
    }
    return true;
}

bool UdpServer::start()
{
    if (!is_stop_) {
        return true;
    }
    is_stop_ = false;
    for (auto& sock : socks_) {
        worker_->schedule(std::bind(&UdpServer::startRecv, shared_from_this(), sock));
    }
    return true;
}

void UdpServer::stop()
{
    is_stop_ = true;
    auto self = shared_from_this();
    worker_->schedule([this, self]() {
        for (auto& sock : socks_) {
            sock->cancelAll();
            sock->close();
        }
        socks_.clear();
    });
}

void UdpServer::startRecv(Socket::ptr sock)
{
    std::vector<char> buffer(recv_buf_size_);
    struct sockaddr_storage addr;
    socklen_t addrlen;
    while (!is_stop_) {
        addrlen = sizeof(addr);
        int n = ::recvfrom(sock->getSocket(), buffer.data(), buffer.size(), 0, (struct sockaddr*)&addr, &addrlen);
        if (n > 0) {
            auto from = Address::Create((sockaddr*)&addr, addrlen);
            std::vector<char> data(buffer.begin(), buffer.begin() + n);
            io_worker_->schedule([this, self = shared_from_this(), sock, data = std::move(data), from]() {
                handleRecv(sock, data.data(), data.size(), from);
            });
        } else if (!is_stop_) {
            ERROR(logger) << "recvfrom errno=" << errno << " errstr=" << strerror(errno);
        }
    }
}

void UdpServer::handleRecv(Socket::ptr sock, const char* data, size_t len, Address::ptr from)
{
    INFO(logger) << "handleRecv from " << from->toString() << " len=" << len;
}

void UdpServer::setConf(const UdpServerConf& v)
{
    conf_.reset(new UdpServerConf(v));
    if (v.recv_buf_size > 0) {
        recv_buf_size_ = v.recv_buf_size;
    }
    if (v.timeout > 0) {
        // UDP doesn't have a per-connection timeout, but set for consistency
    }
    DEBUG(logger) << "apply udp server conf"
                  << " name=" << (v.name.empty() ? name_ : v.name) << " type=" << v.type;
}

std::string UdpServer::toString(const std::string& prefix)
{
    std::stringstream ss;
    ss << prefix << "[type=" << type_ << " name=" << name_ << " worker=" << (worker_ ? worker_->getName() : "")
       << " recv_buf_size=" << recv_buf_size_;

    std::string pre = prefix.empty() ? "    " : prefix;
    for (auto& i : socks_) {
        ss << pre << pre << *i << std::endl;
    }
    return ss.str();
}

bool UdpServerFactory::Register(const std::string& type, Creator cb)
{
    if (type.empty() || !cb) {
        return false;
    }
    auto& registry = GetUdpServerFactoryRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.creators.emplace(type, std::move(cb)).second;
}

bool UdpServerFactory::Unregister(const std::string& type)
{
    auto& registry = GetUdpServerFactoryRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.creators.erase(type) > 0;
}

UdpServer::ptr UdpServerFactory::Create(const UdpServerConf& conf, IOManager* process_worker, IOManager* io_worker)
{
    auto& registry = GetUdpServerFactoryRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto it = registry.creators.find(conf.type);
    if (it == registry.creators.end()) {
        return nullptr;
    }
    return it->second(conf, process_worker, io_worker);
}

std::vector<std::string> UdpServerFactory::ListTypes()
{
    auto& registry = GetUdpServerFactoryRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    std::vector<std::string> types;
    types.reserve(registry.creators.size());
    for (auto& [k, _] : registry.creators) {
        types.push_back(k);
    }
    return types;
}

} // namespace chen
