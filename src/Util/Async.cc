/**
 * @file Async.cc
 * @brief 异步执行辅助的非模板实现
 * @author chen
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "Async.hpp"

#include <memory>
#include <mutex>
#include <utility>

#include "../Log/Log.hpp"
#include "TimeUtil.hpp"

namespace chen {
namespace async {

static Logger::ptr g_logger = LOG_NAME("system");

namespace {

/**
 * @brief 执行任务体并吞掉异常（分离任务专用）
 */
void RunSafely(const std::string& name, const std::function<void()>& fn)
{
    try {
        fn();
    } catch (const std::exception& e) {
        ERROR(g_logger) << "async task '" << name << "' failed: " << e.what();
    } catch (...) {
        ERROR(g_logger) << "async task '" << name << "' failed: unknown exception";
    }
}

} // namespace

void Detach(const std::string& name, std::function<void()> fn)
{
    if (!fn) {
        return;
    }
    Scheduler* scheduler = Scheduler::GetThis();
    if (!scheduler) {
        RunSafely(name, fn);
        return;
    }
    scheduler->schedule([name, fn = std::move(fn)]() mutable {
        RunSafely(name, fn);
    });
}

void RunAll(std::vector<std::function<void()>> tasks, uint64_t timeout_ms)
{
    if (tasks.empty()) {
        return;
    }

    Scheduler* scheduler = Scheduler::GetThis();
    if (!scheduler || !Fiber::CanYield()) {
        for (auto& task : tasks) {
            task();
        }
        return;
    }

    // 任务可能在超时后继续运行，错误容器必须与任务同生命周期，故用 shared_ptr 按值捕获
    auto errors = std::make_shared<std::vector<std::exception_ptr>>();
    auto errors_mutex = std::make_shared<std::mutex>();
    std::vector<Future<void>> futures;
    futures.reserve(tasks.size());

    for (auto& task : tasks) {
        Promise<void> promise;
        futures.push_back(promise.getFuture());
        scheduler->schedule([promise, task = std::move(task), errors, errors_mutex]() mutable {
            try {
                task();
            } catch (...) {
                std::lock_guard lock(*errors_mutex);
                errors->push_back(std::current_exception());
            }
            promise.setValue();
        });
    }

    const uint64_t deadline = timeout_ms > 0 ? TimeUtil::GetCurrentMs() + timeout_ms : 0;
    for (auto& future : futures) {
        if (timeout_ms == 0) {
            future.wait();
            continue;
        }
        const uint64_t now = TimeUtil::GetCurrentMs();
        if (now >= deadline || !future.waitFor(deadline - now)) {
            throw TimeoutError("chen::async::RunAll timeout after " + std::to_string(timeout_ms) + "ms");
        }
    }

    std::exception_ptr first_error;
    {
        std::lock_guard lock(*errors_mutex);
        if (!errors->empty()) {
            first_error = errors->front();
        }
    }
    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

} // namespace async
} // namespace chen
