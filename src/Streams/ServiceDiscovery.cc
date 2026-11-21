#include "ServiceDiscovery.hpp"

#include <mutex>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../Util/Util.hpp" // IWYU pragma: keep

namespace chen {

IServiceDiscovery::~IServiceDiscovery()
{
}

ServiceItemInfo::ptr ServiceItemInfo::Create(const std::string& addr, const std::string& data, uint32_t weight)
{
    std::size_t pos = addr.find(":");
    if (pos == std::string::npos) {
        return nullptr;
    }
    std::string ip = addr.substr(0, pos);
    uint64_t port = TypeUtil::Atoi(addr.substr(pos + 1));
    in_addr_t ip_addr = inet_addr(ip.c_str());
    if (ip_addr == 0) {
        return nullptr;
    }

    /**
     * 创建TCP连接标识
     * +-----------------------------------+-----------------------------------+
     * |         32位IP地址 (高32位)         |        32位端口号 (低32位)          |
     * +-----------------------------------+-----------------------------------+
     */
    ServiceItemInfo::ptr rt(new ServiceItemInfo);
    rt->id_ = ((uint64_t)ip_addr << 32) | port;
    rt->ip_ = ip;
    rt->port_ = port;
    rt->data_ = data;
    rt->weight_ = weight == 0 ? 10000 : weight;
    return rt;
}

std::string ServiceItemInfo::toString() const
{
    std::stringstream ss;
    ss << "[ServiceItemInfo id=" << id_ << " ip=" << ip_ << " port=" << port_ << " weight=" << weight_
       << " data=" << data_ << "]";
    return ss.str();
}

void IServiceDiscovery::registerServer(const std::string& domain, const std::string& service, const std::string& addr, const std::string& data)
{
    std::unique_lock lock(mtx_);
    register_infos_[domain][service][addr] = data;
}

bool IServiceDiscovery::unregisterServer(const std::string& domain, const std::string& service, const std::string& addr)
{
    std::unique_lock lock(mtx_);
    auto it = register_infos_.find(domain);
    if (it == register_infos_.end()) {
        return false;
    }
    auto sit = it->second.find(service);
    if (sit == it->second.end()) {
        return false;
    }
    bool erased = sit->second.erase(addr) > 0;
    if (sit->second.empty()) {
        it->second.erase(sit);
    }
    if (it->second.empty()) {
        register_infos_.erase(it);
    }
    return erased;
}

void IServiceDiscovery::queryServer(const std::string& domain, const std::string& service)
{
    std::unique_lock lock(mtx_);
    query_infos_[domain].insert(service);
}

void IServiceDiscovery::listServer(
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<uint64_t, ServiceItemInfo::ptr>>>& infos)
{
    std::shared_lock lock(mtx_);
    infos = datas_;
}

void IServiceDiscovery::listRegisterServer(
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, std::string>>>& infos)
{
    std::shared_lock lock(mtx_);
    infos = register_infos_;
}

void IServiceDiscovery::listQueryServer(std::unordered_map<std::string, std::unordered_set<std::string>>& infos)
{
    std::shared_lock lock(mtx_);
    infos = query_infos_;
}

void IServiceDiscovery::setQueryServer(const std::unordered_map<std::string, std::unordered_set<std::string>>& v)
{
    std::unique_lock lock(mtx_);
    query_infos_ = v;
}

} // namespace chen
