/**
 * @file rate_limiter.h
 * @brief 令牌桶限流器
 * @author Christins
 * @date 2026-08-24
 */
#pragma once

#include <chrono>
#include <memory>
#include <mutex>

namespace chen {

/**
 * @brief 令牌桶限流器
 * @details 以 qps 速率持续补充令牌，桶容量为 burst；tryAcquire 时若存在令牌则消费一个并放行。
 *          qps <= 0 表示不限流，tryAcquire 恒返回 true。纯算法原语，不感知业务语义。
 */
class RateLimiter {
public:
    typedef std::shared_ptr<RateLimiter> ptr;

    /**
     * @brief 构造函数
     * @param qps 令牌补充速率（个/秒），<= 0 表示不限流
     * @param burst 桶容量（瞬时可放行的最大突发量），0 或缺省时默认取 qps
     */
    RateLimiter(double qps = 0, uint64_t burst = 0);

    /**
     * @brief 尝试获取一个令牌
     * @return bool 是否放行
     */
    bool tryAcquire();

    /**
     * @brief 是否开启了限流
     */
    bool isLimited() const;

    /**
     * @brief 动态设置令牌补充速率
     * @param qps 令牌补充速率（个/秒）
     */
    void setQps(double qps);

    /**
     * @brief 动态设置桶容量
     * @param burst 桶容量
     */
    void setBurst(uint64_t burst);

private:
    /// 互斥锁
    mutable std::mutex m_mutex;
    /// 令牌补充速率（个/秒）
    double m_qps;
    /// 桶容量
    double m_burst;
    /// 当前令牌数
    double m_tokens;
    /// 上次补充令牌的时间点
    std::chrono::steady_clock::time_point m_lastTime;
};

}
