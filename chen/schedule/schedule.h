/**
 * @file schedule.h
 * @brief 协程调度
 * @author Christins
 * @date 2024-11-10
 */
#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include "../fiber/fiber.h"
#include "../thread/thread.h"

namespace chen {

/**
 * @brief 协程调度器
 * @details 封装的是N-M的协程调度器
 *          内部有一个线程池,支持协程在线程池里面切换
 */
class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;

    /**
     * @brief 构造函数
     * @param[in] threads 线程数量，默认就是一根线程
     * @param[in] use_caller 
     *            true: 执行协程调度器的那一根线程也纳入工作线程中，这个的执行效率较高
     *            false: 不纳入进来
     * @param[in] name 调度器名称
     */
    Scheduler(size_t threadCount = 1, bool use_caller = true, const std::string& name = "");
    
    /**
     * @brief 析构函数，设为虚类是为了充当基类
     */
    virtual ~Scheduler();

    /**
     * @brief 获取调度器名称
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief 获取当前的这个协程调度器
     */
    static Scheduler* GetThis();

    /**
     * @brief 获取主协程
     */
    static Fiber* GetMainFiber();
    
    /**
     * @brief 开始调度器
     */
    void start();
    
    /**
     * @brief 停止调度器
     */
    void stop();
    /// 调度一个协程或者传入一个任务函数，需要在协程调度器里面执行
    template <class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            std::lock_guard lock(m_mutex);
            need_tickle = scheduleNoLock(fc, thread);
        }
        if (need_tickle) {
            tickle();
        }
    }

    /// 批量调度协程或者任务函数
    template <class InputIterator>
    void schedule(InputIterator begin, InputIterator end) {
        bool need_tickle = false;
        {
            std::lock_guard lock(m_mutex);
            while (begin != end) {
                need_tickle = scheduleNoLock(&*begin, -1) | need_tickle;
                ++begin;
            }
        }
        if (need_tickle) {
            tickle();
        }
    }

	void switchTo(int thread = -1);

    std::ostream& dump(std::ostream& os);
protected:
    /**
     * @brief 通知有任务来了
     */
    virtual void tickle();

    /**
     * @brief 调度器运行
     */
    void run();

    /**
     * @brief 没有任务做的时候，就执行idle，这个idle做什么，取决于它的子类，
     * @details 1. 当没有任务做的时候，将cpu占住
     *          2. 隔一段时间sleep一下，让出执行时间
     *          3. 因为是网络服务器，而且是在linux上，所以我们要和epoll相结合，
     *             让其陷入epoll_wait中，让epoll_wait来唤醒执行
     */
    virtual void idle();
    
    /**
     * @brief 为了让子类有清理任务的机会, 停止的条件
     */
    virtual bool stopping();

	/**
     * @brief 设置当前线程局部变量的调度器
     */
    void setThis();

    /**
     * @brief 是否有空闲的线程
     */
    bool hasIdleThreads() { return m_idleThreadCount > 0; }
private:
    /// 协程调度启动(无锁的), 主要是检查任务队列是否还有任务，没有就添加，否则tickle一下
    template <class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
		/* 是否需要通知一下, 为空就需要插入之后直接开始调度 */
        bool need_tickle = m_fibers.empty();
        FiberAndThread ft(fc, thread);
        if (ft.fiber || ft.cb) {
            m_fibers.push_back(ft);
        }
        return need_tickle;
    }
private:
    struct FiberAndThread {
        /// 协程
        Fiber::ptr fiber;
        /// 任务
        std::function<void()> cb;
        /// 线程id
        int threadId;
        // 确定协程在哪一个线程上面执行
        FiberAndThread(Fiber::ptr f, int thr)
            :fiber(f), threadId(thr) {
        }
        /* 传入的智能指针swap，这样传入的智能指针就是空指针，
            引用计数就会减一，防止出现引用释放的问题，引用数量并没有全部释放掉，
            没有真实的释放 */
        FiberAndThread(Fiber::ptr* f, int thr)
            :threadId(thr) {
            fiber.swap(*f);
        }
        // 确定回调函数在哪一个线程上面执行
        FiberAndThread(std::function<void()> f, int thr)
            :cb(f), threadId(thr) {
        }
        /* 传入的智能指针swap，这样传入的智能指针就是空指针，
            引用计数就会减一，防止出现引用释放的问题，引用数量并没有全部释放掉，
            没有真实的释放 */
        FiberAndThread(std::function<void()>* f, int thr)
            :threadId(thr) {
            cb.swap(*f);
        }
        // 默认构造
        FiberAndThread()
            :threadId(-1) {
        }
        /**
         * @brief 将任务信息置为空
         */
        void reset() {
            fiber = nullptr;
            cb = nullptr;
            threadId = -1;
        }
    };
private:
    /// 互斥锁
    std::mutex m_mutex;
    /// 线程池
    std::vector<Thread::ptr> m_threads;
    /// 待执行的协程队列
    std::list<FiberAndThread> m_fibers;
    /// 协程调度器的名称
    std::string m_name;
    /// user_caller为true时生效，调度协程
    Fiber::ptr m_rootFiber;
protected:
    /// 协程下的线程id数组
    std::vector<int> m_threadIds;
    /// 线程数量
    size_t m_threadCount = 0;
    /// 工作线程的数量
    std::atomic<size_t> m_activeThreadCount = {0};
    /// 空闲线程数量
    std::atomic<size_t> m_idleThreadCount = {0};
    /// 是否正在停止
    bool m_stopping = true;
    /// 是否自动停止
    bool m_autoStop = false;
    /// 主线程id(use_caller的)
    int m_rootThreadId = 0;
};

class SchedulerSwitcher : public Scheduler {
public:
	SchedulerSwitcher(Scheduler* target = nullptr);
	~SchedulerSwitcher();
private:
	Scheduler* m_caller;
};

}
