/**
 * @file future.cc
 * @brief 异步结果句柄 Promise/Future 的非模板部分实现
 * @author chen
 * @date 2026-10-25
 * @copyright GPL-3.0
 */
#include "future.h"

#include <chrono>
#include <utility>

#include "../iomanager/iomanager.h"
#include "../log/log.h"

namespace chen {

namespace detail {

FutureStateBase::~FutureStateBase() = default;

bool FutureStateBase::ready() const
{
    std::lock_guard lock(m_mutex);
    return m_done;
}

void FutureStateBase::rethrowIfException() const
{
    std::exception_ptr e;
    {
        std::lock_guard lock(m_mutex);
        e = m_exception;
    }
    if (e) {
        std::rethrow_exception(e);
    }
}

void FutureStateBase::wait()
{
    (void)waitImpl(0);
}

bool FutureStateBase::waitFor(uint64_t timeout_ms)
{
    if (timeout_ms == 0) {
        wait();
        return true;
    }
    return waitImpl(timeout_ms);
}

void FutureStateBase::setException(std::exception_ptr e)
{
    std::vector<Waiter::ptr> waiters;
    {
        std::lock_guard lock(m_mutex);
        if (m_done) {
            warnDuplicate("setException");
            return;
        }
        m_exception = std::move(e);
        waiters = completeLocked();
    }
    wake(waiters);
}

void FutureStateBase::warnDuplicate(const char* api)
{
    WARN(LOG_NAME("system")) << "Future already satisfied, ignore " << api;
}

std::vector<FutureStateBase::Waiter::ptr> FutureStateBase::completeLocked()
{
    m_done = true;
    std::vector<Waiter::ptr> out;
    out.reserve(m_waiters.size());
    for (auto& w : m_waiters) {
        if (!w->woken) {
            w->woken = true;
            out.push_back(w);
        }
    }
    m_waiters.clear();
    m_cond.notify_all();
    return out;
}

std::vector<FutureStateBase::Waiter::ptr> FutureStateBase::markAllLocked()
{
    std::vector<Waiter::ptr> out;
    for (auto& w : m_waiters) {
        if (!w->woken) {
            w->woken = true;
            out.push_back(w);
        }
    }
    m_cond.notify_all();
    return out;
}

void FutureStateBase::wake(const std::vector<FutureStateBase::Waiter::ptr>& waiters)
{
    for (auto& w : waiters) {
        if (w->scheduler && w->fiber) {
            w->scheduler->schedule(w->fiber);
        }
    }
}

void FutureStateBase::removeWaiterLocked(uint64_t id)
{
    for (auto it = m_waiters.begin(); it != m_waiters.end(); ++it) {
        if ((*it)->id == id) {
            m_waiters.erase(it);
            return;
        }
    }
}

bool FutureStateBase::armTimerLocked(uint64_t timeout_ms, const Waiter::ptr& self, Timer::ptr& timer)
{
    IOManager* iom = IOManager::GetThis();
    if (!iom) {
        return false;
    }
    std::weak_ptr<FutureStateBase> weak_state = weak_from_this();
    std::weak_ptr<Waiter> weak_waiter = self;
    if (weak_state.expired()) {
        return false;
    }
    timer = iom->addTimer(
        timeout_ms,
        [weak_state, weak_waiter]() {
            auto state = weak_state.lock();
            auto waiter = weak_waiter.lock();
            if (!state || !waiter) {
                return;
            }
            std::vector<Waiter::ptr> waiters;
            {
                std::lock_guard lock(state->m_mutex);
                if (state->m_done) {
                    return;
                }
                waiter->timedOut = true;
                waiters = state->markAllLocked();
            }
            wake(waiters);
        },
        false);
    return true;
}

bool FutureStateBase::waitImpl(uint64_t timeout_ms)
{
    std::unique_lock lock(m_mutex);
    if (m_done) {
        return true;
    }

    // 线程主协程 / 调度器 root 协程 / 普通线程只能阻塞，Yield 出去就回不来
    if (!Fiber::CanYield()) {
        if (Scheduler::GetThis()) {
            WARN(LOG_NAME("system")) << "Future::wait called in scheduler main fiber, fallback to thread blocking";
        }
        if (timeout_ms) {
            return m_cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
                return m_done;
            });
        }
        m_cond.wait(lock, [this] {
            return m_done;
        });
        return true;
    }

    Waiter::ptr waiter = std::make_shared<Waiter>();
    waiter->id = ++m_nextWaiterId;
    waiter->scheduler = Scheduler::GetThis();
    waiter->fiber = Fiber::GetThis();
    m_waiters.push_back(waiter);

    Timer::ptr timer;
    if (timeout_ms && !armTimerLocked(timeout_ms, waiter, timer)) {
        // 没有 IOManager 就没有定时器，只能退化成阻塞等待
        removeWaiterLocked(waiter->id);
        WARN(LOG_NAME("system")) << "Future::waitFor in fiber without IOManager, fallback to thread blocking";
        return m_cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
            return m_done;
        });
    }

    for (;;) {
        lock.unlock();
        Fiber::YieldToHold();
        lock.lock();
        if (m_done) {
            break;
        }
        // 可能是别人的超时定时器唤醒了所有人，也可能只是虚假唤醒
        if (waiter->timedOut) {
            removeWaiterLocked(waiter->id);
            if (timer) {
                timer->cancel();
            }
            return false;
        }
        // 继续等待：重置标志，后续完成时才能再次被唤醒
        waiter->woken = false;
    }
    if (timer) {
        timer->cancel();
    }
    return true;
}

void FutureState<void>::setValue()
{
    std::vector<Waiter::ptr> waiters;
    {
        std::lock_guard lock(m_mutex);
        if (m_done) {
            warnDuplicate("setValue");
            return;
        }
        waiters = completeLocked();
    }
    wake(waiters);
}

void FutureState<void>::get()
{
    wait();
    rethrowIfException();
}

} // namespace detail

Future<void>::Future(StatePtr state)
    : m_state(std::move(state))
{
}

bool Future<void>::valid() const
{
    return m_state != nullptr;
}

bool Future<void>::ready() const
{
    return m_state && m_state->ready();
}

void Future<void>::wait() const
{
    if (m_state) {
        m_state->wait();
    }
}

bool Future<void>::waitFor(uint64_t timeout_ms) const
{
    return m_state ? m_state->waitFor(timeout_ms) : true;
}

void Future<void>::get() const
{
    if (m_state) {
        m_state->get();
    }
}

const Future<void>::StatePtr& Future<void>::state() const
{
    return m_state;
}

Promise<void>::Promise()
    : m_state(std::make_shared<detail::FutureState<void>>())
{
}

Future<void> Promise<void>::getFuture() const
{
    return Future<void>(m_state);
}

void Promise<void>::setValue()
{
    m_state->setValue();
}

void Promise<void>::setException(std::exception_ptr e)
{
    m_state->setException(std::move(e));
}

bool Promise<void>::satisfied() const
{
    return m_state->ready();
}

} // namespace chen
