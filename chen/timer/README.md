# Timer — 定时器

基于时间堆的毫秒级定时器，集成在 IOManager 中。

## 使用示例

```cpp
// 通过 IOManager 创建定时器（推荐）
auto iom = IOManager::GetThis();

// 一次性定时器
Timer::ptr t1 = iom->addTimer(1000, []() {
    INFO(LOG_ROOT()) << "1s later";
}, false);

// 循环定时器
Timer::ptr t2 = iom->addTimer(5000, []() {
    INFO(LOG_ROOT()) << "every 5s";
}, true);

// 条件定时器（对象销毁后不再触发）
Timer::ptr t3 = iom->addConditionTimer(3000, []() {
    INFO(LOG_ROOT()) << "3s, if obj still alive";
}, std::weak_ptr<SomeObject>(obj), false);

t1->cancel();  // 取消定时器
t2->refresh(); // 重置计时
```

## 注意

**不要直接使用 `TimerManager`**——它没有事件循环，无法驱动定时器到期。定时器必须通过 `IOManager::addTimer()` 创建，由 IOManager 的 idle fiber 在 `epoll_wait` 超时后统一触发到期回调。

## 核心类

- **Timer** — 定时器对象，存储回调、间隔、到期时间
- **TimerManager** — 定时器管理器，维护 `std::set<Timer>` 时间堆

## 特性

- 一次性 / 循环定时器
- 条件定时器（`addConditionTimer`）：通过 `weak_ptr` 检查条件对象存活
- 自动检测系统时间回拨（超过 1 小时）
- 支持 `cancel()` / `refresh()` / `reset()`

## 关键方法

| 方法 | 说明 |
|------|------|
| `addTimer(ms, cb, recurring)` | 添加定时器 |
| `addConditionTimer(ms, cb, weak_cond, recurring)` | 添加条件定时器 |
| `cancel()` | 取消定时器，从集合移除并清空回调 |
| `refresh()` | 刷新（重新定时） |
| `reset(ms, from_now)` | 重新设置间隔 |
| `getNextTimer()` | 获取最近到期时间 |
| `listExpiredCb(cbs)` | 取出所有到期回调 |

## 与 IOManager 的集成

IOManager 的 idle fiber 在 `epoll_wait` 超时后调用 `listExpiredCb`，将到期回调调度执行。`onTimerInsertedAtFront` 回调在更早定时器插入时主动 tickle 唤醒。
