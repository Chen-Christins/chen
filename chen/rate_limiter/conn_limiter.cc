#include "conn_limiter.h"

#include <cstdlib>

namespace chen {

ConnLimiter::ConnLimiter(double qps, uint64_t burst, uint32_t max_conn)
    :m_rateLimiter(qps, burst)
    ,m_maxConn(max_conn) {
}

ConnLimiter::ptr ConnLimiter::CreateFromArgs(const std::map<std::string, std::string>& args) {
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

bool ConnLimiter::tryAcquire() {
    if (!m_rateLimiter.tryAcquire()) {
        std::lock_guard lock(m_mutex);
        ++m_rejected;
        return false;
    }
    std::lock_guard lock(m_mutex);
    if (m_maxConn > 0 && m_current >= m_maxConn) {
        ++m_rejected;
        return false;
    }
    ++m_current;
    return true;
}

void ConnLimiter::release() {
    std::lock_guard lock(m_mutex);
    if (m_current > 0) {
        --m_current;
    }
}

uint32_t ConnLimiter::current() const {
    std::lock_guard lock(m_mutex);
    return m_current;
}

uint64_t ConnLimiter::getRejectedCount() const {
    std::lock_guard lock(m_mutex);
    return m_rejected;
}

uint32_t ConnLimiter::getMaxConn() const {
    return m_maxConn;
}

bool ConnLimiter::isLimited() const {
    std::lock_guard lock(m_mutex);
    return m_rateLimiter.isLimited() || m_maxConn > 0;
}

}
