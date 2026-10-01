/**
 * @file timer.h
 * @brief 定时器模块
 * @author Christins
 * @date 2024-11-14
 */
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>

namespace chen {

class TimerManager;

class Timer : public std::enable_shared_from_this<Timer> {
friend class TimerManager;
public:
    typedef std::shared_ptr<Timer> ptr;
    /**
     * @brief 取消当前定时器
     */
    bool cancel();

    /**
     * @brief 刷新定时器，将要执行的定时器进行类似延后的刷新
     */
    bool refresh();
    
    /**
     * @brief 重新设置定时器，包括其中的任务和开始时间
     * @param ms 间隔时间
     * @param from_now 定时器的设置时间
     */
    bool reset(uint64_t ms, bool from_now);
private:
    /**
     * @brief 构造函数（多参数）
     * @param ms 定时器的执行间隔时间
     * @param cb 回调函数
     * @param recurring 是否循环触发
     * @param manager 定时器管理器
     */
    Timer(uint64_t ms, std::function<void()> cb
            ,bool recurring, TimerManager* manager);
    
    /**
     * @brief 构造函数（单一参数）
     * @param next 直接设置它的触发时间戳
     */
    Timer(uint64_t next);

    /**
     * @brief 定时器集合的排序规则，重载方法
     */
    struct Comparator {
        bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
    };
private:
    /// 是否循环定时器
    bool m_recurring = false;
    /// 执行周期
    uint64_t m_ms = 0;
    /// 精确的执行时间
    uint64_t m_next = 0;
    /// 回调函数
    std::function<void()> m_cb;
    /// 定时器管理类
    TimerManager* m_manager = nullptr;
};

class TimerManager {
friend class Timer;
public:
    /**
     * @brief 构造函数
     * @details 设定当前管理器的第一次创建时间
     */
    TimerManager();

    /**
     * @brief 析构函数
     */
    virtual ~TimerManager() {}

    /**
     * @brief 添加定时器
     * @param ms 触发的间隔时间
     * @param cb 回调函数
     * @param recurring 是否循环触发
     * @return Timer::ptr 当前创建的定时器
     */
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb
                        ,bool recurring = false);
    
    /**
     * @brief 创建条件定时器
     * @param ms 触发的间隔时间
     * @param cb 回调函数
     * @param weak_cond 弱引用
     * @param recurring 是否循环触发
     * @return Timer::ptr 当前创建的定时器
     */
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb
                        ,std::weak_ptr<void> weak_cond
                        ,bool recurring = false);
    /**
     * @brief 获取距离当前时间最近的一个定时器的执行时间
     * @details 1. 如果定时器集合为空，就返回无穷大
     *          2. 如果距离当前最近的一个定时器以及执行了，就返回 0
     *          3. 否则返回与现在间隔的时间长度
     * @return uint64_t 
     */
    uint64_t getNextTimer();

    /**
     * @brief 获取所有已经触发的定时器任务
     * @param[out] cbs 回调函数集合
     */
    void listExpiredCb(std::vector<std::function<void()>>& cbs);

    /**
     * @brief 定时器集合里是否存在定时器
     */
    bool hasTimer();

    /**
     * @brief 取消所有定时器（仅清理集合，不触发回调）
     */
    void cancelAllTimers();
protected:
    /**
     * @brief 添加定时到集合中，同时触发onTimerInsertedAtFront
     * @param val 要插入的定时器
     * @param lock 写锁
     */
    void addTimer(Timer::ptr val, std::unique_lock<std::shared_mutex>& lock);

    /**
     * @brief tickle()
     */
    virtual void onTimerInsertedAtFront() = 0;
private:
    /**
     * @brief 用于校验服务器的时间
     * @param now_ms 当前的时间戳
     */
    bool detectClockRollover(uint64_t now_ms);
private:
    /// 读写锁
    std::shared_mutex m_mutex;
    /// 定时器集合
    std::set<Timer::ptr, Timer::Comparator> m_timers;
    /// 是否触发onTimerInsertedAtFront
    bool m_tickled = false;
    /// 上次执行的时间
    uint64_t m_previouseTime = 0;
};

}
