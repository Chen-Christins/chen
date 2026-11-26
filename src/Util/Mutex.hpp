/**
 * @file Mutex.hpp
 * @brief 锁和信号量的封装
 * @author Christins
 * @date 2024-11-03
 * @copyright GPL-3.0
 */
#pragma once

#include <list>
#include <pthread.h>
#include <semaphore.h>

#include "../Fiber/Fiber.hpp"
#include "Noncopyable.hpp"

namespace chen {

/**
 * @brief 信号量封装
 */
class Semaphore : Noncopyable {
public:
    Semaphore(uint32_t count = 0);
    ~Semaphore();

    void wait();

    void notify();

private:
    /// 信号量
    sem_t semaphore_;
};

/**
 * @brief 局部锁 RAII机制
 */
template <class T>
class ScopedLockImpl {
public:
    ScopedLockImpl(T& mutex)
        : mutex_(mutex)
    {
        mutex_.lock();
        locked_ = true;
    }

    ~ScopedLockImpl() { unlock(); }

    void lock()
    {
        if (!locked_) {
            mutex_.lock();
            locked_ = true;
        }
    }

    void unlock()
    {
        if (locked_) {
            mutex_.unlock();
            locked_ = false;
        }
    }

private:
    T& mutex_;
    bool locked_;
};

/**
 * @brief 自旋锁
 */
class SpinLock : public Noncopyable {
public:
    typedef ScopedLockImpl<SpinLock> Lock;

    SpinLock();
    ~SpinLock();

    void lock() { pthread_spin_lock(&mutex_); }

    void unlock() { pthread_spin_unlock(&mutex_); }

private:
    pthread_spinlock_t mutex_;
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

    size_t getConcurrency() const { return concurrency_; }

    void reset() { concurrency_ = 0; }

private:
    MutexType mutex_;
    std::list<std::pair<Scheduler*, Fiber::ptr>> waiters_;
    size_t concurrency_;
};

} // namespace chen
