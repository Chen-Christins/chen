#include "StaticServiceDiscovery.hpp"

#include <algorithm>
#include <mutex>
#include <set>
#include <sstream>

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<StaticServiceDiscovery::Conf>::ptr g_service_discovery_domains =
    Config::Lookup("service_discovery.domains", StaticServiceDiscovery::Conf(), "static service discovery domains");

/**
 * @brief 判断两组实例是否完全一致（按 id 比较）
 */
static bool SameItems(const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& lhs, const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (auto& [id, info] : lhs) {
        if (rhs.find(id) == rhs.end()) {
            return false;
        }
    }
    return true;
}

StaticServiceDiscovery::StaticServiceDiscovery()
{
}

StaticServiceDiscovery::StaticServiceDiscovery(const Conf& conf)
    : conf_(conf)
{
}

StaticServiceDiscovery::~StaticServiceDiscovery()
{
    if (listener_id_ != 0) {
        g_service_discovery_domains->delListener(listener_id_);
        listener_id_ = 0;
    }
}

StaticServiceDiscovery::ptr StaticServiceDiscovery::CreateFromConfig()
{
    auto conf = g_service_discovery_domains->getValue();
    if (conf.empty()) {
        return nullptr;
    }
    return std::make_shared<StaticServiceDiscovery>(conf);
}

void StaticServiceDiscovery::start()
{
    if (conf_.empty()) {
        auto conf = g_service_discovery_domains->getValue();
        std::unique_lock lock(mtx_);
        if (conf_.empty()) {
            conf_ = std::move(conf);
        }
    }

    {
        std::unique_lock lock(mtx_);
        started_ = true;
    }

    // 只注册一次配置变更监听，实现 yml 热更新（避免在持有 mtx_ 时去拿 ConfigVar 的锁）
    if (listener_id_ == 0) {
        listener_id_ = g_service_discovery_domains->addListener([this](const Conf&, const Conf& now) {
            setConf(now);
        });
    }

    apply(true);
    INFO(logger) << "StaticServiceDiscovery started" << std::endl << statusString();
}

void StaticServiceDiscovery::stop()
{
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<uint64_t, ServiceItemInfo::ptr>>> datas;
    service_callback cb;
    {
        std::unique_lock lock(mtx_);
        started_ = false;
        datas.swap(datas_);
        cb = cb_;
    }

    if (cb) {
        for (auto& [domain, services] : datas) {
            for (auto& [service, items] : services) {
                cb(domain, service, items, {});
            }
        }
    }
    INFO(logger) << "StaticServiceDiscovery stopped";
}

void StaticServiceDiscovery::setConf(const Conf& conf)
{
    {
        std::unique_lock lock(mtx_);
        conf_ = conf;
    }
    if (shouldApply()) {
        apply(true);
    }
}

StaticServiceDiscovery::Conf StaticServiceDiscovery::getConf()
{
    std::shared_lock lock(mtx_);
    return conf_;
}

bool StaticServiceDiscovery::addServer(
    const std::string& domain,
    const std::string& service,
    const std::string& addr,
    const std::string& name,
    uint32_t weight)
{
    if (!ServiceItemInfo::Create(addr, name, weight)) {
        ERROR(logger) << "StaticServiceDiscovery addServer invalid addr=" << addr;
        return false;
    }

    {
        std::unique_lock lock(mtx_);
        register_infos_[domain][service][addr] = name;
        register_weights_[domain][service][addr] = weight;
    }
    if (shouldApply()) {
        apply(true);
    }
    return true;
}

bool StaticServiceDiscovery::delServer(const std::string& domain, const std::string& service, const std::string& addr)
{
    bool found = false;
    {
        std::unique_lock lock(mtx_);

        auto it = register_infos_.find(domain);
        if (it != register_infos_.end()) {
            auto sit = it->second.find(service);
            if (sit != it->second.end()) {
                found = sit->second.erase(addr) > 0;
                if (sit->second.empty()) {
                    it->second.erase(sit);
                }
            }
            if (it->second.empty()) {
                register_infos_.erase(it);
            }
        }

        auto wit = register_weights_.find(domain);
        if (wit != register_weights_.end()) {
            auto sit = wit->second.find(service);
            if (sit != wit->second.end()) {
                sit->second.erase(addr);
                if (sit->second.empty()) {
                    wit->second.erase(sit);
                }
            }
            if (wit->second.empty()) {
                register_weights_.erase(wit);
            }
        }

        auto cit = conf_.find(domain);
        if (cit != conf_.end()) {
            auto scit = cit->second.find(service);
            if (scit != cit->second.end()) {
                auto old_size = scit->second.size();
                std::erase_if(scit->second, [&addr](const ItemConf& item) {
                    return item.address == addr;
                });
                found = found || scit->second.size() != old_size;
                if (scit->second.empty()) {
                    cit->second.erase(scit);
                }
            }
            if (cit->second.empty()) {
                conf_.erase(cit);
            }
        }
    }

    if (found && shouldApply()) {
        apply(true);
    }
    return found;
}

