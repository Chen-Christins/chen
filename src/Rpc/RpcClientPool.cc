#include "RpcClientPool.hpp"

#include "../IOManager/IOManager.hpp"
#include "../Log/Log.hpp"
#include "../Socket/Address.hpp"
#include "../Util/Util.hpp" // IWYU pragma: keep

namespace chen::rpc {

static chen::Logger::ptr logger = LOG_NAME("system");

chen::rpc::RpcClient::ptr RpcClientPool::getClient(const std::string& address, uint64_t timeout_ms)
{
    // 注意：connect() 内部是非阻塞 connect + Fiber::YieldToHold()，会让出协程。
    // 因此绝对不能持有 mutex_（std::mutex 阻塞整个 OS 线程）去 connect，
    // 否则同一调度器上的 prune 定时器会卡死线程，协程永远无法恢复。
    std::vector<RpcClient::ptr> to_close;
    RpcClient::ptr reused;
    uint64_t heartbeat_ms = 0;
    {
        std::lock_guard lock(mutex_);

        ensurePruneTimerLocked();

        heartbeat_ms = heartbeat_interval_ms_;
        const uint64_t now_ms = TimeUtil::GetCurrentMs();
        pruneIdleUnlocked(now_ms, idle_timeout_ms_, to_close);

        if (auto it = clients_.find(address); it != clients_.end()) {
            if (auto& existing = it->second.client; existing && existing->isConnected()) {
                it->second.last_used_ms = now_ms;
                DEBUG(logger) << "reuse rpc client address=" << address << " pool_size=" << clients_.size();
                reused = existing;
            } else {
                if (existing) {
                    // 连接已失效，从池中移除，锁外关闭后重新创建
                    TRACE(logger) << "reuse rpc client address=" << address << " but not connected, reconnecting";
                    to_close.push_back(existing);
                }
                clients_.erase(it);
            }
        }
    }

    for (auto& c : to_close) {
        if (c) {
            c->close();
        }
    }

    if (reused) {
        return reused;
    }

    auto addr = chen::Address::LookupAny(address);
    if (!addr) {
        WARN(logger) << "invalid address: " << address;
        return nullptr;
    }

    auto client = std::make_shared<chen::rpc::RpcClient>();
    client->setHeartbeatInterval(heartbeat_ms);
    if (!client->connect(addr, timeout_ms)) {
        WARN(logger) << "connect failed: " << address;
        return nullptr;
    }

    RpcClient::ptr result = client;
    bool discard_new = false;
    {
        std::lock_guard lock(mutex_);
        // 并发 getClient 可能已经插入了可用连接，复用已有的，丢弃自己新建的
        if (auto it = clients_.find(address);
            it != clients_.end() && it->second.client && it->second.client->isConnected()) {
            it->second.last_used_ms = TimeUtil::GetCurrentMs();
            result = it->second.client;
            discard_new = true;
        } else {
            clients_[address] = ClientEntry{client, TimeUtil::GetCurrentMs()};
            TRACE(logger) << "create rpc client address=" << address << " heartbeat_ms=" << heartbeat_ms
                          << " idle_timeout_ms=" << idle_timeout_ms_ << " pool_size=" << clients_.size();
        }
    }

    if (discard_new) {
        client->close();
    }
    return result;
}

void RpcClientPool::closeClient(const std::string& address)
{
    RpcClient::ptr client;
    {
        std::lock_guard lock(mutex_);
        if (auto it = clients_.find(address); it != clients_.end()) {
            client = it->second.client;
            clients_.erase(it);
        }
    }
    if (client) {
        // 锁外关闭，避免在池锁内做可能的阻塞操作
        client->close();
        INFO(logger) << "close rpc client address=" << address;
    }
}

void RpcClientPool::closeAll()
{
    std::vector<RpcClient::ptr> clients;
    Timer::ptr timer;
    {
        std::lock_guard lock(mutex_);
        for (auto& [_, entry] : clients_) {
            if (entry.client) {
                clients.push_back(entry.client);
            }
        }
        if (!clients_.empty()) {
            INFO(logger) << "close all rpc clients count=" << clients_.size();
        }
        clients_.clear();
        timer.swap(prune_timer_);
    }
    for (auto& c : clients) {
        if (c) {
            c->close();
        }
    }
    if (timer) {
        timer->cancel();
    }
}

void RpcClientPool::pruneIdle(uint64_t idle_ms)
{
    std::vector<RpcClient::ptr> to_close;
    {
        std::lock_guard lock(mutex_);
        pruneIdleUnlocked(TimeUtil::GetCurrentMs(), idle_ms, to_close);
    }
    for (auto& c : to_close) {
        if (c) {
            c->close();
        }
    }
}

void RpcClientPool::pruneIdleUnlocked(uint64_t now_ms, uint64_t idle_ms, std::vector<RpcClient::ptr>& to_close)
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
                // 锁外统一关闭
                to_close.push_back(it->second.client);
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
