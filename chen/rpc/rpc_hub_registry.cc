#include "rpc_hub_registry.h"

#include <algorithm>

namespace chen::rpc {

bool RpcHubRegistry::registerService(uint32_t peer_id, uint32_t func_id, uint32_t instance_id
    ,const std::vector<uint32_t>& bind_ids, RpcConnection::ptr conn) {
    std::unique_lock lock(m_mutex);

    // 连接重复注册：先注销旧记录，支持重注册更新
    auto old = m_connToPeer.find(conn);
    if (old != m_connToPeer.end()) {
        if (old->second != peer_id) {
            removePeerLocked(old->second);
        }
    }

    // peer_id 已被其它连接占用：拒绝
    auto exist = m_peers.find(peer_id);
    if (exist != m_peers.end() && exist->second.conn != conn) {
        return false;
    }

    ServiceEntry entry;
    entry.peer_id = peer_id;
    entry.func_id = func_id;
    entry.instance_id = instance_id;
    entry.bind_ids = std::set<uint32_t>(bind_ids.begin(), bind_ids.end());
    entry.conn = conn;

    m_peers[peer_id] = entry;
    m_connToPeer[conn] = peer_id;
    m_funcInstances[func_id][instance_id] = peer_id;
    for (auto bind_id : bind_ids) {
        m_bindings[(uint64_t)func_id << 32 | bind_id] = peer_id;
    }
    return true;
}

void RpcHubRegistry::removePeerLocked(uint32_t peer_id) {
    auto it = m_peers.find(peer_id);
    if (it == m_peers.end()) {
        return;
    }
    const ServiceEntry& entry = it->second;
    auto& inst_map = m_funcInstances[entry.func_id];
    inst_map.erase(entry.instance_id);
    if (inst_map.empty()) {
        m_funcInstances.erase(entry.func_id);
    }
    for (auto bind_id : entry.bind_ids) {
        m_bindings.erase((uint64_t)entry.func_id << 32 | bind_id);
    }
    m_connToPeer.erase(entry.conn);
    m_peers.erase(it);
}

void RpcHubRegistry::unregisterByConn(RpcConnection::ptr conn) {
    std::unique_lock lock(m_mutex);
    auto it = m_connToPeer.find(conn);
    if (it == m_connToPeer.end()) {
        return;
    }
    removePeerLocked(it->second);
}

RpcConnection::ptr RpcHubRegistry::getByPeerId(uint32_t peer_id) const {
    std::shared_lock lock(m_mutex);
    auto it = m_peers.find(peer_id);
    if (it == m_peers.end()) {
        return nullptr;
    }
    return it->second.conn;
}

RpcConnection::ptr RpcHubRegistry::getByBindId(uint32_t func_id, uint32_t bind_id) const {
    std::shared_lock lock(m_mutex);
    auto it = m_bindings.find((uint64_t)func_id << 32 | bind_id);
    if (it == m_bindings.end()) {
        return nullptr;
    }
    auto peer_it = m_peers.find(it->second);
    if (peer_it == m_peers.end()) {
        return nullptr;
    }
    return peer_it->second.conn;
}

RpcConnection::ptr RpcHubRegistry::getByGroupId(uint32_t func_id, uint32_t group_id, bool real_random) const {
    std::shared_lock lock(m_mutex);
    auto func_it = m_funcInstances.find(func_id);
    if (func_it == m_funcInstances.end() || func_it->second.empty()) {
        return nullptr;
    }

    if (real_random) {
        // 动态拓扑：按在线实例数取模，定位到在线实例集合中的第 N 个
        size_t count = func_it->second.size();
        size_t pos = 1 + group_id % count;
        std::vector<uint32_t> instance_ids;
        instance_ids.reserve(count);
        for (auto& [instance_id, peer_id] : func_it->second) {
            instance_ids.push_back(instance_id);
        }
        std::sort(instance_ids.begin(), instance_ids.end());
        if (pos > instance_ids.size()) {
            return nullptr;
        }
        uint32_t target_instance = instance_ids[pos - 1];
        auto inst_it = func_it->second.find(target_instance);
        if (inst_it == func_it->second.end()) {
            return nullptr;
        }
        auto peer_it = m_peers.find(inst_it->second);
        if (peer_it == m_peers.end()) {
            return nullptr;
        }
        return peer_it->second.conn;
    }

    // 固定拓扑：InstanceID = 1 + (GroupID % MaxInstanceID)，按实例编号直接定位
    uint32_t max_instance = 0;
    for (auto& [instance_id, peer_id] : func_it->second) {
        if (instance_id > max_instance) {
            max_instance = instance_id;
        }
    }
    if (max_instance == 0) {
        return nullptr;
    }
    uint32_t target_instance = 1 + group_id % max_instance;
    auto inst_it = func_it->second.find(target_instance);
    if (inst_it == func_it->second.end()) {
        return nullptr;
    }
    auto peer_it = m_peers.find(inst_it->second);
    if (peer_it == m_peers.end()) {
        return nullptr;
    }
    return peer_it->second.conn;
}

std::vector<RpcConnection::ptr> RpcHubRegistry::getByFuncId(uint32_t func_id) const {
    std::shared_lock lock(m_mutex);
    std::vector<RpcConnection::ptr> conns;
    auto func_it = m_funcInstances.find(func_id);
    if (func_it == m_funcInstances.end()) {
        return conns;
    }
    for (auto& [instance_id, peer_id] : func_it->second) {
        auto peer_it = m_peers.find(peer_id);
        if (peer_it != m_peers.end()) {
            conns.push_back(peer_it->second.conn);
        }
    }
    return conns;
}

size_t RpcHubRegistry::getInstanceCount(uint32_t func_id) const {
    std::shared_lock lock(m_mutex);
    auto func_it = m_funcInstances.find(func_id);
    if (func_it == m_funcInstances.end()) {
        return 0;
    }
    return func_it->second.size();
}

bool RpcHubRegistry::isRegistered(RpcConnection::ptr conn) const {
    std::shared_lock lock(m_mutex);
    return m_connToPeer.find(conn) != m_connToPeer.end();
}

uint32_t RpcHubRegistry::getPeerIdByConn(RpcConnection::ptr conn) const {
    std::shared_lock lock(m_mutex);
    auto it = m_connToPeer.find(conn);
    if (it == m_connToPeer.end()) {
        return 0;
    }
    return it->second;
}

} // namespace chen::rpc