#include "rpc_client_pool.h"

#include "../iomanager/iomanager.h"
#include "../log/log.h"
#include "../socket/address.h"
#include "../util/util.h" // IWYU pragma: keep

namespace chen::rpc {

static chen::Logger::ptr logger = LOG_NAME("system");

chen::rpc::RpcClient::ptr RpcClientPool::getClient(const std::string& address, uint64_t timeout_ms) {
    std::lock_guard lock(m_mutex);

    ensurePruneTimerLocked();

    const uint64_t nowMs = chen::GetCurrentMs();
    pruneIdleUnlocked(nowMs, m_idleTimeoutMs);

    if (auto it = m_clients.find(address); it != m_clients.end()) {
        if (auto& existing = it->second.client; existing) {
            if (existing->isConnected()) {
                it->second.lastUsedMs = nowMs;
                DEBUG(logger) << "reuse rpc client address=" << address << " pool_size=" << m_clients.size();
                return existing;
            }
            // 连接已失效，关闭并从池中移除，下面重新创建
            TRACE(logger) << "reuse rpc client address=" << address << " but not connected, reconnecting";
            existing->close();
            m_clients.erase(it);
        } else {
            m_clients.erase(it);
        }
    }

    auto addr = chen::Address::LookupAny(address);
    if (!addr) {
        WARN(logger) << "invalid address: " << address;
        return nullptr;
    }

    auto client = std::make_shared<chen::rpc::RpcClient>();
    client->setHeartbeatInterval(m_heartbeatIntervalMs);
    if (!client->connect(addr, timeout_ms)) {
        WARN(logger) << "connect failed: " << address;
        return nullptr;
    }

    m_clients[address] = ClientEntry{client, nowMs};
    TRACE(logger) << "create rpc client address=" << address << " heartbeat_ms=" << m_heartbeatIntervalMs
        << " idle_timeout_ms=" << m_idleTimeoutMs << " pool_size=" << m_clients.size();
    return client;
}

void RpcClientPool::closeClient(const std::string& address) {
    std::lock_guard lock(m_mutex);
    if (auto it = m_clients.find(address); it != m_clients.end()) {
        if (it->second.client) {
            it->second.client->close();
        }
        INFO(logger) << "close rpc client address=" << address;
        m_clients.erase(it);
    }
}

void RpcClientPool::closeAll() {
    std::lock_guard lock(m_mutex);
    for (auto& [_, entry] : m_clients) {
        if (entry.client) {
            entry.client->close();
        }
    }
    if (!m_clients.empty()) {
        INFO(logger) << "close all rpc clients count=" << m_clients.size();
    }
    m_clients.clear();
    if (m_pruneTimer) {
        m_pruneTimer->cancel();
        m_pruneTimer.reset();
    }
}

void RpcClientPool::pruneIdle(uint64_t idle_ms) {
    std::lock_guard lock(m_mutex);
    pruneIdleUnlocked(chen::GetCurrentMs(), idle_ms);
}

void RpcClientPool::pruneIdleUnlocked(uint64_t nowMs, uint64_t idleMs) {
    if (idleMs == 0) {
        return;
    }

    const size_t before = m_clients.size();
    size_t removed = 0;
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        const bool idleTooLong = (nowMs > it->second.lastUsedMs) && (nowMs - it->second.lastUsedMs >= idleMs);
        if (!it->second.client || idleTooLong) {
            const std::string addr = it->first;
            if (it->second.client) {
                // 如果有外部持有（use_count > 1），跳过关闭以避免竞态
                size_t usecnt = it->second.client.use_count();
                if (usecnt > 1) {
                    DEBUG(logger) << "skip prune in-use rpc client address=" << addr << " use_count=" << usecnt;
                    // 延后一次检查，更新 lastUsedMs 以避免频繁尝试删除正在使用的客户端
                    it->second.lastUsedMs = nowMs;
                    ++it;
                    continue;
                }
                it->second.client->close();
            }
            ++removed;
            TRACE(logger) << "prune idle rpc client address=" << addr << " idle_ms=" << (nowMs - it->second.lastUsedMs)
                << " threshold_ms=" << idleMs;
            it = m_clients.erase(it);
            continue;
        }
        ++it;
    }

    if (removed > 0) {
        DEBUG(logger) << "prune idle run before=" << before << " removed=" << removed << " after=" << m_clients.size()
            << " idle_threshold_ms=" << idleMs;
    }
}

void RpcClientPool::ensurePruneTimerLocked() {
    if (m_pruneTimer || m_pruneIntervalMs == 0) {
        return;
    }
    auto* iom = chen::IOManager::GetThis();
    if (!iom) {
        WARN(logger) << "rpc client pool prune timer not started: IOManager::GetThis() is null";
        return;
    }

    m_pruneTimer = iom->addTimer(m_pruneIntervalMs, [this]() {
        this->pruneIdle(this->m_idleTimeoutMs);
    }, true);
    TRACE(logger) << "rpc client pool prune timer started interval_ms=" << m_pruneIntervalMs
        << " idle_timeout_ms=" << m_idleTimeoutMs;
}

} // namespace chen::rpc
