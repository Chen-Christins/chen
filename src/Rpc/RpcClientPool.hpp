/**
 * @file RpcClientPool.hpp
 * @brief Lightweight RPC client pool for sharing connections by address.
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Timer/Timer.hpp"
#include "../Util/Singleton.hpp"
#include "RpcClient.hpp"

namespace chen::rpc {

class RpcClientPool {
public:
    typedef std::shared_ptr<RpcClientPool> ptr;

    /**
     * @brief Get or create a connected client to the given address (e.g. "127.0.0.1:8089").
     * @note connect() 会挂起当前协程，因此实现中不能在持有 mutex_ 时调用；
     *       本类所有可能阻塞的操作（connect / close）都在锁外执行。
     * @param address RPC server address in "host:port" format
     * @param timeout_ms Connection timeout in milliseconds (default 3000ms)
     * @return chen::rpc::RpcClient::ptr
     */
    RpcClient::ptr getClient(const std::string& address, uint64_t timeout_ms = 3000);

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
    void setHeartbeatIntervalMs(uint64_t ms) { heartbeat_interval_ms_ = ms; }

    /**
     * @brief Set idle timeout for automatic prune in getClient().
     */
    void setIdleTimeoutMs(uint64_t ms) { idle_timeout_ms_ = ms; }

private:
    struct ClientEntry {
        RpcClient::ptr client;
        uint64_t last_used_ms = 0;
    };

    void pruneIdleUnlocked(uint64_t now_ms, uint64_t idle_ms, std::vector<RpcClient::ptr>& to_close);
    void ensurePruneTimerLocked();

    // mutex to protect access to clients_
    // 约束：临界区内不得调用会挂起协程的接口（connect / call / read / write / sleep）
    std::mutex mutex_;
    // map of address to strong pointer for long-lived connection reuse
    std::unordered_map<std::string, ClientEntry> clients_;

    uint64_t heartbeat_interval_ms_ = 30000;
    uint64_t idle_timeout_ms_ = 60000;
    uint64_t prune_interval_ms_ = 10000;
    Timer::ptr prune_timer_;
};

using RpcClientPoolMgr = Singleton<RpcClientPool>;

} // namespace chen::rpc
