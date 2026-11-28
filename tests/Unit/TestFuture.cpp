/**
 * @file TestFuture.cpp
 * @brief Promise/Future 单元测试：线程阻塞路径 + 调度器 fiber 路径
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @note fiber 路径需要一个 IOManager；用例内自建并在结束时 stop，不监听端口。
 */
#include "../TestHarness.hpp"

#include "src/IOManager/IOManager.hpp"
#include "src/Util/Future.hpp"
#include "src/Util/TimeUtil.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using chen::Future;
using chen::IOManager;
using chen::Promise;
using chen::TimeUtil;

CHEN_TEST(future, thread_blocking)
{
    Promise<std::string> promise;
    Future<std::string> future = promise.getFuture();

    std::thread producer([&promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        promise.setValue("from-thread");
    });

    CHEN_CHECK_EQ(future.get(), std::string("from-thread"));
    producer.join();
    // 已完成的 Future 可重复读取
    CHEN_CHECK_EQ(future.get(), std::string("from-thread"));
}

CHEN_TEST(future, thread_timeout)
{
    Promise<int> promise;
    Future<int> future = promise.getFuture();

    const uint64_t start = TimeUtil::GetCurrentMs();
    const bool ok = future.waitFor(100);
    const uint64_t elapsed = TimeUtil::GetCurrentMs() - start;

    CHEN_CHECK(!ok);
    CHEN_CHECK(elapsed >= 90);
    CHEN_CHECK(!future.ready());

    // 超时后仍可继续等待并拿到值
    promise.setValue(7);
    CHEN_CHECK_EQ(future.get(), 7);
}

CHEN_TEST(future, ready_before_get_and_single_completion)
{
    Promise<int> promise;
    Future<int> future = promise.getFuture();

    CHEN_CHECK(future.valid());
    CHEN_CHECK(!future.ready());
    CHEN_CHECK(!promise.satisfied());

    promise.setValue(42);
    CHEN_CHECK(future.ready());
    CHEN_CHECK(promise.satisfied());
    CHEN_CHECK_EQ(future.get(), 42);

    // 重复完成只生效一次
    promise.setValue(7);
    CHEN_CHECK_EQ(future.get(), 42);
}

CHEN_TEST(future, exception_propagation)
{
    Promise<int> promise;
    Future<int> future = promise.getFuture();
    promise.setException(std::make_exception_ptr(std::runtime_error("boom")));
    CHEN_CHECK(future.ready());

    CHEN_CHECK_THROW(future.get(), std::runtime_error);

    Promise<void> bad;
    Future<void> bad_future = bad.getFuture();
    bad.setException(std::make_exception_ptr(std::logic_error("void-boom")));
    CHEN_CHECK_THROW(bad_future.get(), std::logic_error);
}

