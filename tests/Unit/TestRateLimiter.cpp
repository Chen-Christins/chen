/**
 * @file TestRateLimiter.cpp
 * @brief 令牌桶限流器 / 连接准入限流器单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "../TestHarness.hpp"

#include "src/RateLimiter/ConnLimiter.hpp"
#include "src/RateLimiter/RateLimiter.hpp"

#include <chrono>
#include <map>
#include <string>
#include <thread>

using chen::ConnLimiter;
using chen::RateLimiter;

CHEN_TEST(rate_limiter, unlimited)
{
    RateLimiter limiter(0, 0);
    CHEN_CHECK(!limiter.isLimited());
    for (int i = 0; i < 1000; ++i) {
        CHEN_CHECK(limiter.tryAcquire());
    }
}

CHEN_TEST(rate_limiter, burst_then_reject)
{
    // qps=100, burst=5：瞬间放行 5 个，第 6 个拒绝（10ms 才补一个令牌，断言不受调度抖动影响）
    RateLimiter limiter(100, 5);
    CHEN_CHECK(limiter.isLimited());
    for (int i = 0; i < 5; ++i) {
        CHEN_CHECK(limiter.tryAcquire());
    }
    CHEN_CHECK(!limiter.tryAcquire());
}

CHEN_TEST(rate_limiter, refill_after_wait)
{
    // qps=100, burst=2：耗尽后等待 50ms 应至少补充 1 个令牌（封顶 2）
    RateLimiter limiter(100, 2);
    CHEN_CHECK(limiter.tryAcquire());
    CHEN_CHECK(limiter.tryAcquire());
    CHEN_CHECK(!limiter.tryAcquire());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHEN_CHECK(limiter.tryAcquire());
}

CHEN_TEST(conn_limiter, max_conn)
{
    // 仅限制并发数：max_conn=3，不限速
    ConnLimiter limiter(0, 0, 3);
    CHEN_CHECK(limiter.isLimited());

    CHEN_CHECK(limiter.tryAcquire());
    CHEN_CHECK(limiter.tryAcquire());
    CHEN_CHECK(limiter.tryAcquire());
    CHEN_CHECK_EQ(limiter.current(), 3u);

    CHEN_CHECK(!limiter.tryAcquire());
    CHEN_CHECK_EQ(limiter.getRejectedCount(), 1u);

    limiter.release();
    CHEN_CHECK_EQ(limiter.current(), 2u);
    CHEN_CHECK(limiter.tryAcquire());
    CHEN_CHECK_EQ(limiter.current(), 3u);
}

CHEN_TEST(conn_limiter, create_from_args)
{
    // 无任何限流参数 -> nullptr
    std::map<std::string, std::string> empty;
    CHEN_CHECK(ConnLimiter::CreateFromArgs(empty) == nullptr);

    // 仅 max_conn
    std::map<std::string, std::string> args1{{"max_conn", "10"}};
    auto l1 = ConnLimiter::CreateFromArgs(args1);
    CHEN_CHECK(l1 != nullptr);
    CHEN_CHECK(l1->isLimited());
    CHEN_CHECK_EQ(l1->getMaxConn(), 10u);
    for (int i = 0; i < 10; ++i) {
        CHEN_CHECK(l1->tryAcquire());
    }
    CHEN_CHECK(!l1->tryAcquire());

    // 仅 accept_qps（burst 缺省 = qps）
    std::map<std::string, std::string> args2{{"accept_qps", "1000000"}};
    auto l2 = ConnLimiter::CreateFromArgs(args2);
    CHEN_CHECK(l2 != nullptr);
    CHEN_CHECK(l2->tryAcquire());
}

CHEN_TEST_MAIN()
