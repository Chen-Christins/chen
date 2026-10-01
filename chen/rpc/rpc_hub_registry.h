/**
 * @file rpc_hub_registry.h
 * @brief 中心转发（Hub）服务注册表
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-09-03
 * @copyright GPL-3.0
 */
#pragma once

#include <map>
#include <memory>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "rpc_connection.h"

namespace chen::rpc {

/**
 * @brief 服务注册表（对应旧版 Tunnel 的 CSvrRouteMgr）
 * @details hub 侧维护服务实例与路由索引，服务注册后建立：
 *          - peer_id -> conn                       （DIRECT）
 *          - func_id -> [instance_id -> peer_id]   （GROUPID / BROADCAST）
 *          - (func_id, bind_id) -> peer_id         （BIND_ID）
 */
class RpcHubRegistry {
public:
    typedef std::shared_ptr<RpcHubRegistry> ptr;

    /**
     * @brief 注册/更新服务实例
     * @param peer_id 对端身份（配置分配，按 hub 唯一）
     * @param func_id 功能类型
     * @param instance_id 实例编号
     * @param bind_ids 绑定的业务 ID 列表
     * @param conn 服务连接
     * @return bool 成功与否（peer_id 冲突返回 false）
     */
    bool registerService(uint32_t peer_id, uint32_t func_id, uint32_t instance_id, const std::vector<uint32_t>& bind_ids,
                         RpcConnection::ptr conn);

    /**
     * @brief 按连接注销服务实例（连接断开时调用）
     * @param conn 服务连接
     */
    void unregisterByConn(RpcConnection::ptr conn);

    /**
     * @brief DIRECT：按 peer_id 获取连接
     */
    RpcConnection::ptr getByPeerId(uint32_t peer_id) const;

    /**
     * @brief BIND_ID：按 (func_id, bind_id) 获取连接
     */
    RpcConnection::ptr getByBindId(uint32_t func_id, uint32_t bind_id) const;

    /**
     * @brief GROUPID：按固定/动态拓扑选择实例连接
     * @param func_id 功能类型
     * @param group_id 分片键
     * @param real_random true=动态拓扑（按在线实例数），false=固定拓扑（按最大实例编号）
     * @return RpcConnection::ptr 目标连接，无可用实例返回 nullptr
     */
    RpcConnection::ptr getByGroupId(uint32_t func_id, uint32_t group_id, bool real_random) const;

    /**
     * @brief BROADCAST：获取 func_id 下全部在线实例连接
     */
    std::vector<RpcConnection::ptr> getByFuncId(uint32_t func_id) const;

    /**
     * @brief 获取 func_id 下的在线实例数量
     */
    size_t getInstanceCount(uint32_t func_id) const;

    /**
     * @brief 连接是否已注册
     */
    bool isRegistered(RpcConnection::ptr conn) const;

    /**
     * @brief 获取连接对应的 peer_id
     * @return 未注册返回 0
     */
    uint32_t getPeerIdByConn(RpcConnection::ptr conn) const;

private:
    /**
     * @brief 移除指定 peer_id 的全部索引
     */
    void removePeerLocked(uint32_t peer_id);

private:
    struct ServiceEntry {
        uint32_t peer_id = 0;
        uint32_t func_id = 0;
        uint32_t instance_id = 0;
        std::set<uint32_t> bind_ids;
        RpcConnection::ptr conn;
    };

private:
    /// peer_id -> 服务实例
    std::unordered_map<uint32_t, ServiceEntry> m_peers;
    /// func_id -> [instance_id -> peer_id]
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> m_funcInstances;
    /// (func_id << 32 | bind_id) -> peer_id
    std::unordered_map<uint64_t, uint32_t> m_bindings;
    /// conn -> peer_id
    std::map<RpcConnection::ptr, uint32_t, std::owner_less<RpcConnection::ptr>> m_connToPeer;
    /// 读写锁
    mutable std::shared_mutex m_mutex;
};

} // namespace chen::rpc