#include "timer.h"

#include "../util/util.h" // IWYU pragma: keep

namespace chen {

bool Timer::Comparator::operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const {
    if (!lhs && !rhs) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    if (!rhs) {
        return false;
    }
    if (lhs->m_next < rhs->m_next) {
        return true;
    }
    if (lhs->m_next > rhs->m_next) {
        return false;
    }
    return lhs.get() < rhs.get();
}

Timer::Timer(uint64_t next)
    :m_next(next) {
}

Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager)
    :m_recurring(recurring)
    ,m_ms(ms)
    ,m_cb(cb)
    ,m_manager(manager) {
    m_next = ms + GetCurrentMs();
}

bool Timer::cancel() {
    std::unique_lock lock(m_manager->m_mutex);

    // 在定时器集合中查找自己（即使 m_cb 已被 listExpiredCb 消费，
    // 循环定时器仍会被重新插入集合，所以需要以集合中的存在性为准）
    auto it = m_manager->m_timers.find(shared_from_this());
    bool found = (it != m_manager->m_timers.end());
    if (found) {
        m_manager->m_timers.erase(it);
    }
    // 清理回调（如果还在的话）
    if (m_cb) {
        m_cb = nullptr;
    }
    return found;
}

bool Timer::refresh() {
    std::unique_lock lock(m_manager->m_mutex);
    if (!m_cb) {
        return false;
    }
    // 找到自己的定时器
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) {
        return false;
    }
    // 然后删除
    m_manager->m_timers.erase(it);
    // 更新执行时间
    m_next = GetCurrentMs() + m_ms;
    // 重新插入定时器
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
    // 如果周期相同，并且不从当前时间开始
    if (ms == m_ms && !from_now) {
        return true;
    }
    std::unique_lock lock(m_manager->m_mutex);
    if (!m_cb) {
        return false;
    }
    // 找到自己的那个定时器, 找不到说明不能重置
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) {
        return false;
    }
    // 找到了就删除
    m_manager->m_timers.erase(it);
    uint64_t start = 0;
    if (from_now) {
        start = GetCurrentMs();
    } else {
        /* 这里的m_next和m_ms都是在本次重置之前的数值，如果不是from_now，
            那么就按照上一次创建的时间节点和间隔时间设定 */
        start = m_next - m_ms;
    }
    // 更新当前时间间隔
    m_ms = ms;
    m_next = start + m_ms;
    // 然后就添加定时器
    m_manager->addTimer(shared_from_this(), lock);
    return true;
}

TimerManager::TimerManager() {
    m_previouseTime = GetCurrentMs();
}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb
                    ,bool recurring) {
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    std::unique_lock lock(m_mutex);

    addTimer(timer, lock);
    return timer;
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    // weak_ptr的lock会返回一个shared_ptr，如果weak_ptr失效了，就会返回一个空的指针
    std::shared_ptr<void> tmp = weak_cond.lock();
    // 只要不空，就执行回调
    if (tmp) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb
        , std::weak_ptr<void> weak_cond, bool recurring) {
    // 在定时器触发时会调用 OnTimer 函数，并在OnTimer函数中判断条件对象是否存在，如果存在则调用回调函数cb。
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}
                    
uint64_t TimerManager::getNextTimer() {
    std::shared_lock lock(m_mutex);
    // 不触发 onTimerInsertedAtFront
    m_tickled = false;
    // 如果没有定时器，返回一个最大值
    if (m_timers.empty()) {
        return ~0ull;
    }
    // 获取当前最接近的一个timer
    const Timer::ptr& next = *m_timers.begin();
    // 现在的时间
    uint64_t now_ms = GetCurrentMs();
    // 如果执行时间已经结束了，就返回0
    if (now_ms >= next->m_next) {
        return 0;
    } else {
        // 还没超时，返回需要执行多久
        return next->m_next - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs) {
    // 获取当前的时间
    uint64_t now_ms = GetCurrentMs();
    std::vector<Timer::ptr> expired;
    {
        std::shared_lock lock(m_mutex);
        if (m_timers.empty()) {
            return ;
        }
    }
    std::unique_lock lock(m_mutex);
    if (m_timers.empty()) {
        return ;
    }
    // 是否服务器的时间被调后了
    bool rollover = detectClockRollover(now_ms);
    // 如果服务器时间没问题，并且第一个定时器都没有到执行时间，就说明没有任务需要执行
    if (!rollover && ((*m_timers.begin())->m_next > now_ms)) {
        return ;
    }

    // 定义一个当前时间的定时器
    Timer::ptr now_timer(new Timer(now_ms));
    /* 如果服务器的时间被调后了，就把这些“错误”的定时器放到新的定时器里，让直接把原来的m_timers全删了
       如果时间没有错误，那么就把当前时间之前的定时器都放到新的定时器的最前面，然后把m_timers在当前时间之前的都删了 */
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    while (it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }

    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    cbs.reserve(expired.size());

    // 枚举新的定时器列表，将它们的回调函数都添加到cbs里
    for (auto& timer : expired) {
        cbs.push_back(timer->m_cb);
        // 如果设置了循环定时器，那么相同的时间间隔就再次插入相同的cb
        if (timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } else { /* 否则就直接将回调函数注销就行 */
            timer->m_cb = nullptr;
        }
    }
}

bool TimerManager::hasTimer() {
    std::shared_lock lock(m_mutex);
    return m_timers.empty();
}

void TimerManager::cancelAllTimers() {
    std::unique_lock lock(m_mutex);
    m_timers.clear();
}

void TimerManager::addTimer(Timer::ptr val, std::unique_lock<std::shared_mutex>& lock) {
    // 添加到set中，同时拿到添加的位置
    auto it = m_timers.insert(val).first;
    // 如果该定时器是超时时间最短，并且没有设置触发onTimerInsertedAtFront
    bool at_front = (it == m_timers.begin() && !m_tickled);
    // 设置触发onTimerInsertedAtFront
    if (at_front) {
        m_tickled = true;
    }
    lock.unlock();

    /* 触发onTimerInsertedAtFront()
	 * onTimerInsertedAtFront()在IOManager中就是做了一次tickle()的操作 */
    if (at_front) {
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    // 如果相差1个小时，说明时间被调后了
    if (now_ms < m_previouseTime
            && now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}

}
