#include "RpcClientPool.hpp"

#include "../IOManager/IOManager.hpp"
#include "../Log/Log.hpp"
#include "../Socket/Address.hpp"
#include "../Util/Util.hpp" // IWYU pragma: keep

namespace chen::rpc {

static chen::Logger::ptr logger = LOG_NAME("system");

chen::rpc::RpcClient::ptr RpcClientPool::getClient(const std::string& address, uint64_t timeout_ms)
{
    std::lock_guard lock(mutex_);

    ensurePruneTimerLocked();

    const uint64_t now_ms = TimeUtil::GetCurrentMs();
    pruneIdleUnlocked(now_ms, idle_timeout_ms_);

    if (auto it = clients_.find(address); it != clients_.end()) {
        if (auto& existing = it->second.client; existing) {
            if (existing->isConnected()) {
                it->second.last_used_ms = now_ms;
                DEBUG(logger) << "reuse rpc client address=" << address << " pool_size=" << clients_.size();
                return existing;
            }
            // 连接已失效，关闭并从池中移除，下面重新创建
            TRACE(logger) << "reuse rpc client address=" << address << " but not connected, reconnecting";
            existing->close();
            clients_.erase(it);
        } else {
            clients_.erase(it);
        }
    }

    auto addr = chen::Address::LookupAny(address);
    if (!addr) {
        WARN(logger) << "invalid address: " << address;
        return nullptr;
    }

    auto client = std::make_shared<chen::rpc::RpcClient>();
    client->setHeartbeatInterval(heartbeat_interval_ms_);
    if (!client->connect(addr, timeout_ms)) {
        WARN(logger) << "connect failed: " << address;
        return nullptr;
    }

    clients_[address] = ClientEntry{client, now_ms};
    TRACE(logger) << "create rpc client address=" << address << " heartbeat_ms=" << heartbeat_interval_ms_
                  << " idle_timeout_ms=" << idle_timeout_ms_ << " pool_size=" << clients_.size();
    return client;
}

void RpcClientPool::closeClient(const std::string& address)
{
    std::lock_guard lock(mutex_);
    if (auto it = clients_.find(address); it != clients_.end()) {
        if (it->second.client) {
            it->second.client->close();
        }
        INFO(logger) << "close rpc client address=" << address;
        clients_.erase(it);
    }
}

void RpcClientPool::closeAll()
{
    std::lock_guard lock(mutex_);
    for (auto& [_, entry] : clients_) {
        if (entry.client) {
            entry.client->close();
        }
    }
    if (!clients_.empty()) {
        INFO(logger) << "close all rpc clients count=" << clients_.size();
    }
    clients_.clear();
    if (prune_timer_) {
        prune_timer_->cancel();
        prune_timer_.reset();
    }
}

void RpcClientPool::pruneIdle(uint64_t idle_ms)
{
    std::lock_guard lock(mutex_);
    pruneIdleUnlocked(TimeUtil::GetCurrentMs(), idle_ms);
}

void RpcClientPool::pruneIdleUnlocked(uint64_t now_ms, uint64_t idle_ms)
{
    if (idle_ms == 0) {
        return;
    }

    const size_t before = clients_.size();
    size_t removed = 0;
    for (auto it = clients_.begin(); it != clients_.end();) {
        const bool idle_too_long = (now_ms > it->second.last_used_ms) && (now_ms - it->second.last_used_ms >= idle_ms);
        if (!it->second.client || idle_too_long) {
            const std::string addr = it->first;
            if (it->second.client) {
                // 如果有外部持有（use_count > 1），跳过关闭以避免竞态
                size_t usecnt = it->second.client.use_count();
                if (usecnt > 1) {
                    DEBUG(logger) << "skip prune in-use rpc client address=" << addr << " use_count=" << usecnt;
                    // 延后一次检查，更新 last_used_ms 以避免频繁尝试删除正在使用的客户端
                    it->second.last_used_ms = now_ms;
                    ++it;
                    continue;
                }
                it->second.client->close();
            }
            ++removed;
            TRACE(logger) << "prune idle rpc client address=" << addr
                          << " idle_ms=" << (now_ms - it->second.last_used_ms) << " threshold_ms=" << idle_ms;
            it = clients_.erase(it);
            continue;
        }
        ++it;
    }

    if (removed > 0) {
        DEBUG(logger) << "prune idle run before=" << before << " removed=" << removed << " after=" << clients_.size()
                      << " idle_threshold_ms=" << idle_ms;
    }
}

void RpcClientPool::ensurePruneTimerLocked()
{
    if (prune_timer_ || prune_interval_ms_ == 0) {
        return;
    }
    auto* iom = chen::IOManager::GetThis();
    if (!iom) {
        WARN(logger) << "rpc client pool prune timer not started: IOManager::GetThis() is null";
        return;
    }

    prune_timer_ = iom->addTimer(
        prune_interval_ms_,
        [this]() {
            this->pruneIdle(this->idle_timeout_ms_);
        },
        true);
    TRACE(logger) << "rpc client pool prune timer started interval_ms=" << prune_interval_ms_
                  << " idle_timeout_ms=" << idle_timeout_ms_;
}

} // namespace chen::rpc
