#include "RateLimiter.hpp"

namespace chen {

RateLimiter::RateLimiter(double qps, uint64_t burst)
    : qps_(qps)
    , burst_(burst > 0 ? static_cast<double>(burst) : qps)
    , tokens_(burst_ > 0 ? burst_ : 0)
    , last_time_(std::chrono::steady_clock::now())
{
}

bool RateLimiter::tryAcquire()
{
    std::lock_guard lock(mutex_);
    if (qps_ <= 0) {
        return true;
    }
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_time_).count();
    last_time_ = now;
    tokens_ += elapsed * qps_;
    if (tokens_ > burst_) {
        tokens_ = burst_;
    }
    if (tokens_ >= 1.0) {
        tokens_ -= 1.0;
        return true;
    }
    return false;
}

bool RateLimiter::isLimited() const
{
    std::lock_guard lock(mutex_);
    return qps_ > 0;
}

void RateLimiter::setQps(double qps)
{
    std::lock_guard lock(mutex_);
    qps_ = qps;
}

void RateLimiter::setBurst(uint64_t burst)
{
    std::lock_guard lock(mutex_);
    burst_ = static_cast<double>(burst);
    if (tokens_ > burst_) {
        tokens_ = burst_;
    }
}

} // namespace chen
