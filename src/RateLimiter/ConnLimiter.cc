#include "ConnLimiter.hpp"

#include <cstdlib>

namespace chen {

ConnLimiter::ConnLimiter(double qps, uint64_t burst, uint32_t max_conn)
    : rate_limiter_(qps, burst)
    , max_conn_(max_conn)
{
}

ConnLimiter::ptr ConnLimiter::CreateFromArgs(const std::map<std::string, std::string>& args)
{
    double qps = 0;
    uint64_t burst = 0;
    uint32_t max_conn = 0;
    bool has = false;
    if (auto it = args.find("accept_qps"); it != args.end()) {
        qps = strtod(it->second.c_str(), nullptr);
        has = true;
    }
    if (auto it = args.find("accept_burst"); it != args.end()) {
        burst = strtoull(it->second.c_str(), nullptr, 10);
        has = true;
    }
    if (auto it = args.find("max_conn"); it != args.end()) {
        max_conn = static_cast<uint32_t>(strtoul(it->second.c_str(), nullptr, 10));
        has = true;
    }
    if (!has) {
        return nullptr;
    }
    return std::make_shared<ConnLimiter>(qps, burst, max_conn);
}

bool ConnLimiter::tryAcquire()
{
    if (!rate_limiter_.tryAcquire()) {
        std::lock_guard lock(mutex_);
        ++rejected_;
        return false;
    }
    std::lock_guard lock(mutex_);
    if (max_conn_ > 0 && current_ >= max_conn_) {
        ++rejected_;
        return false;
    }
    ++current_;
    return true;
}

void ConnLimiter::release()
{
    std::lock_guard lock(mutex_);
    if (current_ > 0) {
        --current_;
    }
}

uint32_t ConnLimiter::current() const
{
    std::lock_guard lock(mutex_);
    return current_;
}

uint64_t ConnLimiter::getRejectedCount() const
{
    std::lock_guard lock(mutex_);
    return rejected_;
}

uint32_t ConnLimiter::getMaxConn() const
{
    return max_conn_;
}

bool ConnLimiter::isLimited() const
{
    std::lock_guard lock(mutex_);
    return rate_limiter_.isLimited() || max_conn_ > 0;
}

} // namespace chen
