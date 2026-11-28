/**
 * @file StallDetector.cc
 * @brief 调度线程 stall 检测
 * @author Christins
 * @date 2026-11-28
 * @copyright GPL-3.0
 */
#include "StallDetector.hpp"

#include "TimeUtil.hpp"
#include "Util.hpp"

#include <vector>

namespace chen {

namespace {

/// 每个线程一个，注册后通过 thread_local 指针访问，心跳路径不加锁
struct StallThreadState {
    std::string name;
    /// 最后一次心跳时间（ms）
    std::atomic<uint64_t> last_ping{0};
    /// 当前执行的协程 id，0 表示没有协程
    std::atomic<uint64_t> fiber_id{0};
    /// 最后一次阻塞操作
    std::atomic<int> op{0};
    /// 阻塞操作对应的 fd
    std::atomic<int> fd{-1};
    /// 是否正阻塞在 epoll_wait（空闲不算 stall）
    std::atomic<bool> idle{false};
    /// 当前这段 stall 是否已经报告过
    std::atomic<bool> reported{false};

    StallThreadState() { last_ping.store(TimeUtil::GetCurrentMs()); }
};

thread_local StallThreadState* t_state = nullptr;

} // namespace

StallDetector::StallDetector() = default;

StallDetector::~StallDetector()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : threads_) {
        delete static_cast<StallThreadState*>(item.second);
    }
    threads_.clear();
}

StallDetector* StallDetector::GetInstance()
{
    static StallDetector instance;
    return &instance;
}

const char* StallDetector::opName(int op)
{
    switch (op) {
    case OP_EPOLL:
        return "epoll_wait";
    case OP_READ:
        return "read";
    case OP_WRITE:
        return "write";
    case OP_CONNECT:
        return "connect";
    case OP_ACCEPT:
        return "accept";
    case OP_SLEEP:
        return "sleep";
    case OP_WAIT_LOCK:
        return "wait-lock";
    case OP_NONE:
    default:
        return "none";
    }
}

void StallDetector::registerThread(const std::string& name)
{
    if (t_state) {
        return;
    }
    StallThreadState* state = new StallThreadState();
    state->name = name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threads_[static_cast<int64_t>(GetThreadId())] = state;
    }
    t_state = state;
}

void StallDetector::unregisterThread()
{
    if (!t_state) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = threads_.find(static_cast<int64_t>(GetThreadId()));
        if (it != threads_.end()) {
            delete static_cast<StallThreadState*>(it->second);
            threads_.erase(it);
        }
    }
    t_state = nullptr;
}

void StallDetector::heartbeat()
{
    if (!t_state) {
        return;
    }
    t_state->last_ping.store(TimeUtil::GetCurrentMs());
    t_state->reported.store(false);
}

void StallDetector::setIdle(bool idle)
{
    if (!t_state) {
        return;
    }
    t_state->idle.store(idle);
    if (!idle) {
        heartbeat();
    }
}

void StallDetector::setFiber(uint64_t fiber_id)
{
    if (t_state) {
        t_state->fiber_id.store(fiber_id);
    }
}

void StallDetector::setOp(int op, int fd)
{
    if (!t_state) {
        return;
    }
    t_state->op.store(op);
    t_state->fd.store(fd);
}

void StallDetector::clearOp()
{
    if (!t_state) {
        return;
    }
    t_state->op.store(OP_NONE);
    t_state->fd.store(-1);
}

std::string StallDetector::check(uint64_t warn_ms)
{
    if (warn_ms == 0) {
        return std::string();
    }

    const uint64_t now = TimeUtil::GetCurrentMs();
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : threads_) {
            StallThreadState* state = static_cast<StallThreadState*>(item.second);
            if (state->idle.load()) {
                state->reported.store(false);
                continue;
            }
            const uint64_t last = state->last_ping.load();
            if (last == 0 || now < last) {
                continue;
            }
            const uint64_t stalled = now - last;
            if (stalled < warn_ms) {
                state->reported.store(false);
                continue;
            }
            if (state->reported.exchange(true)) {
                continue;
            }
            std::string line = "thread '" + state->name + "' (tid=" + std::to_string(item.first)
                + ") has not returned to the event loop for " + std::to_string(stalled)
                + "ms, fiber=" + std::to_string(state->fiber_id.load()) + ", last op=" + opName(state->op.load());
            const int fd = state->fd.load();
            if (fd >= 0) {
                line += "(fd=" + std::to_string(fd) + ")";
            }
            lines.push_back(std::move(line));
        }
    }

    std::string report;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            report += "\n";
        }
        report += lines[i];
    }
    return report;
}

std::string StallDetector::dump() const
{
    const uint64_t now = TimeUtil::GetCurrentMs();
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : threads_) {
            const StallThreadState* state = static_cast<const StallThreadState*>(item.second);
            const uint64_t last = state->last_ping.load();
            const uint64_t age = (last != 0 && now >= last) ? now - last : 0;
            const int fd = state->fd.load();
            std::string line = "thread '" + state->name + "' (tid=" + std::to_string(item.first)
                + ") idle=" + (state->idle.load() ? "true" : "false") + " last_ping=" + std::to_string(age) + "ms ago"
                + " fiber=" + std::to_string(state->fiber_id.load()) + " op=" + opName(state->op.load());
            if (fd >= 0) {
                line += "(fd=" + std::to_string(fd) + ")";
            }
            lines.push_back(std::move(line));
        }
    }

    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            out += "\n";
        }
        out += lines[i];
    }
    return out;
}

size_t StallDetector::threadCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_.size();
}

} // namespace chen
