#include "rate_limiter.h"

namespace chen {

RateLimiter::RateLimiter(double qps, uint64_t burst)
    :m_qps(qps)
    ,m_burst(burst > 0 ? static_cast<double>(burst) : qps)
    ,m_tokens(m_burst > 0 ? m_burst : 0)
    ,m_lastTime(std::chrono::steady_clock::now()) {
}

bool RateLimiter::tryAcquire() {
    std::lock_guard lock(m_mutex);
    if (m_qps <= 0) {
        return true;
    }
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_lastTime).count();
    m_lastTime = now;
    m_tokens += elapsed * m_qps;
    if (m_tokens > m_burst) {
        m_tokens = m_burst;
    }
    if (m_tokens >= 1.0) {
        m_tokens -= 1.0;
        return true;
    }
    return false;
}

bool RateLimiter::isLimited() const {
    std::lock_guard lock(m_mutex);
    return m_qps > 0;
}

void RateLimiter::setQps(double qps) {
    std::lock_guard lock(m_mutex);
    m_qps = qps;
}

void RateLimiter::setBurst(uint64_t burst) {
    std::lock_guard lock(m_mutex);
    m_burst = static_cast<double>(burst);
    if (m_tokens > m_burst) {
        m_tokens = m_burst;
    }
}

}
