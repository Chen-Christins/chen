/**
 * @file fiber.h
 * @brief 协程模块
 * @author Christins
 * @date 2024-11-08
 */
#pragma once

#include <functional>
#include <memory>

#include <boost/context/fiber_fcontext.hpp>

namespace chen {

class Scheduler;

/**
 * @brief 协程类封装
 */
class Fiber : public std::enable_shared_from_this<Fiber> {
public:
    typedef std::shared_ptr<Fiber> ptr;

    enum State {
        /// 初始化状态
        INIT,
        /// 暂停状态
        HOLD,
        /// 执行态
        EXEC,
        /// 结束态
        TERM,
        /// 可执行态
        READY,
        /// 异常状态
        EXCEPT
    };
private:
    /**
    * @brief 无参构造函数
    * @attention 每个线程第一个协程的构造
    */
    Fiber();
public:
    /**
    * @brief 构造函数，用于绑定当前m_ctx上下文触发时执行的函数，以及分配栈空间, 用户协程
    * @param task 协程执行的函数
    * @param stacksize 协程栈大小
    * @param use_caller 是否在主协程上调度
    */
    Fiber(std::function<void()> task, size_t stacksize = 0, bool use_caller = false);

    /**
     * @brief 析构函数
     */
    ~Fiber();
    
    /**
     * @brief 重置协程执行函数
     * @param task 新的执行函数
     * @pre getState == INIT, TERM, EXCEPT
     * @post getState = INIT
     */
    void reset(std::function<void()> task);

    /**
     * @brief 将当前协程切换到执行态, 保存主协程或上一个协程的上下文
     * @pre getState != EXEC
     * @post getState = EXEC
     */
    void swapIn();
    
    /**
     * @brief 将当前协程切换到后台, 当前协程切回主协程
     */
    void swapOut();
    
    /**
     * @brief 将当前线程切换到执行状态
     * @pre 执行的为当前线程的主协程
     */
    void call();

    /**
     * @brief 将当前线程切换到后台
     * @pre 执行的为该协程
     * @post 返回到线程的主协程
     */
    void back();

    /**
     * @brief 设置当前协程的状态
     */
    void setState(State v) { m_state = v; }

    /**
     * @brief 返回当前协程的状态
     */
    State getState() const { return m_state; }

    /**
     * @brief 返回当前协程的id
     */
    uint64_t getId() const { return m_id; }
public:
    /**
     * @brief 将静态局部变量t_fiber设置为f
     * @param f 一个Fiber*类型的裸指针
     */
    static void SetThis(Fiber* f);

    /**
     * @brief 返回局部变量t_fiber的智能指针
     * @details 因为开启了std::enable_shared_from_this
     *          可以从自己的作用域传智能指针, 如果它没有值，就
     *			会给他初始化一个主协程。
	 * @details 每个线程第一次调用 Fiber::GetThis() 时
	 * 			会自动创建一个“主协程”（主协程代表当前线程的原生栈环境）
	 * 			并挂在 thread_local 指针上（t_fiber / t_threadFiber）。
     * @return Fiber::ptr Fiber的智能指针
     */
    static Fiber::ptr GetThis();

    /**
     * @brief 将当前的协程状态切到(后台)可执行态
     */
    static void YieldToReady();

    /**
     * @brief 将当前协程切到(后台)暂停状态
     */
    static void YieldToHold();

    /**
     * @brief 返回原子变量的值，当前所有协程的数量
     */
    static uint64_t TotalFibers();

    /**
     * @brief 不在主协程上调度的方法
     */
    static void MainFunc(boost::context::detail::transfer_t t);

    /**
     * @brief 不在主协程上调度的方法
     */
    static void CallMainFunc(boost::context::detail::transfer_t t);

    /**
     * @brief 返回当前协程的id
     */
    static uint64_t GetFiberId();

    /**
     * @brief 清理当前线程的协程资源
     * @details 在 worker 线程退出前调用，重置 t_fiber 和 t_threadFiber，
     *          确保线程的主协程被正确释放，避免 ASan 报 indirect leak。
     */
    static void CleanupThread();
private:
    /// 协程id
    uint64_t m_id = 0;
    /// 协程栈大小
    uint32_t m_stacksize = 0;
    /// 协程状态
    State m_state = INIT;
    /// 协程上下文（目标/恢复点）
    boost::context::detail::fcontext_t m_ctx = nullptr;
    /// 调用者上下文
    boost::context::detail::fcontext_t m_caller = nullptr;
    /// 协程运行栈指针
    void* m_stack = nullptr;
    /// 协程运行函数
    std::function<void()> m_task;
};

}
