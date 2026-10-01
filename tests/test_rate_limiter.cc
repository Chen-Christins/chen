/**
 * @file test_rate_limiter.cc
 * @brief 限流器测试（令牌桶 + 连接准入）
 * @author Christins
 * @date 2026-08-24
 */
#include "chen/rate_limiter/conn_limiter.h"
#include "chen/rate_limiter/rate_limiter.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <thread>

void test_rate_limiter_unlimited() {
    std::cout << "=== test_rate_limiter_unlimited ===" << std::endl;

    chen::RateLimiter limiter(0, 0);
    assert(limiter.isLimited() == false);
    for (int i = 0; i < 1000; ++i) {
        assert(limiter.tryAcquire() == true);
    }

    std::cout << "PASS" << std::endl;
}

void test_rate_limiter_burst() {
    std::cout << "=== test_rate_limiter_burst ===" << std::endl;

    // qps=1000, burst=5：瞬间放行 5 个，第 6 个拒绝
    chen::RateLimiter limiter(1000, 5);
    assert(limiter.isLimited() == true);
    for (int i = 0; i < 5; ++i) {
        assert(limiter.tryAcquire() == true);
    }
    assert(limiter.tryAcquire() == false);

    std::cout << "PASS" << std::endl;
}

void test_rate_limiter_refill() {
    std::cout << "=== test_rate_limiter_refill ===" << std::endl;

    // qps=100, burst=2：耗尽后等待 50ms（约补充 5 个，封顶 2）即可再放行
    chen::RateLimiter limiter(100, 2);
    assert(limiter.tryAcquire() == true);
    assert(limiter.tryAcquire() == true);
    assert(limiter.tryAcquire() == false);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(limiter.tryAcquire() == true);

    std::cout << "PASS" << std::endl;
}

void test_conn_limiter_max_conn() {
    std::cout << "=== test_conn_limiter_max_conn ===" << std::endl;

    // 仅限制并发数：max_conn=3，不限速
    chen::ConnLimiter limiter(0, 0, 3);
    assert(limiter.isLimited() == true);

    assert(limiter.tryAcquire() == true);
    assert(limiter.tryAcquire() == true);
    assert(limiter.tryAcquire() == true);
    assert(limiter.current() == 3);

    assert(limiter.tryAcquire() == false);
    assert(limiter.getRejectedCount() == 1);

    limiter.release();
    assert(limiter.current() == 2);
    assert(limiter.tryAcquire() == true);
    assert(limiter.current() == 3);

    std::cout << "PASS" << std::endl;
}

void test_conn_limiter_create_from_args() {
    std::cout << "=== test_conn_limiter_create_from_args ===" << std::endl;

    // 无任何限流参数 -> nullptr
    std::map<std::string, std::string> empty;
    assert(chen::ConnLimiter::CreateFromArgs(empty) == nullptr);

    // 仅 max_conn
    std::map<std::string, std::string> args1{{"max_conn", "10"}};
    auto l1 = chen::ConnLimiter::CreateFromArgs(args1);
    assert(l1 != nullptr);
    assert(l1->isLimited() == true);
    for (int i = 0; i < 10; ++i) {
        assert(l1->tryAcquire() == true);
    }
    assert(l1->tryAcquire() == false);

    // 仅 accept_qps（burst 缺省 = qps）
    std::map<std::string, std::string> args2{{"accept_qps", "1000000"}};
    auto l2 = chen::ConnLimiter::CreateFromArgs(args2);
    assert(l2 != nullptr);
    assert(l2->tryAcquire() == true);

    std::cout << "PASS" << std::endl;
}

int main() {
    test_rate_limiter_unlimited();
    test_rate_limiter_burst();
    test_rate_limiter_refill();
    test_conn_limiter_max_conn();
    test_conn_limiter_create_from_args();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
