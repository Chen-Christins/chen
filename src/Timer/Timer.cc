#include "Timer.hpp"

#include "../Util/Util.hpp"

namespace chen {

bool Timer::Comparator::operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const
{
    if (!lhs && !rhs) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    if (!rhs) {
        return false;
    }
    if (lhs->next_ < rhs->next_) {
        return true;
    }
    if (lhs->next_ > rhs->next_) {
        return false;
    }
    return lhs.get() < rhs.get();
}

Timer::Timer(uint64_t next)
    : next_(next)
{
}

Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager)
    : recurring_(recurring)
    , ms_(ms)
    , cb_(cb)
    , manager_(manager)
{
    next_ = ms + TimeUtil::GetCurrentMs();
}

bool Timer::cancel()
{
    std::unique_lock lock(manager_->mutex_);

    // 在定时器集合中查找自己（即使 cb_ 已被 listExpiredCb 消费，
    // 循环定时器仍会被重新插入集合，所以需要以集合中的存在性为准）
    auto it = manager_->timers_.find(shared_from_this());
    bool found = (it != manager_->timers_.end());
    if (found) {
        manager_->timers_.erase(it);
    }
    // 清理回调（如果还在的话）
    if (cb_) {
        cb_ = nullptr;
    }
    return found;
}

bool Timer::refresh()
{
    std::unique_lock lock(manager_->mutex_);
    if (!cb_) {
        return false;
    }
    // 找到自己的定时器
    auto it = manager_->timers_.find(shared_from_this());
    if (it == manager_->timers_.end()) {
        return false;
    }
    // 然后删除
    manager_->timers_.erase(it);
    // 更新执行时间
    next_ = TimeUtil::GetCurrentMs() + ms_;
    // 重新插入定时器
    manager_->timers_.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now)
{
    // 如果周期相同，并且不从当前时间开始
    if (ms == ms_ && !from_now) {
        return true;
    }
    std::unique_lock lock(manager_->mutex_);
    if (!cb_) {
        return false;
    }
    // 找到自己的那个定时器, 找不到说明不能重置
    auto it = manager_->timers_.find(shared_from_this());
    if (it == manager_->timers_.end()) {
        return false;
    }
    // 找到了就删除
    manager_->timers_.erase(it);
    uint64_t start = 0;
    if (from_now) {
        start = TimeUtil::GetCurrentMs();
    } else {
        /* 这里的m_next和m_ms都是在本次重置之前的数值，如果不是from_now，
            那么就按照上一次创建的时间节点和间隔时间设定 */
        start = next_ - ms_;
    }
    // 更新当前时间间隔
    ms_ = ms;
    next_ = start + ms_;
    // 然后就添加定时器
    manager_->addTimer(shared_from_this(), lock);
    return true;
}

TimerManager::TimerManager()
{
    previouse_time_ = TimeUtil::GetCurrentMs();
}

TimerManager::~TimerManager()
{
}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
{
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    std::unique_lock lock(mutex_);

    addTimer(timer, lock);
    return timer;
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)
{
    // weak_ptr的lock会返回一个shared_ptr，如果weak_ptr失效了，就会返回一个空的指针
    std::shared_ptr<void> tmp = weak_cond.lock();
    // 只要不空，就执行回调
    if (tmp) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring)
{
    // 在定时器触发时会调用 OnTimer 函数，并在OnTimer函数中判断条件对象是否存在，如果存在则调用回调函数cb。
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

uint64_t TimerManager::getNextTimer()
{
    std::shared_lock lock(mutex_);
    // 不触发 onTimerInsertedAtFront
    tickled_ = false;
    // 如果没有定时器，返回一个最大值
    if (timers_.empty()) {
        return ~0ull;
    }
    // 获取当前最接近的一个timer
    const Timer::ptr& next = *timers_.begin();
    // 现在的时间
    uint64_t now_ms = TimeUtil::GetCurrentMs();
    // 如果执行时间已经结束了，就返回0
    if (now_ms >= next->next_) {
        return 0;
    } else {
        // 还没超时，返回需要执行多久
        return next->next_ - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs)
{
    // 获取当前的时间
    uint64_t now_ms = TimeUtil::GetCurrentMs();
    std::vector<Timer::ptr> expired;
    {
        std::shared_lock lock(mutex_);
        if (timers_.empty()) {
            return;
        }
    }
    std::unique_lock lock(mutex_);
    if (timers_.empty()) {
        return;
    }
    // 是否服务器的时间被调后了
    bool rollover = detectClockRollover(now_ms);
    // 如果服务器时间没问题，并且第一个定时器都没有到执行时间，就说明没有任务需要执行
    if (!rollover && ((*timers_.begin())->next_ > now_ms)) {
        return;
    }

    // 定义一个当前时间的定时器
    Timer::ptr now_timer(new Timer(now_ms));
    /* 如果服务器的时间被调后了，就把这些“错误”的定时器放到新的定时器里，让直接把原来的m_timers全删了
       如果时间没有错误，那么就把当前时间之前的定时器都放到新的定时器的最前面，然后把m_timers在当前时间之前的都删了 */
    auto it = rollover ? timers_.end() : timers_.lower_bound(now_timer);
    while (it != timers_.end() && (*it)->next_ == now_ms) {
        ++it;
    }

    expired.insert(expired.begin(), timers_.begin(), it);
    timers_.erase(timers_.begin(), it);
    cbs.reserve(expired.size());

    // 枚举新的定时器列表，将它们的回调函数都添加到cbs里
    for (auto& timer : expired) {
        cbs.push_back(timer->cb_);
        // 如果设置了循环定时器，那么相同的时间间隔就再次插入相同的cb
        if (timer->recurring_) {
            timer->next_ = now_ms + timer->ms_;
            timers_.insert(timer);
        } else { /* 否则就直接将回调函数注销就行 */
            timer->cb_ = nullptr;
        }
    }
}

bool TimerManager::hasTimer()
{
    std::shared_lock lock(mutex_);
    return timers_.empty();
}

size_t TimerManager::getTimerCount()
{
    std::shared_lock lock(mutex_);
    return timers_.size();
}

void TimerManager::cancelAllTimers()
{
    std::unique_lock lock(mutex_);
    timers_.clear();
}

void TimerManager::addTimer(Timer::ptr val, std::unique_lock<std::shared_mutex>& lock)
{
    // 添加到set中，同时拿到添加的位置
    auto it = timers_.insert(val).first;
    // 如果该定时器是超时时间最短，并且没有设置触发onTimerInsertedAtFront
    bool at_front = (it == timers_.begin() && !tickled_);
    // 设置触发onTimerInsertedAtFront
    if (at_front) {
        tickled_ = true;
    }
    lock.unlock();

    /* 触发onTimerInsertedAtFront()
     * onTimerInsertedAtFront()在IOManager中就是做了一次tickle()的操作 */
    if (at_front) {
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover(uint64_t now_ms)
{
    bool rollover = false;
    // 如果相差1个小时，说明时间被调后了
    if (now_ms < previouse_time_ && now_ms < (previouse_time_ - 60 * 60 * 1000)) {
        rollover = true;
    }
    previouse_time_ = now_ms;
    return rollover;
}

} // namespace chen
