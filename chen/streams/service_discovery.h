/**
 * @file service_discovery.h
 * @brief 服务发现接口
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

namespace chen {

class ServiceItemInfo {
public:
    typedef std::shared_ptr<ServiceItemInfo> ptr;

    static ServiceItemInfo::ptr Create(const std::string& addr, const std::string& data, uint32_t weight = 10000);

    uint64_t getId() const { return m_id; }

    uint16_t getPort() const { return m_port; }

    const std::string& getIp() const { return m_ip; }

    const std::string& getData() const { return m_data; }

    uint32_t getWeight() const { return m_weight; }

    void setWeight(uint32_t v) { m_weight = v; }

    std::string toString() const;

public:
    uint64_t m_id;
    uint16_t m_port;
    std::string m_ip;
    std::string m_data;
    uint32_t m_weight = 10000;
};

class IServiceDiscovery {
public:
    typedef std::shared_ptr<IServiceDiscovery> ptr;
    typedef std::function<void(
        const std::string& domain,
        const std::string& service,
        const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& old_value,
        const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& new_value)>
        service_callback;

    virtual ~IServiceDiscovery();

    void registerServer(const std::string& domain, const std::string& service, const std::string& addr, const std::string& data);

    /**
     * @brief 注销本进程注册的服务实例
     * @details 具体实现可在重写时同步通知订阅者
     * @return bool 是否找到并删除
     */
    virtual bool unregisterServer(const std::string& domain, const std::string& service, const std::string& addr);

    void queryServer(const std::string& domain, const std::string& service);

    void listServer(std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<uint64_t, ServiceItemInfo::ptr>>>& infos);

    void listRegisterServer(std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, std::string>>>& infos);

    void listQueryServer(std::unordered_map<std::string, std::unordered_set<std::string>>& infos);

    virtual void start() = 0;

    virtual void stop() = 0;

    service_callback getServiceCallback() const { return m_cb; }

    void setServiceCallback(service_callback v) { m_cb = v; }

    void setQueryServer(const std::unordered_map<std::string, std::unordered_set<std::string>>& v);

protected:
    /// 互斥量
    std::shared_mutex m_mtx;
    /// domain -> [service -> [id -> ServiceInfo] ]
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<uint64_t, ServiceItemInfo::ptr>>> m_datas;
    /// domain -> [service -> [ip:port -> data]]
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> m_registerInfos;
    /// domain -> [service]
    std::unordered_map<std::string, std::unordered_set<std::string>> m_queryInfos;
    /// cbs
    service_callback m_cb;
};

} // namespace chen
