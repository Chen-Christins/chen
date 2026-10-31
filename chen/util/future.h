/**
 * @file future.h
 * @brief 异步结果句柄 Promise/Future
 * @author chen
 * @date 2026-10-25
 * @copyright GPL-3.0
 */
#pragma once

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "../fiber/fiber.h"
#include "../timer/timer.h"

namespace chen {

template <typename T>
class Future;

template <typename T>
class Promise;

namespace detail {

/**
 * @class FutureStateBase
 * @brief Future 共享状态基类（非模板部分，实现在 future.cc）
 * @details 负责等待队列、完成通知与超时唤醒：
 *          1. 在调度器任务协程里等待 → 登记 waiter 后 YieldToHold，完成时 schedule 唤醒，不占线程
 *          2. 在线程主协程 / 普通线程里等待 → 退化为 std::condition_variable 阻塞
 *          同一个状态只允许完成一次，重复完成会被忽略并打 WARN。
 */
class FutureStateBase : public std::enable_shared_from_this<FutureStateBase> {
public:
    typedef std::shared_ptr<FutureStateBase> ptr;

    FutureStateBase() = default;
    virtual ~FutureStateBase();

    /**
     * @brief 结果是否已经就绪（含异常）
     */
    bool ready() const;

    /**
     * @brief 结果若是异常则重新抛出
     */
    void rethrowIfException() const;

    /**
     * @brief 等待结果就绪（fiber 里挂起协程，普通线程里阻塞线程）
     */
    void wait();

    /**
     * @brief 等待结果就绪，带超时
     * @param timeout_ms 超时毫秒数，0 表示不超时
     * @return bool true 结果已就绪 | false 超时（状态不变，后续仍可 get）
     * @attention fiber 内超时依赖 IOManager 的定时器，裸 Scheduler 下会退化为阻塞线程
     */
    bool waitFor(uint64_t timeout_ms);

    /**
     * @brief 以异常方式完成并唤醒所有等待者
     */
    void setException(std::exception_ptr e);

protected:
    /**
     * @brief 等待者：协程 + 所属调度器
     */
    struct Waiter {
        typedef std::shared_ptr<Waiter> ptr;
        /// 等待者编号，超时后按编号摘除
        uint64_t id = 0;
        /// 是否已经被唤醒过（避免重复 schedule 同一个协程）
        bool woken = false;
        /// 是否已经被自己的超时定时器标记（无需依赖墙钟比较）
        bool timedOut = false;
        /// 所属调度器
        Scheduler* scheduler = nullptr;
        /// 等待中的协程
        Fiber::ptr fiber;
    };

    /**
     * @brief 标记完成并取出所有未唤醒的等待者
     * @pre 调用方必须持有 m_mutex
     * @return 需要唤醒的等待者（须在解锁后调用 wake）
     */
    std::vector<Waiter::ptr> completeLocked();

    /**
     * @brief 唤醒所有等待者但保留等待队列（用于超时让等待者重新检查）
     * @pre 调用方必须持有 m_mutex
     */
    std::vector<Waiter::ptr> markAllLocked();

    /**
     * @brief 在锁外恢复等待者的协程
     */
    static void wake(const std::vector<Waiter::ptr>& waiters);

    /**
     * @brief 重复完成时打 WARN
     * @param api 出错的接口名，用于定位
     */
    static void warnDuplicate(const char* api);

    /// 保护状态与等待队列
    mutable std::mutex m_mutex;
    /// 是否已经完成
    bool m_done = false;
    /// 异常（非空表示以异常完成）
    std::exception_ptr m_exception;
    /// 等待中的协程
    std::vector<Waiter::ptr> m_waiters;
    /// 阻塞等待用的条件变量
    std::condition_variable m_cond;
    /// 等待者编号分配
    uint64_t m_nextWaiterId = 0;

private:
    /**
     * @brief 等待实现
     * @param timeout_ms 0 表示不超时
     * @return bool true 已就绪 | false 超时
     */
    bool waitImpl(uint64_t timeout_ms);

    /**
     * @brief 摘除指定等待者
     * @pre 调用方必须持有 m_mutex
     */
    void removeWaiterLocked(uint64_t id);

    /**
     * @brief 挂起期间用 IOManager 定时器做超时唤醒
     * @param timeout_ms 超时时间
     * @param self 相关 waiter 的信息
     * @param[out] timer 成功时返回定时器句柄
     * @return bool false 表示当前没有 IOManager（无法在挂起期间计时）
     * @pre 调用方必须持有 m_mutex
     */
    bool armTimerLocked(uint64_t timeout_ms, const Waiter::ptr& self, Timer::ptr& timer);
};

/**
 * @brief 带值的共享状态
 * @tparam T 结果类型
 */
template <typename T>
class FutureState : public FutureStateBase {
public:
    typedef std::shared_ptr<FutureState> ptr;

