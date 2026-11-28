/**
 * @file Mutex.hpp
 * @brief 锁和信号量的封装
 * @author Christins
 * @date 2024-11-03
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <mutex>
#include <pthread.h>
#include <semaphore.h>
#include <string>

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

/**
 * @brief 协程感知互斥锁
 * @details 协程里等待时让出执行权（不占线程，等待期间该线程仍能处理别的协程），
 *          普通线程里等待时走条件变量。同一把锁可以被协程与线程混合使用。
 *
 *          与 std::mutex 的关键区别：临界区内允许调用会让出的 API
 *          （DB / Redis / sleep / 网络 IO）。std::mutex 在协程里跨让出使用是
 *          死锁隐患——持锁协程让出后，第二个协程阻塞的是整个线程，
 *          线程卡住后持锁协程的 IO 完成事件再也无人处理。
 * @note 不可重入：同一协程重复 lock() 会抛 std::logic_error（等待自己必然死锁）。
 */
class Mutex : public Noncopyable {
public:
    typedef ScopedLockImpl<Mutex> Lock;

    Mutex();
    ~Mutex();

    /**
     * @brief 加锁，必要时让出协程或阻塞线程
     */
    void lock();

    /**
     * @brief 尝试加锁，不等待
     * @return bool 是否拿到锁
     */
    bool tryLock();

    /**
     * @brief 解锁
     */
    void unlock();

    /**
     * @brief 当前是否已被持有
     */
    bool isLocked() const;

    /**
     * @brief 持有者描述（协程 id / 线程 id），主要供诊断使用
     */
    std::string describeHolder() const;

private:
    /// 保护下面的簿记字段，绝不跨让出持有
    mutable std::mutex state_mutex_;
    /// 普通线程等待通道
    std::condition_variable thread_cv_;
    /// 协程等待通道
    FiberSemaphore fiber_semaphore_;
    bool locked_;
    size_t fiber_waiters_;
    /// 诊断：持锁协程 id（0 表示普通线程持有）
    std::atomic<uint64_t> holder_fiber_;
    /// 诊断：持锁线程 id
    std::atomic<int64_t> holder_thread_;
};

/**
 * @brief 协程感知读写锁
 * @details 读锁之间共享，写锁独占；写者优先（有写者在等时新的读锁排队），
 *          避免写者被持续的读流量饿死。等待时协程让出、线程阻塞，语义同 Mutex。
 * @note 不可重入：同一协程重复加同一把读锁/写锁不会抛异常，但卸载次数必须匹配。
 */
class RWMutex : public Noncopyable {
public:
    /**
     * @brief 读锁 RAII
     */
    class ReadLock {
    public:
        explicit ReadLock(RWMutex& mutex)
            : mutex_(mutex)
            , locked_(false)
        {
            lock();
        }

        ~ReadLock() { unlock(); }

        void lock()
        {
            if (!locked_) {
                mutex_.rlock();
                locked_ = true;
            }
        }

        void unlock()
        {
            if (locked_) {
                mutex_.runlock();
                locked_ = false;
            }
        }

    private:
        RWMutex& mutex_;
        bool locked_;
    };

    /**
     * @brief 写锁 RAII
     */
    class WriteLock {
    public:
        explicit WriteLock(RWMutex& mutex)
            : mutex_(mutex)
            , locked_(false)
        {
            lock();
        }

        ~WriteLock() { unlock(); }

        void lock()
        {
            if (!locked_) {
                mutex_.wlock();
                locked_ = true;
            }
        }

        void unlock()
        {
            if (locked_) {
                mutex_.wunlock();
                locked_ = false;
            }
        }

    private:
        RWMutex& mutex_;
        bool locked_;
    };

    RWMutex();
    ~RWMutex();

    void rlock();
    void runlock();
    void wlock();
    void wunlock();

    int getReaderCount() const;
    bool isWriting() const;

private:
    /// 唤醒所有等待者（调用方已释放 state_mutex_）
    void wakeWaiters(size_t fiber_waiters);

    /// 保护下面的簿记字段，绝不跨让出持有
    mutable std::mutex state_mutex_;
    std::condition_variable thread_cv_;
    FiberSemaphore fiber_semaphore_;
    int readers_;
    int waiting_writers_;
    bool writer_;
    size_t fiber_waiters_;
};

} // namespace chen
