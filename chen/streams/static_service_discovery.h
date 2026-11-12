/**
 * @file static_service_discovery.h
 * @brief 基于配置文件的静态服务发现
 * @author Christins
 * @date 2026-11-13
 * @copyright GPL-3.0
 */
#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "../config/config.h"
#include "../util/string_util.h"
#include "service_discovery.h"

namespace chen {

/**
 * @brief 静态服务发现
 * @details 从配置 service_discovery.domains.<domain>.<service> 读取服务实例，格式：
 *
 * @code{.yaml}
 * service_discovery:
 *   domains:
 *     astartes:
 *       rock:
 *         - name: "rock-1"
 *           address: "127.0.0.1:9001"
 *           weight: 100
 * @endcode
 *
 * start() 时发布给订阅者（如 SDLoadBalance），并监听配置变更实现热更新；
 * 也支持运行时 addServer/delServer 增量更新。
 * 注意：ServiceItemInfo 目前只支持 IPv4 地址。
 */
class StaticServiceDiscovery : public IServiceDiscovery {
public:
    typedef std::shared_ptr<StaticServiceDiscovery> ptr;

    /**
     * @brief 单个实例的配置
     */
    struct ItemConf {
        /// 实例别名（仅用于标识/日志，会写入 ServiceItemInfo::m_data）
        std::string name;
        /// 地址 ip:port
        std::string address;
        /// 权重，0 表示使用默认值 10000
        uint32_t weight = 10000;

        bool operator==(const ItemConf& rhs) const { return name == rhs.name && address == rhs.address && weight == rhs.weight; }

        bool operator!=(const ItemConf& rhs) const { return !(*this == rhs); }
    };

    /// domain -> [ service -> [ items ] ]
    typedef std::unordered_map<std::string, std::unordered_map<std::string, std::vector<ItemConf>>> Conf;

    StaticServiceDiscovery();
    explicit StaticServiceDiscovery(const Conf& conf);
    ~StaticServiceDiscovery() override;

    /**
     * @brief 根据 service_discovery.domains 配置创建实例
     * @return 配置为空时返回 nullptr
     */
    static StaticServiceDiscovery::ptr CreateFromConfig();

    /**
     * @brief 开始服务发现，构建实例列表并通知订阅者
     * @details m_conf 为空时会读取 service_discovery.domains 配置；同时注册配置变更监听
     */
    virtual void start() override;

    /**
     * @brief 停止服务发现，清空已发布的实例
     */
    virtual void stop() override;

    /**
     * @brief 设置静态实例配置
     * @details 已 start 时会立即重建并通知订阅者
     */
    void setConf(const Conf& conf);

    /**
     * @brief 获取当前静态实例配置
     */
    Conf getConf();

    /**
     * @brief 运行时添加一个实例并立即通知订阅者
     * @param[in] domain 服务域名
     * @param[in] service 服务名称
     * @param[in] addr ip:port
     * @param[in] name 实例别名（可选）
     * @param[in] weight 权重，0 表示使用默认值 10000
     * @return bool 地址非法时返回 false
     */
    bool addServer(
        const std::string& domain,
        const std::string& service,
        const std::string& addr,
        const std::string& name = "",
        uint32_t weight = 10000);

    /**
     * @brief 运行时删除一个实例并立即通知订阅者
     * @return bool 实例不存在时返回 false
     */
    bool delServer(const std::string& domain, const std::string& service, const std::string& addr);

    /**
     * @brief 注销实例（IServiceDiscovery 接口）
     * @details 等价于 delServer，会通知订阅者
     */
    virtual bool unregisterServer(const std::string& domain, const std::string& service, const std::string& addr) override;

    /**
     * @brief 是否已经 start
     */
    bool isStarted();

    /**
     * @brief 当前已发布实例的可读描述
     */
    std::string statusString();

private:
    /**
     * @brief 根据 m_conf + m_registerInfos 重建实例列表并发布
     * @param[in] notify 是否通知订阅者
     */
    void apply(bool notify);

    /**
     * @brief 是否需要发布（started 门控）
     */
    bool shouldApply();

private:
    Conf m_conf;
    /// 运行时 addServer 指定的权重：domain -> [ service -> [ addr -> weight ] ]
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, uint32_t>>> m_registerWeights;
    /// 配置变更监听 id
    uint64_t m_listenerId = 0;
    bool m_started = false;
};

/**
 * @brief ItemConf 的 YAML 解析/序列化
 */
template <>
class LexicalCast<std::string, StaticServiceDiscovery::ItemConf> {
public:
    StaticServiceDiscovery::ItemConf operator()(const std::string& v)
    {
        // YAML 标量可能带引号，Trim 的字符集合同时覆盖空白和引号
        static const std::string s_delimit = " \t\r\n\"'";

        StaticServiceDiscovery::ItemConf item;
        YAML::Node node = YAML::Load(v);
        std::stringstream ss;
        if (node["name"]) {
            ss << node["name"];
            item.name = StringUtil::Trim(ss.str(), s_delimit);
        }
        if (node["address"]) {
            ss.str("");
            ss << node["address"];
            item.address = StringUtil::Trim(ss.str(), s_delimit);
        }
        if (node["weight"]) {
            ss.str("");
            ss << node["weight"];
            item.weight = LexicalCast<std::string, uint32_t>()(StringUtil::Trim(ss.str(), s_delimit));
        }
        return item;
    }
};

template <>
class LexicalCast<StaticServiceDiscovery::ItemConf, std::string> {
public:
    std::string operator()(const StaticServiceDiscovery::ItemConf& v)
    {
        YAML::Node node;
        node["name"] = v.name;
        node["address"] = v.address;
        node["weight"] = v.weight;
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

} // namespace chen
