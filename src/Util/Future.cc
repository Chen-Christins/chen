/**
 * @file Future.cc
 * @brief 异步结果句柄 Promise/Future 的非模板部分实现
 * @author chen
 * @date 2026-10-25
 * @copyright GPL-3.0
 */
#include "Future.hpp"

#include <chrono>
#include <utility>

#include "../IOManager/IOManager.hpp"
#include "../Log/Log.hpp"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

FutureStateBase::~FutureStateBase() = default;

bool FutureStateBase::ready() const
{
    std::lock_guard lock(mutex_);
    return done_;
}

void FutureStateBase::rethrowIfException() const
{
    std::exception_ptr e;
    {
        std::lock_guard lock(mutex_);
        e = exception_;
    }
    if (e) {
        std::rethrow_exception(e);
    }
}

void FutureStateBase::wait()
{
    waitImpl(0);
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
        std::lock_guard lock(mutex_);
        if (done_) {
            warnDuplicate("setException");
            return;
        }
        exception_ = std::move(e);
        waiters = completeLocked();
    }
    wake(waiters);
}

void FutureStateBase::warnDuplicate(const char* api)
{
    WARN(g_logger) << "Future already satisfied, ignore " << api;
}

std::vector<FutureStateBase::Waiter::ptr> FutureStateBase::completeLocked()
{
    done_ = true;
    std::vector<Waiter::ptr> out;
    out.reserve(waiters_.size());
    for (auto& w : waiters_) {
        if (!w->woken) {
            w->woken = true;
            out.push_back(w);
        }
    }
    waiters_.clear();
    cond_.notify_all();
    return out;
}

std::vector<FutureStateBase::Waiter::ptr> FutureStateBase::markAllLocked()
{
    std::vector<Waiter::ptr> out;
    for (auto& w : waiters_) {
        if (!w->woken) {
            w->woken = true;
            out.push_back(w);
        }
    }
    cond_.notify_all();
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
    for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
        if ((*it)->id == id) {
            waiters_.erase(it);
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
                std::lock_guard lock(state->mutex_);
                if (state->done_) {
                    return;
                }
                waiter->timed_out = true;
                waiters = state->markAllLocked();
            }
            wake(waiters);
        },
        false);
    return true;
}

bool FutureStateBase::waitImpl(uint64_t timeout_ms)
{
    std::unique_lock lock(mutex_);
    if (done_) {
        return true;
    }

    // 线程主协程 / 调度器 root 协程 / 普通线程只能阻塞，Yield 出去就回不来
    if (!Fiber::CanYield()) {
        if (Scheduler::GetThis()) {
            WARN(g_logger) << "Future::wait called in scheduler main fiber, fallback to thread blocking";
        }
        if (timeout_ms) {
            return cond_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
                return done_;
            });
        }
        cond_.wait(lock, [this] {
            return done_;
        });
        return true;
    }

    Waiter::ptr waiter = std::make_shared<Waiter>();
    waiter->id = ++next_waiter_id_;
    waiter->scheduler = Scheduler::GetThis();
    waiter->fiber = Fiber::GetThis();
    waiters_.push_back(waiter);

    Timer::ptr timer;
    if (timeout_ms && !armTimerLocked(timeout_ms, waiter, timer)) {
        // 没有 IOManager 就没有定时器，只能退化成阻塞等待
        removeWaiterLocked(waiter->id);
        WARN(g_logger) << "Future::waitFor in fiber without IOManager, fallback to thread blocking";
        return cond_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
            return done_;
        });
    }

    for (;;) {
        lock.unlock();
        Fiber::YieldToHold();
        lock.lock();
        if (done_) {
            break;
        }
        // 可能是别人的超时定时器唤醒了所有人，也可能只是虚假唤醒
        if (waiter->timed_out) {
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
        std::lock_guard lock(mutex_);
        if (done_) {
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

Future<void>::Future(StatePtr state)
    : state_(std::move(state))
{
}

bool Future<void>::valid() const
{
    return state_ != nullptr;
}

bool Future<void>::ready() const
{
    return state_ && state_->ready();
}

void Future<void>::wait() const
{
    if (state_) {
        state_->wait();
    }
}

bool Future<void>::waitFor(uint64_t timeout_ms) const
{
    return state_ ? state_->waitFor(timeout_ms) : true;
}

void Future<void>::get() const
{
    if (state_) {
        state_->get();
    }
}

const Future<void>::StatePtr& Future<void>::state() const
{
    return state_;
}

Promise<void>::Promise()
    : state_(std::make_shared<FutureState<void>>())
{
}

Future<void> Promise<void>::getFuture() const
{
    return Future<void>(state_);
}

void Promise<void>::setValue()
{
    state_->setValue();
}

void Promise<void>::setException(std::exception_ptr e)
{
    state_->setException(std::move(e));
}

bool Promise<void>::satisfied() const
{
    return state_->ready();
}

} // namespace chen