    /**
     * @brief 写入结果并唤醒所有等待者
     */
    void setValue(T v)
    {
        std::vector<Waiter::ptr> waiters;
        {
            std::lock_guard lock(m_mutex);
            if (m_done) {
                warnDuplicate("setValue");
                return;
            }
            m_value = std::move(v);
            waiters = completeLocked();
        }
        wake(waiters);
    }

    /**
     * @brief 等待并取值（异常则重新抛出）
     */
    T get()
    {
        wait();
        std::lock_guard lock(m_mutex);
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
        return m_value;
    }

private:
    /// 结果值
    T m_value{};
};

/**
 * @brief 无返回值的共享状态（实现在 future.cc）
 */
template <>
class FutureState<void> : public FutureStateBase {
public:
    typedef std::shared_ptr<FutureState> ptr;

    /**
     * @brief 标记完成并唤醒所有等待者
     */
    void setValue();

    /**
     * @brief 等待完成（异常则重新抛出）
     */
    void get();
};

} // namespace detail

/**
 * @class Future
 * @brief 异步结果句柄
 * @details 可在多个 fiber / 线程之间共享，结果就绪后可重复 get。
 *          T 需要可拷贝（get() 返回副本）。
 */
template <typename T>
class Future {
public:

    Future() = default;

    explicit Future(detail::FutureState<T>::ptr state)
        : m_state(std::move(state))
    {
    }

    /**
     * @brief 句柄是否有效
     */
    bool valid() const { return m_state != nullptr; }

    /**
     * @brief 结果是否已经就绪
     */
    bool ready() const { return m_state && m_state->ready(); }

    /**
     * @brief 等待结果就绪
     */
    void wait() const
    {
        if (m_state) {
            m_state->wait();
        }
    }

    /**
     * @brief 等待结果就绪，带超时
     * @return bool true 已就绪 | false 超时
     */
    bool waitFor(uint64_t timeout_ms) const { return m_state ? m_state->waitFor(timeout_ms) : true; }

    /**
     * @brief 等待并取出结果（异常则重新抛出）
     * @attention 无效句柄返回 T{}，调用方应先用 valid() 判断
     */
    T get() const { return m_state ? m_state->get() : T{}; }

    /**
     * @brief 取出共享状态（需要与回调配合时使用）
     */
    const detail::FutureState<T>::ptr& state() const { return m_state; }

private:
    /// 共享状态
    detail::FutureState<T>::ptr m_state;
};

/**
 * @class Future<void>
 * @brief 无返回值结果句柄
 */
template <>
class Future<void> {
public:
    typedef std::shared_ptr<detail::FutureState<void>> StatePtr;

    Future() = default;

    explicit Future(StatePtr state);

    /**
     * @brief 句柄是否有效
     */
    bool valid() const;

    /**
     * @brief 结果是否已经就绪
     */
    bool ready() const;

    /**
     * @brief 等待结果就绪
     */
    void wait() const;

    /**
     * @brief 等待结果就绪，带超时
     * @return bool true 已就绪 | false 超时
     */
    bool waitFor(uint64_t timeout_ms) const;

    /**
     * @brief 等待完成（异常则重新抛出）
     */
    void get() const;

    /**
     * @brief 取出共享状态（需要与回调配合时使用）
     */
    const StatePtr& state() const;

private:
    /// 共享状态
    StatePtr m_state;
};

/**
 * @class Promise
 * @brief 结果生产端，可拷贝（副本共享同一状态）
 */
template <typename T>
class Promise {
public:
    Promise()
        : m_state(std::make_shared<detail::FutureState<T>>())
    {
    }

    /**
     * @brief 获取消费端句柄，可多次调用（共享同一状态）
     */
    Future<T> getFuture() const { return Future<T>(m_state); }

    /**
     * @brief 写入结果并唤醒所有等待者（重复调用无效）
     */
    void setValue(T v) { m_state->setValue(std::move(v)); }

    /**
     * @brief 以异常完成（重复调用无效）
     */
    void setException(std::exception_ptr e) { m_state->setException(std::move(e)); }

    /**
     * @brief 是否已经完成
     */
    bool satisfied() const { return m_state->ready(); }

private:
    /// 共享状态
    std::shared_ptr<detail::FutureState<T>> m_state;
};

/**
 * @class Promise<void>
 * @brief 无返回值结果生产端
 */
template <>
class Promise<void> {
public:
    Promise();

    /**
     * @brief 获取消费端句柄，可多次调用（共享同一状态）
     */
    Future<void> getFuture() const;

    /**
     * @brief 标记完成并唤醒所有等待者（重复调用无效）
     */
    void setValue();

    /**
     * @brief 以异常完成（重复调用无效）
     */
    void setException(std::exception_ptr e);

    /**
     * @brief 是否已经完成
     */
    bool satisfied() const;

private:
    /// 共享状态
    std::shared_ptr<detail::FutureState<void>> m_state;
};

} // namespace chen
