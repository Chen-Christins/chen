#include "Mutex.hpp"

#include "../Schedule/Schedule.hpp"
#include "Macro.hpp"

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

} // namespace chen
