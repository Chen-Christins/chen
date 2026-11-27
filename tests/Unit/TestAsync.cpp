/**
 * @file TestAsync.cpp
 * @brief async::Detach / Run / RunAll 单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @note 前三组用例不需要调度器（验证退化路径）；fiber 用例自建 IOManager，不监听端口。
 */
#include "../TestHarness.hpp"

#include "src/IOManager/IOManager.hpp"
#include "src/Util/Async.hpp"
#include "src/Util/Future.hpp"
#include "src/Util/TimeUtil.hpp"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using chen::Future;
using chen::IOManager;
using chen::Promise;
using chen::TimeUtil;
using chen::async::Detach;
using chen::async::Run;
using chen::async::RunAll;
using chen::async::TimeoutError;

CHEN_TEST(async, detach_runs_inline_without_scheduler)
{
    std::atomic<int> ran{0};
    Detach("unit-detach", [&ran]() {
        ++ran;
    });
    // 没有调度器时同步执行，调用返回即已完成
    CHEN_CHECK_EQ(ran.load(), 1);
}

CHEN_TEST(async, detach_swallows_exception)
{
    // 分离任务的异常只记日志，不能传播给调用方
    Detach("unit-detach-throw", []() {
        throw std::runtime_error("detached-boom");
    });
    CHEN_CHECK(true);
}

CHEN_TEST(async, run_inline_returns_value)
{
    const int value = Run([]() {
        return 42;
    });
    CHEN_CHECK_EQ(value, 42);

    std::string text = Run([]() {
        return std::string("inline");
    });
    CHEN_CHECK_EQ(text, std::string("inline"));
}

CHEN_TEST(async, run_inline_propagates_exception)
{
    CHEN_CHECK_THROW(Run([]() -> int {
        throw std::runtime_error("run-boom");
    }),
        std::runtime_error);
}

CHEN_TEST(async, fiber_run_and_run_all)
{
    IOManager iom(2, true, "async_unit");
    std::exception_ptr error;
    std::atomic<int> passed{0};

    iom.schedule([&iom, &error, &passed]() {
        try {
            // 1) 调度执行并取回返回值
            {
                const int value = Run([]() {
                    return 7;
                });
                CHEN_CHECK_EQ(value, 7);
                ++passed;
            }

            // 2) 任务异常原样回传
            {
                CHEN_CHECK_THROW(Run([]() -> int {
                    throw std::logic_error("task-boom");
                }),
                    std::logic_error);
                ++passed;
            }

            // 3) 超时抛 TimeoutError（任务本身继续跑完，结果被丢弃）
            {
                const uint64_t start = TimeUtil::GetCurrentMs();
                CHEN_CHECK_THROW(Run(
                                     []() {
                                         usleep(100 * 1000);
                                         return 1;
                                     },
                                     10),
                    TimeoutError);
                CHEN_CHECK(TimeUtil::GetCurrentMs() - start < 90);
                ++passed;
            }

            // 4) RunAll 并行执行全部任务
            {
                std::atomic<int> done{0};
                std::vector<std::function<void()>> tasks;
                for (int i = 0; i < 4; ++i) {
                    tasks.push_back([&done]() {
                        ++done;
                    });
                }
                RunAll(std::move(tasks));
                CHEN_CHECK_EQ(done.load(), 4);
                ++passed;
            }

            // 5) RunAll 等全部结束后重抛第一个异常
            {
                std::atomic<int> done{0};
                std::vector<std::function<void()>> tasks;
                tasks.push_back([&done]() {
                    ++done;
                    throw std::runtime_error("task-1-boom");
                });
                tasks.push_back([&done]() {
                    ++done;
                });
                CHEN_CHECK_THROW(RunAll(std::move(tasks)), std::runtime_error);
                CHEN_CHECK_EQ(done.load(), 2);
                ++passed;
            }
        } catch (...) {
            error = std::current_exception();
        }
    });

    iom.stop();

    if (error) {
        std::rethrow_exception(error);
    }
    CHEN_CHECK_EQ(passed.load(), 5);
}

CHEN_TEST_MAIN()
