/**
 * @file ServiceDiscovery.hpp
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

    uint64_t getId() const { return id_; }

    uint16_t getPort() const { return port_; }

    const std::string& getIp() const { return ip_; }

    const std::string& getData() const { return data_; }

    uint32_t getWeight() const { return weight_; }

    void setWeight(uint32_t v) { weight_ = v; }

    std::string toString() const;

public:
    uint64_t id_;
    uint16_t port_;
    std::string ip_;
    std::string data_;
    uint32_t weight_ = 10000;
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

    service_callback getServiceCallback() const { return cb_; }

    void setServiceCallback(service_callback v) { cb_ = v; }

    void setQueryServer(const std::unordered_map<std::string, std::unordered_set<std::string>>& v);

protected:
    /// 互斥量
    std::shared_mutex mtx_;
    /// domain -> [service -> [id -> ServiceInfo] ]
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<uint64_t, ServiceItemInfo::ptr>>>
        datas_;
    /// domain -> [service -> [ip:port -> data]]
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, std::string>>>
        register_infos_;
    /// domain -> [service]
    std::unordered_map<std::string, std::unordered_set<std::string>> query_infos_;
    /// cbs
    service_callback cb_;
};

} // namespace chen
