/**
 * @file mutex.h
 * @brief 锁和信号量的封装
 * @author Christins
 * @date 2024-11-03
 */
#pragma once

#include <cstdint>
#include <list>
#include <pthread.h>
#include <semaphore.h>
#include <stdexcept>

#include "../fiber/fiber.h"
#include "noncopyable.h"

namespace chen {

/**
 * @brief 信号量封装
 */
class Semaphore : Noncopyable {
public:
    Semaphore(uint32_t count = 0) {
        if (sem_init(&m_semaphore, 0, count)) {
            throw std::logic_error("sem_init error");
        }
    }
    ~Semaphore() {
        sem_destroy(&m_semaphore);
    }

    void wait() {
        if (sem_wait(&m_semaphore)) {
            throw std::logic_error("sem_wait error");
        }
    }

    void notify() {
        if (sem_post(&m_semaphore)) {
            throw std::logic_error("sem_post error");
        }
    }
private:
    /// 信号量
    sem_t m_semaphore;
};

/**
 * @brief 局部锁 RAII机制
 */
template <class T>
class ScopedLockImpl {
public:
    ScopedLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.lock();
        m_locked = true;
    }
    ~ScopedLockImpl() {
        unlock();
    }
    void lock() {
        if (!m_locked) {
            m_mutex.lock();
            m_locked = true;
        }
    }
    void unlock() {
        if (m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    T& m_mutex;
    bool m_locked;
};

/**
 * @brief 自旋锁
 */
class SpinLock : public Noncopyable {
public:
    typedef ScopedLockImpl<SpinLock> Lock;
    
    SpinLock() {
        pthread_spin_init(&m_mutex, 0);
    }
    ~SpinLock() {
        pthread_spin_destroy(&m_mutex);
    }
    void lock() {
        pthread_spin_lock(&m_mutex);
    }
    void unlock() {
        pthread_spin_unlock(&m_mutex);
    }

private:
    pthread_spinlock_t m_mutex;
};

class Scheduler;
class FiberSemaphore : Noncopyable {
public:
    typedef SpinLock MutexType;

    FiberSemaphore(size_t initail_concurrency = 0);
    ~FiberSemaphore();

    bool tryWait();
    void wait();
    void notify();

    size_t getConcurrency() const { return m_concurrency; }
    void reset() { m_concurrency = 0; }
private:
    MutexType m_mutex;
    std::list<std::pair<Scheduler*, Fiber::ptr>> m_waiters;
    size_t m_concurrency;
};

}
