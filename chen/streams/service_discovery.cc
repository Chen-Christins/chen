#include "service_discovery.h"

#include <mutex>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../util/util.h" // IWYU pragma: keep

namespace chen {

ServiceItemInfo::ptr ServiceItemInfo::Create(const std::string& addr, const std::string& data) {
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
	rt->m_id = ((uint64_t)ip_addr << 32) | port;
	rt->m_ip = ip;
	rt->m_port = port;
	rt->m_data = data;
	return rt;
}

std::string ServiceItemInfo::toString() const {
	std::stringstream ss;
	ss << "[ServiceItemInfo id=" << m_id
	   << " ip=" << m_ip
	   << " port=" << m_port
	   << " data=" << m_data
	   << "]";
	return ss.str();
}

void IServiceDiscovery::registerServer(const std::string& domain, const std::string& service
		, const std::string& addr, const std::string& data) {
	std::unique_lock lock(m_mtx);
	m_registerInfos[domain][service][addr] = data;
}

void IServiceDiscovery::queryServer(const std::string& domain, const std::string& service) {
	std::unique_lock lock(m_mtx);
	m_queryInfos[domain].insert(service);
}

void IServiceDiscovery::listServer(std::unordered_map<std::string, std::unordered_map<std::string,
		std::unordered_map<uint64_t, ServiceItemInfo::ptr>>>& infos) {
	std::shared_lock lock(m_mtx);
	infos = m_datas;
}

void IServiceDiscovery::listRegisterServer(std::unordered_map<std::string, std::unordered_map<std::string, 
		std::unordered_map<std::string, std::string>>>& infos) {
	std::shared_lock lock(m_mtx);
	infos = m_registerInfos;
}

void IServiceDiscovery::listQueryServer(std::unordered_map<std::string, std::unordered_set<std::string>>& infos) {
	std::shared_lock lock(m_mtx);
	infos = m_queryInfos;
}

void IServiceDiscovery::setQueryServer(const std::unordered_map<std::string, std::unordered_set<std::string>>& v) {
	std::unique_lock lock(m_mtx);
	m_queryInfos = v;
}

}
