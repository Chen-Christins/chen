/**
 * @file test_future.cc
 * @brief Promise/Future 测试：fiber 挂起等待、线程阻塞等待、超时、异常、多等待者
 * @author chen
 * @date 2026-10-25
 * @copyright GPL-3.0
 */
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/util/future.h"
#include "chen/util/time_util.h"

static chen::Logger::ptr logger = LOG_ROOT();
static std::atomic<int> g_failed{0};

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            ++g_failed;                                                            \
            ERROR(logger) << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond; \
        }                                                                          \
    } while (0)

// ========== 普通线程路径（无调度器） ==========

void test_thread_blocking()
{
    INFO(logger) << "=== test_thread_blocking ===";
    chen::Promise<std::string> promise;
    chen::Future<std::string> future = promise.getFuture();

    std::thread producer([&promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        promise.setValue("from-thread");
    });

    const std::string value = future.get();
    producer.join();
    CHECK(value == "from-thread");
    CHECK(future.get() == "from-thread");
}

void test_thread_timeout()
{
    INFO(logger) << "=== test_thread_timeout ===";
    chen::Promise<int> promise;
    chen::Future<int> future = promise.getFuture();

    const uint64_t start = chen::TimeUtil::GetCurrentMs();
    const bool ok = future.waitFor(100);
    const uint64_t elapsed = chen::TimeUtil::GetCurrentMs() - start;
    CHECK(!ok);
    CHECK(elapsed >= 90);
    CHECK(!future.ready());

    promise.setValue(7);
    CHECK(future.get() == 7);
}

// ========== fiber 路径（在调度器任务协程里） ==========

void test_ready_before_get()
{
    INFO(logger) << "=== test_ready_before_get ===";
    chen::Promise<int> promise;
    chen::Future<int> future = promise.getFuture();

    CHECK(future.valid());
    CHECK(!future.ready());
    CHECK(!promise.satisfied());

    promise.setValue(42);
    CHECK(future.ready());
    CHECK(promise.satisfied());
    CHECK(future.get() == 42);
    CHECK(future.get() == 42);

    // 重复完成只生效一次
    promise.setValue(7);
    CHECK(future.get() == 42);
}

void test_wait_across_fibers()
{
    INFO(logger) << "=== test_wait_across_fibers ===";
    chen::Promise<std::string> promise;
    chen::Future<std::string> future = promise.getFuture();

    std::atomic<bool> other_ran{false};
    chen::IOManager::GetThis()->schedule([&promise, &other_ran]() {
        other_ran = true;
        promise.setValue("hello");
    });

    CHECK(!future.ready());
    // 当前协程挂起，让出线程给上面的任务
    const std::string value = future.get();
    CHECK(value == "hello");
    CHECK(other_ran.load());
}

void test_multi_waiters()
{
    INFO(logger) << "=== test_multi_waiters ===";
    chen::Promise<int> promise;
    chen::Future<int> future = promise.getFuture();
    chen::Promise<void> done;
    chen::Future<void> done_future = done.getFuture();

    const int n = 3;
    std::atomic<int> count{0};
    std::atomic<int> sum{0};
    for (int i = 0; i < n; ++i) {
        chen::IOManager::GetThis()->schedule([future, &count, &sum, &done, i]() {
            const int v = future.get();
            sum += v + i;
            if (++count == 3) {
                done.setValue();
            }
        });
    }

    promise.setValue(10);
    done_future.get();
    CHECK(count.load() == n);
    CHECK(sum.load() == 10 * n + 0 + 1 + 2);
}

void test_exception()
{
    INFO(logger) << "=== test_exception ===";
    chen::Promise<int> promise;
    chen::Future<int> future = promise.getFuture();

    promise.setException(std::make_exception_ptr(std::runtime_error("boom")));
    CHECK(future.ready());

    bool caught = false;
    try {
        (void)future.get();
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()) == "boom";
    }
    CHECK(caught);
}

void test_void()
{
    INFO(logger) << "=== test_void ===";
    chen::Promise<void> promise;
    chen::Future<void> future = promise.getFuture();

    CHECK(!future.ready());
    chen::IOManager::GetThis()->schedule([&promise]() {
        promise.setValue();
    });
    future.get();
    CHECK(future.ready());

    // void 的异常路径
    chen::Promise<void> bad;
    chen::Future<void> bad_future = bad.getFuture();
    bad.setException(std::make_exception_ptr(std::logic_error("void-boom")));
    bool caught = false;
    try {
        bad_future.get();
    } catch (const std::logic_error& e) {
        caught = std::string(e.what()) == "void-boom";
    }
    CHECK(caught);
}

void test_wait_timeout_in_fiber()
{
    INFO(logger) << "=== test_wait_timeout_in_fiber ===";
    chen::Promise<int> promise;
    chen::Future<int> future = promise.getFuture();

    const uint64_t start = chen::TimeUtil::GetCurrentMs();
    const bool ok = future.waitFor(100);
    const uint64_t elapsed = chen::TimeUtil::GetCurrentMs() - start;
    CHECK(!ok);
    CHECK(elapsed >= 90);
    CHECK(!future.ready());

    // 超时后协程应能继续正常使用该 Future
    promise.setValue(9);
    CHECK(future.get() == 9);
}

void test_wait_within_deadline()
{
    INFO(logger) << "=== test_wait_within_deadline ===";
    chen::Promise<int> promise;
    chen::Future<int> future = promise.getFuture();

    chen::IOManager::GetThis()->schedule([&promise]() {
        promise.setValue(5);
    });

    CHECK(future.waitFor(5000));
    CHECK(future.get() == 5);
}

void test_multi_timeout_waiters()
{
    INFO(logger) << "=== test_multi_timeout_waiters ===";
    chen::Promise<int> promise;
    chen::Future<int> future = promise.getFuture();
    chen::Promise<void> done;
    chen::Future<void> done_future = done.getFuture();

    const int n = 3;
    std::atomic<int> timeout_count{0};
    std::atomic<int> value_count{0};
    for (int i = 0; i < n; ++i) {
        chen::IOManager::GetThis()->schedule([future, &timeout_count, &value_count, &done, i]() {
            if (i == 0) {
                // 第 0 个等待者超时退出，其余等待者继续等待
                if (!future.waitFor(50)) {
                    ++timeout_count;
                }
            } else {
                if (future.get() == 10) {
                    ++value_count;
                }
            }
            if (timeout_count.load() + value_count.load() == n) {
                done.setValue();
            }
        });
    }

    chen::IOManager::GetThis()->addTimer(
        300,
        [&promise]() {
            promise.setValue(10);
        },
        false);

    done_future.get();
    CHECK(timeout_count.load() == 1);
    CHECK(value_count.load() == 2);
}

void run_tests()
{
    test_ready_before_get();
    test_wait_across_fibers();
    test_multi_waiters();
    test_exception();
    test_void();
    test_wait_timeout_in_fiber();
    test_wait_within_deadline();
    test_multi_timeout_waiters();
}

int main(int argc, char** argv)
{
    logger->setLevel(chen::LogLevel::TRACE);

    // 必须在创建调度器之前：此时 Scheduler::GetThis() 为空，走线程阻塞路径
    test_thread_blocking();
    test_thread_timeout();

    chen::IOManager iom(2, true, "future_test");
    iom.schedule(run_tests);
    iom.stop();

    if (g_failed.load() == 0) {
        std::cout << "test_future PASS" << std::endl;
        return 0;
    }
    std::cout << "test_future FAILED count=" << g_failed.load() << std::endl;
    return 1;
}
