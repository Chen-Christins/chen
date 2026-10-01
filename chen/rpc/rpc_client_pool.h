/**
 * @file rpc_client_pool.h
 * @brief Lightweight RPC client pool for sharing connections by address.
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../timer/timer.h"
#include "../util/singleton.h"
#include "rpc_client.h"

namespace chen::rpc {

class RpcClientPool {
public:
    typedef std::shared_ptr<RpcClientPool> ptr;

    /**
     * @brief Get or create a connected client to the given address (e.g. "127.0.0.1:8089").
     * @param address RPC server address in "host:port" format
     * @param timeout_ms Connection timeout in milliseconds (default 3000ms)
     * @return chen::rpc::RpcClient::ptr 
     */
    chen::rpc::RpcClient::ptr getClient(const std::string& address, uint64_t timeout_ms = 3000);

    /**
     * @brief Close and remove the client for this address if it exists.
     * @param address RPC server address in "host:port" format
     */
    void closeClient(const std::string& address);

    /**
     * @brief Close all managed clients.
     */
    void closeAll();

    /**
     * @brief Close and remove idle clients.
     * @param idle_ms Client idle threshold in milliseconds.
     */
    void pruneIdle(uint64_t idle_ms);

    /**
     * @brief Set heartbeat interval for newly created clients.
     */
    void setHeartbeatIntervalMs(uint64_t ms) { m_heartbeatIntervalMs = ms; }

    /**
     * @brief Set idle timeout for automatic prune in getClient().
     */
    void setIdleTimeoutMs(uint64_t ms) { m_idleTimeoutMs = ms; }

private:
    struct ClientEntry {
        chen::rpc::RpcClient::ptr client;
        uint64_t lastUsedMs = 0;
    };

    void pruneIdleUnlocked(uint64_t nowMs, uint64_t idleMs);
    void ensurePruneTimerLocked();

    // mutex to protect access to m_clients
    std::mutex m_mutex;
    // map of address to strong pointer for long-lived connection reuse
    std::unordered_map<std::string, ClientEntry> m_clients;

    uint64_t m_heartbeatIntervalMs = 30000;
    uint64_t m_idleTimeoutMs = 60000;
    uint64_t m_pruneIntervalMs = 10000;
    chen::Timer::ptr m_pruneTimer;
};

using RpcClientPoolMgr = chen::Singleton<RpcClientPool>;

} // namespace chen::rpc
