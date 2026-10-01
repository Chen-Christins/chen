#include "mutex.h"

#include "../schedule/schedule.h"
#include "macro.h"

namespace chen {

FiberSemaphore::FiberSemaphore(size_t initail_concurrency)
    :m_concurrency(initail_concurrency) {
}

FiberSemaphore::~FiberSemaphore() {
    ASSERT(m_waiters.empty());
}

bool FiberSemaphore::tryWait() {
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

void FiberSemaphore::wait() {
    ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        if (m_concurrency > 0u) {
            --m_concurrency;
            return ;
        }
        m_waiters.push_back(std::pair(Scheduler::GetThis(), Fiber::GetThis()));
    }
    Fiber::YieldToHold();
}

void FiberSemaphore::notify() {
    MutexType::Lock lock(m_mutex);
    if (!m_waiters.empty()) {
        auto next = m_waiters.front();
        m_waiters.pop_front();
        next.first->schedule(next.second);
    } else {
        ++m_concurrency;
    }
}

}
