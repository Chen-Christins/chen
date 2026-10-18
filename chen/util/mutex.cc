#include "mutex.h"

#include "../schedule/schedule.h"
#include "macro.h"

namespace chen {

Semaphore::Semaphore(uint32_t count)
{
    if (sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore()
{
    sem_destroy(&m_semaphore);
}

void Semaphore::wait()
{
    if (sem_wait(&m_semaphore)) {
        throw std::logic_error("sem_wait error");
    }
}

void Semaphore::notify()
{
    if (sem_post(&m_semaphore)) {
        throw std::logic_error("sem_post error");
    }
}

SpinLock::SpinLock()
{
    pthread_spin_init(&m_mutex, 0);
}

SpinLock::~SpinLock()
{
    pthread_spin_destroy(&m_mutex);
}

FiberSemaphore::FiberSemaphore(size_t initail_concurrency)
    : m_concurrency(initail_concurrency)
{
}

FiberSemaphore::~FiberSemaphore()
{
    ASSERT(m_waiters.empty());
}

bool FiberSemaphore::tryWait()
{
    ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        if (m_concurrency > 0u) {
            --m_concurrency;
            return true;
        }
        return false;
    }
}

void FiberSemaphore::wait()
{
    ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        if (m_concurrency > 0u) {
            --m_concurrency;
            return;
        }
        m_waiters.push_back(std::pair(Scheduler::GetThis(), Fiber::GetThis()));
    }
    Fiber::YieldToHold();
}

void FiberSemaphore::notify()
{
    MutexType::Lock lock(m_mutex);
    if (!m_waiters.empty()) {
        auto next = m_waiters.front();
        m_waiters.pop_front();
        next.first->schedule(next.second);
    } else {
        ++m_concurrency;
    }
}

} // namespace chen
