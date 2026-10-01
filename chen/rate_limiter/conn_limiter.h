/**
 * @file conn_limiter.h
 * @brief 连接准入限流器
 * @author Christins
 * @date 2026-08-24
 */
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "rate_limiter.h"

namespace chen {

/**
 * @brief 连接准入限流器
 * @details 组合令牌桶（速率限制）与并发计数（容量限制），统一提供连接的准入判断。
 *          tryAcquire 通过则占用一个并发名额，连接关闭时需调用 release 归还。
 */
class ConnLimiter {
public:
    typedef std::shared_ptr<ConnLimiter> ptr;

    /**
     * @brief 构造函数
     * @param qps 每秒允许的新连接速率（<= 0 不限速）
     * @param burst 速率突发上限（0 或缺省默认取 qps）
     * @param max_conn 最大并发连接数（<= 0 不限）
     */
    ConnLimiter(double qps = 0, uint64_t burst = 0, uint32_t max_conn = 0);

    /**
     * @brief 从服务器 args 配置创建限流器
     * @param args TcpServerConf::args（accept_qps / accept_burst / max_conn）
     * @return ConnLimiter::ptr 未配置任何限流参数时返回 nullptr
     */
    static ConnLimiter::ptr CreateFromArgs(const std::map<std::string, std::string>& args);

    /**
     * @brief 尝试准入一个连接
     * @return bool 是否放行
     */
    bool tryAcquire();

    /**
     * @brief 归还一个并发名额（连接关闭时调用）
     */
    void release();

    /**
     * @brief 当前占用的并发连接数
     */
    uint32_t current() const;

    /**
     * @brief 被拒绝的连接总数
     */
    uint64_t getRejectedCount() const;

    /**
     * @brief 最大并发连接数（<= 0 不限）
     */
    uint32_t getMaxConn() const;

    /**
     * @brief 是否开启了限流
     */
    bool isLimited() const;

private:
    /// 互斥锁
    mutable std::mutex m_mutex;
    /// 令牌桶速率限制
    RateLimiter m_rateLimiter;
    /// 最大并发连接数（<= 0 不限）
    uint32_t m_maxConn;
    /// 当前并发连接数
    uint32_t m_current = 0;
    /// 被拒绝的连接总数
    uint64_t m_rejected = 0;
};

}