bool StaticServiceDiscovery::unregisterServer(const std::string& domain, const std::string& service, const std::string& addr)
{
    return delServer(domain, service, addr);
}

bool StaticServiceDiscovery::isStarted()
{
    std::shared_lock lock(mtx_);
    return started_;
}

bool StaticServiceDiscovery::shouldApply()
{
    std::shared_lock lock(mtx_);
    return started_;
}

std::string StaticServiceDiscovery::statusString()
{
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<uint64_t, ServiceItemInfo::ptr>>> datas;
    {
        std::shared_lock lock(mtx_);
        datas = datas_;
    }

    std::stringstream ss;
    for (auto& [domain, services] : datas) {
        ss << "domain=" << domain << std::endl;
        for (auto& [service, items] : services) {
            ss << "\tservice=" << service << " count=" << items.size() << std::endl;
            for (auto& [id, info] : items) {
                ss << "\t\t" << info->toString() << " weight=" << info->getWeight() << std::endl;
            }
        }
    }
    return ss.str();
}

void StaticServiceDiscovery::apply(bool notify)
{
    Conf conf;
    service_callback cb;
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<uint64_t, ServiceItemInfo::ptr>>> desired;
    {
        std::shared_lock lock(mtx_);
        conf = conf_;
        cb = cb_;

        // registerServer()/addServer() 补录的实例
        for (auto& [domain, services] : register_infos_) {
            for (auto& [service, addrs] : services) {
                for (auto& [addr, data] : addrs) {
                    uint32_t weight = 10000;
                    auto wit = register_weights_.find(domain);
                    if (wit != register_weights_.end()) {
                        auto sit = wit->second.find(service);
                        if (sit != wit->second.end()) {
                            auto ait = sit->second.find(addr);
                            if (ait != sit->second.end()) {
                                weight = ait->second;
                            }
                        }
                    }
                    auto info = ServiceItemInfo::Create(addr, data, weight);
                    if (info) {
                        desired[domain][service][info->getId()] = info;
                    }
                }
            }
        }
    }

    for (auto& [domain, services] : conf) {
        for (auto& [service, items] : services) {
            for (auto& item : items) {
                auto info = ServiceItemInfo::Create(item.address, item.name, item.weight);
                if (!info) {
                    ERROR(logger) << "StaticServiceDiscovery invalid address, domain=" << domain << " service=" << service << " name=" << item.name
                                  << " address=" << item.address;
                    continue;
                }
                desired[domain][service][info->getId()] = info;
            }
        }
    }

    std::set<std::pair<std::string, std::string>> keys;
    {
        std::shared_lock lock(mtx_);
        for (auto& [domain, services] : datas_) {
            for (auto& [service, items] : services) {
                keys.insert({domain, service});
            }
        }
    }
    for (auto& [domain, services] : desired) {
        for (auto& [service, items] : services) {
            keys.insert({domain, service});
        }
    }

    for (auto& [domain, service] : keys) {
        std::unordered_map<uint64_t, ServiceItemInfo::ptr> old_value;
        std::unordered_map<uint64_t, ServiceItemInfo::ptr> new_value;

        {
            std::unique_lock lock(mtx_);

            auto it = datas_.find(domain);
            if (it != datas_.end()) {
                auto sit = it->second.find(service);
                if (sit != it->second.end()) {
                    old_value = sit->second;
                }
            }

            auto dit = desired.find(domain);
            if (dit != desired.end()) {
                auto sit = dit->second.find(service);
                if (sit != dit->second.end()) {
                    new_value = sit->second;
                }
            }

            if (new_value.empty()) {
                if (it != datas_.end()) {
                    it->second.erase(service);
                    if (it->second.empty()) {
                        datas_.erase(it);
                    }
                }
            } else {
                datas_[domain][service] = new_value;
            }
        }

        if (notify && cb && !SameItems(old_value, new_value)) {
            cb(domain, service, old_value, new_value);
        }
    }
}

} // namespace chen
