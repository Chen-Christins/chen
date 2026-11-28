#include "Mutex.hpp"

#include "../Log/Log.hpp"
#include "../Schedule/Schedule.hpp"
#include "Macro.hpp"
#include "Util.hpp"

#include <stdexcept>
#include <string>

namespace chen {

Semaphore::Semaphore(uint32_t count)
{
    if (sem_init(&semaphore_, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore()
{
    sem_destroy(&semaphore_);
}

void Semaphore::wait()
{
    if (sem_wait(&semaphore_)) {
        throw std::logic_error("sem_wait error");
    }
}

void Semaphore::notify()
{
    if (sem_post(&semaphore_)) {
        throw std::logic_error("sem_post error");
    }
}

SpinLock::SpinLock()
{
    pthread_spin_init(&mutex_, 0);
}

SpinLock::~SpinLock()
{
    pthread_spin_destroy(&mutex_);
}

FiberSemaphore::FiberSemaphore(size_t initail_concurrency)
    : concurrency_(initail_concurrency)
{
}

FiberSemaphore::~FiberSemaphore()
{
    ASSERT(waiters_.empty());
}

bool FiberSemaphore::tryWait()
{
    ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(mutex_);
        if (concurrency_ > 0u) {
            --concurrency_;
            return true;
        }
        return false;
    }
}

void FiberSemaphore::wait()
{
    ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(mutex_);
        if (concurrency_ > 0u) {
            --concurrency_;
            return;
        }
        waiters_.push_back(std::pair(Scheduler::GetThis(), Fiber::GetThis()));
    }
    Fiber::YieldToHold();
}

void FiberSemaphore::notify()
{
    MutexType::Lock lock(mutex_);
    if (!waiters_.empty()) {
        auto next = waiters_.front();
        waiters_.pop_front();
        next.first->schedule(next.second);
    } else {
        ++concurrency_;
    }
}

Mutex::Mutex()
    : locked_(false)
    , fiber_waiters_(0)
    , holder_fiber_(0)
    , holder_thread_(-1)
{
}

Mutex::~Mutex()
{
    ASSERT(!locked_);
    ASSERT(fiber_waiters_ == 0u);
}

void Mutex::lock()
{
    const bool can_yield = Fiber::CanYield();
    const Fiber::ptr self = can_yield ? Fiber::GetThis() : nullptr;
    std::unique_lock<std::mutex> lock(state_mutex_);
    for (;;) {
        if (!locked_) {
            locked_ = true;
            holder_fiber_.store(self ? self->getId() : 0u);
            holder_thread_.store(static_cast<int64_t>(GetThreadId()));
            return;
        }

        if (self && holder_fiber_.load() == self->getId()) {
            const uint64_t fiber_id = self->getId();
            lock.unlock();
            throw std::logic_error("chen::Mutex: recursive lock by fiber " + std::to_string(fiber_id));
        }

        if (can_yield) {
            ++fiber_waiters_;
            lock.unlock();
            fiber_semaphore_.wait();
            lock.lock();
            --fiber_waiters_;
        } else {
            thread_cv_.wait(lock);
        }
    }
}

bool Mutex::tryLock()
{
    const bool can_yield = Fiber::CanYield();
    const Fiber::ptr self = can_yield ? Fiber::GetThis() : nullptr;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (locked_) {
        return false;
    }
    locked_ = true;
    holder_fiber_.store(self ? self->getId() : 0u);
    holder_thread_.store(static_cast<int64_t>(GetThreadId()));
    return true;
}

void Mutex::unlock()
{
    size_t fiber_waiters = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!locked_) {
            // 析构路径也可能走到这里，不能抛异常
            ERROR(LOG_ROOT()) << "chen::Mutex::unlock without lock, thread=" << GetThreadId();
            return;
        }
        locked_ = false;
        holder_fiber_.store(0u);
        holder_thread_.store(-1);
        fiber_waiters = fiber_waiters_;
    }

    // 先唤醒协程（让出通道），再唤醒线程；多唤醒一次只会让等待者重新检查条件
    for (size_t i = 0; i < fiber_waiters; ++i) {
        fiber_semaphore_.notify();
    }
    thread_cv_.notify_one();
}

bool Mutex::isLocked() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return locked_;
}

std::string Mutex::describeHolder() const
{
    const uint64_t fiber_id = holder_fiber_.load();
    const int64_t thread_id = holder_thread_.load();
    if (fiber_id != 0u) {
        return "fiber " + std::to_string(fiber_id) + " on thread " + std::to_string(thread_id);
    }
    if (thread_id >= 0) {
        return "thread " + std::to_string(thread_id);
    }
    return "unlocked";
}

RWMutex::RWMutex()
    : readers_(0)
    , waiting_writers_(0)
    , writer_(false)
    , fiber_waiters_(0)
{
}

RWMutex::~RWMutex()
{
    ASSERT(readers_ == 0);
    ASSERT(!writer_);
    ASSERT(fiber_waiters_ == 0u);
}

void RWMutex::wakeWaiters(size_t fiber_waiters)
{
    for (size_t i = 0; i < fiber_waiters; ++i) {
        fiber_semaphore_.notify();
    }
    thread_cv_.notify_all();
}

void RWMutex::rlock()
{
    const bool can_yield = Fiber::CanYield();
    std::unique_lock<std::mutex> lock(state_mutex_);
    for (;;) {
        if (!writer_ && waiting_writers_ == 0) {
            ++readers_;
            return;
        }
        if (can_yield) {
            ++fiber_waiters_;
            lock.unlock();
            fiber_semaphore_.wait();
            lock.lock();
            --fiber_waiters_;
        } else {
            thread_cv_.wait(lock);
        }
    }
}

void RWMutex::runlock()
{
    bool wake = false;
    size_t fiber_waiters = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (readers_ <= 0) {
            ERROR(LOG_ROOT()) << "chen::RWMutex::runlock without rlock, thread=" << GetThreadId();
            return;
        }
        --readers_;
        // 只有「最后一个读者离开且有人在等写」时才需要唤醒；否则读锁可以自由流动
        if (readers_ == 0 && waiting_writers_ > 0) {
            wake = true;
            fiber_waiters = fiber_waiters_;
        }
    }
    if (wake) {
        wakeWaiters(fiber_waiters);
    }
}

void RWMutex::wlock()
{
    const bool can_yield = Fiber::CanYield();
    std::unique_lock<std::mutex> lock(state_mutex_);
    ++waiting_writers_;
    for (;;) {
        if (!writer_ && readers_ == 0) {
            writer_ = true;
            --waiting_writers_;
            return;
        }
        if (can_yield) {
            ++fiber_waiters_;
            lock.unlock();
            fiber_semaphore_.wait();
            lock.lock();
            --fiber_waiters_;
        } else {
            thread_cv_.wait(lock);
        }
    }
}

void RWMutex::wunlock()
{
    size_t fiber_waiters = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!writer_) {
            ERROR(LOG_ROOT()) << "chen::RWMutex::wunlock without wlock, thread=" << GetThreadId();
            return;
        }
        writer_ = false;
        fiber_waiters = fiber_waiters_;
    }
    wakeWaiters(fiber_waiters);
}

int RWMutex::getReaderCount() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return readers_;
}

bool RWMutex::isWriting() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return writer_;
}

} // namespace chen
