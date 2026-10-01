# Scheduler — 协程调度器

M:N 协程调度器，将协程任务分发到线程池执行。

## 使用示例

```cpp
// 单线程调度器（当前线程也参与执行）
Scheduler sc(1, true, "worker");

sc.schedule([]() {
    INFO(LOG_ROOT()) << "task 1";
});
sc.schedule([]{ INFO(LOG_ROOT()) << "task 2"; },
            []{ INFO(LOG_ROOT()) << "task 3"; });

// 通常不直接使用 Scheduler，而是使用继承它的 IOManager
```

## 注意

`IOManager`（继承 Scheduler）是生产环境推荐使用的调度器，它额外提供 epoll 事件驱动和定时器能力。
直接使用 `Scheduler` 只能执行裸任务，无法利用 Hook 的 fiber 异步 I/O 和 Timer。

## 核心类

- **Scheduler** — 调度器基类，管理任务队列和线程池
- **SchedulerSwitcher** — RAII 调度器切换器

## 设计

```
任务队列 (m_fibers) → 线程池 pick 任务 → swapIn 执行 → swapOut 归还
                                         ↓
                              idle fiber → 无任务时睡眠等待 tickle
```

- `run()` 是调度器主循环，每个工作线程执行
- `use_caller=true` 时将当前线程也作为工作线程，创建 root fiber 执行 `run()`
- `tickle()` 通知空闲线程有新任务到达
- `stopping()` 检查 `m_autoStop && m_stopping && 队列空 && 活跃线程数==0`

## 关键方法

| 方法 | 说明 |
|------|------|
| `start()` | 创建线程池并启动 |
| `stop()` | 设置停止标志，唤醒线程，join 等待退出 |
| `schedule(fiber/cb)` | 将任务加入队列 |
| `tickle()` | 唤醒空闲线程 |

## 退出安全

`run()` 的 while 循环退出后，调用 `Fiber::SetThis(t_schedule_fiber)` 将 `t_fiber` 重置为线程主协程，避免局部 `idle_fiber` 析构时悬垂指针。
