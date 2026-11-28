/**
 * @file Async.hpp
 * @brief 异步执行辅助：分离任务、带结果/超时/异常的任务、并行 fan-out
 * @author chen
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @details 在 Promise/Future 之上补一层「怎么跑」的样板：
 *          1. async::Detach  —— 丢给调度器就不管了，异常记日志，不会中断调度线程
 *          2. async::Run     —— 调度 + 等待 + 超时 + 异常回传，直接拿到返回值
 *          3. async::RunAll  —— 多个任务并行跑，等齐后重抛第一个异常
 *
 *          三者都在「没有调度器」或「当前线程不能挂起」时退化为同步执行，
 *          因此可以在主线程、模块 onLoad、普通 std::thread 里安全调用。
 */
#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../Fiber/Fiber.hpp"
#include "../Schedule/Schedule.hpp"
#include "Future.hpp"

namespace chen {
namespace async {

/**
 * @class TimeoutError
 * @brief async::Run / async::RunAll 等待超时
 */
class TimeoutError : public std::runtime_error {
public:
    explicit TimeoutError(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

/**
 * @brief 分离执行：把 fn 丢到当前调度器，不等待结果
 * @param name 任务名，仅用于日志定位
 * @param fn 任务体；异常会被捕获并记 ERROR，不会传播到调度线程
 * @note 当前线程没有调度器时直接同步执行；调用方需保证 fn 捕获的对象活得比任务久
 */
void Detach(const std::string& name, std::function<void()> fn);

/**
 * @brief 在调度器上执行 fn 并等待结果
 * @tparam Fn 可调用对象（支持 move-only，返回值可以是 void）
 * @param fn 任务体
 * @param timeout_ms 等待超时毫秒数，0 表示不限时
 * @return fn 的返回值
 * @throw TimeoutError 超时
 * @throw fn 抛出的任意异常（原样重抛）
 * @attention 1. 没有调度器、或在调度器主协程/普通线程里调用时，直接同步执行；
 *               这样既保证总有结果，也避免「单线程池 + 主协程等待」自锁
 *            2. 超时后任务本身不会被取消，仍在后台跑完，结果被丢弃
 */
template <typename Fn>
auto Run(Fn&& fn, uint64_t timeout_ms = 0) -> std::invoke_result_t<std::decay_t<Fn>>
{
    using Result = std::invoke_result_t<std::decay_t<Fn>>;
    auto task = std::make_shared<std::decay_t<Fn>>(std::forward<Fn>(fn));

    Scheduler* scheduler = Scheduler::GetThis();
    if (!scheduler || !Fiber::CanYield()) {
        // 没有可挂起的调度上下文：同步执行，避免等待者永远等不到线程
        return (*task)();
    }

    if constexpr (std::is_void_v<Result>) {
        Promise<void> promise;
        Future<void> future = promise.getFuture();
        scheduler->schedule([task, promise]() mutable {
            try {
                (*task)();
                promise.setValue();
            } catch (...) {
                promise.setException(std::current_exception());
            }
        });
        if (timeout_ms > 0 && !future.waitFor(timeout_ms)) {
            throw TimeoutError("chen::async::Run timeout after " + std::to_string(timeout_ms) + "ms");
        }
        future.get();
    } else {
        Promise<Result> promise;
        Future<Result> future = promise.getFuture();
        scheduler->schedule([task, promise]() mutable {
            try {
                promise.setValue((*task)());
            } catch (...) {
                promise.setException(std::current_exception());
            }
        });
        if (timeout_ms > 0 && !future.waitFor(timeout_ms)) {
            throw TimeoutError("chen::async::Run timeout after " + std::to_string(timeout_ms) + "ms");
        }
        return future.get();
    }
}

/**
 * @brief 并行执行一组任务并等待全部完成
 * @param tasks 任务体集合（结果通过捕获的引用/指针带出，与 WorkerGroup 用法一致）
 * @param timeout_ms 整体等待超时毫秒数，0 表示不限时
 * @throw TimeoutError 整体超时（未完成的任务不会被取消）
 * @throw 任务抛出的第一个异常（等全部任务结束之后再抛）
 * @note 没有调度器或当前线程不能挂起时按顺序同步执行
 */
void RunAll(std::vector<std::function<void()>> tasks, uint64_t timeout_ms = 0);

} // namespace async
} // namespace chen