CHEN_TEST(future, fiber_paths)
{
    IOManager iom(2, true, "future_unit");
    std::exception_ptr error;
    std::atomic<int> passed{0};

    iom.schedule([&iom, &error, &passed]() {
        try {
            // 1) 跨 fiber 等待：另一个任务完成 Promise 后当前 fiber 被唤醒
            {
                Promise<std::string> promise;
                Future<std::string> future = promise.getFuture();
                std::atomic<bool> other_ran{false};
                iom.schedule([&promise, &other_ran]() {
                    other_ran = true;
                    promise.setValue("hello");
                });
                // 不在这里断言 !ready()：setter 被调度到 1 个 worker 线程，可能已经执行
                CHEN_CHECK_EQ(future.get(), std::string("hello"));
                CHEN_CHECK(other_ran.load());
                ++passed;
            }

            // 2) 多个等待者共享同一个 Future
            {
                Promise<int> promise;
                Future<int> future = promise.getFuture();
                Promise<void> done;
                Future<void> done_future = done.getFuture();
                const int n = 3;
                std::atomic<int> count{0};
                std::atomic<int> sum{0};
                for (int i = 0; i < n; ++i) {
                    iom.schedule([future, &count, &sum, &done, i]() {
                        const int v = future.get();
                        sum += v + i;
                        if (++count == 3) {
                            done.setValue();
                        }
                    });
                }
                promise.setValue(10);
                done_future.get();
                CHEN_CHECK_EQ(count.load(), n);
                CHEN_CHECK_EQ(sum.load(), 10 * n + 0 + 1 + 2);
                ++passed;
            }

            // 3) fiber 内超时等待
            {
                Promise<int> promise;
                Future<int> future = promise.getFuture();
                const uint64_t start = TimeUtil::GetCurrentMs();
                const bool ok = future.waitFor(100);
                const uint64_t elapsed = TimeUtil::GetCurrentMs() - start;
                CHEN_CHECK(!ok);
                CHEN_CHECK(elapsed >= 90);
                CHEN_CHECK(!future.ready());
                promise.setValue(9);
                CHEN_CHECK_EQ(future.get(), 9);
                ++passed;
            }

            // 4) 在超时前完成
            {
                Promise<int> promise;
                Future<int> future = promise.getFuture();
                iom.schedule([&promise]() {
                    promise.setValue(5);
                });
                CHEN_CHECK(future.waitFor(5000));
                CHEN_CHECK_EQ(future.get(), 5);
                ++passed;
            }

            // 5) 超时等待者与正常等待者共存
            {
                Promise<int> promise;
                Future<int> future = promise.getFuture();
                Promise<void> done;
                Future<void> done_future = done.getFuture();
                const int n = 3;
                std::atomic<int> timeout_count{0};
                std::atomic<int> value_count{0};
                for (int i = 0; i < n; ++i) {
                    iom.schedule([future, &timeout_count, &value_count, &done, i]() {
                        if (i == 0) {
                            if (!future.waitFor(50)) {
                                ++timeout_count;
                            }
                        } else if (future.get() == 10) {
                            ++value_count;
                        }
                        if (timeout_count.load() + value_count.load() == n) {
                            done.setValue();
                        }
                    });
                }
                iom.addTimer(
                    300,
                    [&promise]() {
                        promise.setValue(10);
                    },
                    false);
                done_future.get();
                CHEN_CHECK_EQ(timeout_count.load(), 1);
                CHEN_CHECK_EQ(value_count.load(), 2);
                ++passed;
            }
        } catch (...) {
            error = std::current_exception();
        }
    });

    // stop() 会执行完已排队的任务（use_caller 模式下由调用线程参与调度）
    iom.stop();

    if (error) {
        std::rethrow_exception(error);
    }
    CHEN_CHECK_EQ(passed.load(), 5);
}

CHEN_TEST(future, promise_dropped_completes_with_exception)
{
    Promise<int> promise;
    Future<int> future = promise.getFuture();
    CHEN_CHECK(!future.ready());

    // 副本生产端销毁时计数从 2 降到 1，状态仍未完成
    {
        Promise<int> extra = promise;
        (void)extra;
    }
    CHEN_CHECK(!future.ready());

    // 最后一个生产端被替换（等价于丢弃），等待者应以异常完成而不是永久挂起
    promise = Promise<int>();
    CHEN_CHECK(future.ready());
    CHEN_CHECK_THROW(future.get(), std::runtime_error);

    Promise<void> void_promise;
    Future<void> void_future = void_promise.getFuture();
    void_promise = Promise<void>();
    CHEN_CHECK_THROW(void_future.get(), std::runtime_error);
}

CHEN_TEST(future, promise_dropped_wakes_fiber_waiter)
{
    IOManager iom(2, true, "future_unit");
    std::exception_ptr error;
    std::atomic<bool> woken{false};

    iom.schedule([&iom, &error, &woken]() {
        try {
            Promise<int> promise;
            Future<int> future = promise.getFuture();
            // 把唯一的生产端交给另一个任务：任务结束即销毁，且从不设值
            iom.schedule([promise = std::move(promise)]() mutable {
                (void)promise;
            });
            CHEN_CHECK_THROW(future.get(), std::runtime_error);
            woken = true;
        } catch (...) {
            error = std::current_exception();
        }
    });

    iom.stop();

    if (error) {
        std::rethrow_exception(error);
    }
    CHEN_CHECK(woken.load());
}

CHEN_TEST_MAIN()
