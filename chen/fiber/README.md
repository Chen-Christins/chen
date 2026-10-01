# Fiber — 协程

基于 Boost.Context `jump_fcontext` 实现的用户态协程。

## 使用示例

```cpp
// 创建一个协程
Fiber::ptr fiber(new Fiber([]() {
    INFO(LOG_ROOT()) << "in fiber";
    Fiber::YieldToHold();           // 让出执行权
    INFO(LOG_ROOT()) << "back in fiber";
}));

fiber->swapIn();                    // 切入协程 → 打印 "in fiber"
// 此时 fiber 在 HOLD 状态

fiber->swapIn();                    // 再次切入 → 打印 "back in fiber"
```

框架内部由 Scheduler 自动管理协程切换，**应用层代码不要直接创建 Fiber 或调用 swapIn/swapOut**，应通过 `IOManager::schedule()` 提交任务。

## 核心类

- **Fiber** — 协程对象，独立栈空间（默认 128KB），支持 `swapIn`/`swapOut`/`yield` 操作
- 状态机：`INIT → EXEC → HOLD / READY / TERM / EXCEPT`
- 线程局部指针 `t_fiber` 追踪当前执行中的协程
- `t_threadFiber` 持有线程主协程的 shared_ptr

## 关键方法

| 方法 | 说明 |
|------|------|
| `swapIn()` | 切入当前协程执行 |
| `swapOut()` | 切回主协程 |
| `YieldToReady()` | 让出执行权，状态置为 READY，重新加入调度队列 |
| `YieldToHold()` | 让出执行权，状态置为 HOLD，等待事件唤醒 |
| `GetThis()` | 获取当前线程正在执行的协程 |
| `GetFiberId()` | 获取当前协程 ID |

## 生命周期

构造函数分配栈空间 → `MainFunc` 执行 `m_task` → 任务完成设置 `TERM` → 析构释放栈空间

## 析构安全

`~Fiber()` 中打 DEBUG 日志前清空 `t_fiber`，避免日志框架通过 `GetFiberId()` 访问已释放的协程对象。
