# Thread — 线程封装

跨平台线程封装，支持命名、ID 追踪。

## 警告：不要直接使用 Thread 执行异步任务

本项目是**协程框架**，异步任务应通过 `IOManager::schedule()` 提交，由框架的 M:N 调度器在 fiber 中执行。直接创建 Thread 会：

- 失去 fiber 协作式调度的优势（大量线程带来上下文切换开销）
- 丢失 fiber-local 状态（日志、句柄等）
- 无法利用 IOManager 的 epoll 事件驱动能力

```cpp
// ❌ 不要这样：
Thread t([]() {
    do_some_async_work();
}, "async");

// ✅ 应该这样：
IOManager::GetThis()->schedule([]() {
    do_some_async_work();
});
```

`Thread` 模块作为 `Scheduler`/`IOManager` 的内部基础设施存在，应用层代码一般不需要直接使用。

## 核心类

- **Thread** — `std::thread` 的轻量封装（框架内部使用）
- **Semaphore** — 信号量
- **Mutex** — 互斥锁封装

## 使用（框架内部 / 初始化场景）

```cpp
Thread t([]() {
    // 线程初始化逻辑
}, "init_worker");

t.join();
```

## 工具函数

| 函数 | 说明 |
|------|------|
| `Thread::GetThis()` | 获取当前线程的 Thread 对象 |
| `Thread::GetThreadId()` | 获取当前线程 ID |
| `Thread::SetName(name)` | 设置当前线程名称 |
| `Thread::GetName()` | 获取当前线程名称 |
| `GetThreadId()` | 获取当前线程 ID（全局函数） |
