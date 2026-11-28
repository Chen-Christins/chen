/**
 * @file Schedule.hpp
 * @brief 协程调度
 * @author Christins
 * @date 2024-11-10
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include "../Fiber/Fiber.hpp"
#include "../Thread/Thread.hpp"

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
    Scheduler(size_t thread_count = 1, bool use_caller = true, const std::string& name = "");

    /**
     * @brief 析构函数，设为虚类是为了充当基类
     */
    virtual ~Scheduler();

    /**
     * @brief 获取调度器名称
     */
    const std::string& getName() const { return name_; }

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
    void schedule(FiberOrCb fc, int thread = -1)
    {
        bool need_tickle = false;
        {
            std::lock_guard lock(mutex_);
            need_tickle = scheduleNoLock(fc, thread);
        }
        if (need_tickle) {
            tickle();
        }
    }

    /// 批量调度协程或者任务函数
    template <class InputIterator>
    void schedule(InputIterator begin, InputIterator end)
    {
        bool need_tickle = false;
        {
            std::lock_guard lock(mutex_);
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
     * @brief 是否已经调用过 stop()（不等价于 stopping() 的完整条件）
     * @details 供空闲循环判断"正在停止"，据此缩短 epoll 轮询间隔，避免睡满保底超时拖慢退出
     */
    bool isStopping();

    /**
     * @brief 设置当前线程局部变量的调度器
     */
    void setThis();

    /**
     * @brief 是否有空闲的线程
     */
    bool hasIdleThreads() { return idle_thread_count_ > 0; }

    /**
     * @brief 获取待调度任务数量
     * @return size_t 当前任务队列长度
     */
    size_t getTaskCount();

private:
    /// 协程调度启动(无锁的), 主要是检查任务队列是否还有任务，没有就添加，否则tickle一下
    template <class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread)
    {
        /* 是否需要通知一下, 为空就需要插入之后直接开始调度 */
        bool need_tickle = fibers_.empty();
        FiberAndThread ft(fc, thread);
        if (ft.fiber || ft.cb) {
            fibers_.push_back(ft);
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
        int thread_id;
        // 确定协程在哪一个线程上面执行
        FiberAndThread(Fiber::ptr f, int thr);
        /* 传入的智能指针swap，这样传入的智能指针就是空指针，
            引用计数就会减一，防止出现引用释放的问题，引用数量并没有全部释放掉，
            没有真实的释放 */
        FiberAndThread(Fiber::ptr* f, int thr);
        // 确定回调函数在哪一个线程上面执行
        FiberAndThread(std::function<void()> f, int thr);
        /* 传入的智能指针swap，这样传入的智能指针就是空指针，
            引用计数就会减一，防止出现引用释放的问题，引用数量并没有全部释放掉，
            没有真实的释放 */
        FiberAndThread(std::function<void()>* f, int thr);
        // 默认构造
        FiberAndThread();
        /**
         * @brief 将任务信息置为空
         */
        void reset();
    };

private:
    /// 互斥锁
    std::mutex mutex_;
    /// 线程池
    std::vector<Thread::ptr> threads_;
    /// 待执行的协程队列
    std::list<FiberAndThread> fibers_;
    /// 协程调度器的名称
    std::string name_;
    /// user_caller为true时生效，调度协程
    Fiber::ptr root_fiber_;

protected:
    /// 协程下的线程id数组
    std::vector<int> thread_ids_;
    /// 线程数量
    size_t thread_count_ = 0;
    /// 工作线程的数量
    std::atomic<size_t> active_thread_count_ = {0};
    /// 空闲线程数量
    std::atomic<size_t> idle_thread_count_ = {0};
    /// 是否正在停止（stop() 与工作线程并发读写，必须原子）
    std::atomic<bool> stopping_ = {true};
    /// 是否自动停止（stop() 写、stopping() 读，必须原子）
    std::atomic<bool> auto_stop_ = {false};
    /// 主线程id(use_caller的)
    int root_thread_id_ = 0;
};

class SchedulerSwitcher {
public:
    SchedulerSwitcher(Scheduler* target = nullptr);
    ~SchedulerSwitcher();

    SchedulerSwitcher(const SchedulerSwitcher&) = delete;
    SchedulerSwitcher& operator=(const SchedulerSwitcher&) = delete;

private:
    Scheduler* caller_ = nullptr;
};

} // namespace chen
