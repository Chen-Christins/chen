# IOManager — I/O 调度器

基于 epoll 的 I/O 事件调度器，继承自 Scheduler。整个框架的事件循环核心。

## 使用示例

```cpp
// 创建一个 2 线程的 IOManager（use_caller 默认 true）
IOManager iom(2);

// 调度任务（会在任意工作线程的 fiber 中执行）
iom.schedule([]() {
    INFO(LOG_ROOT()) << "hello from IOManager fiber";
});

// 添加定时器
iom.addTimer(1000, []() {
    INFO(LOG_ROOT()) << "one-shot timer fired";
}, false);  // recurring=false

iom.addTimer(5000, []() {
    INFO(LOG_ROOT()) << "recurring timer";
}, true);   // recurring=true

// IOManager 通常作为长期存在的对象，main 函数退出时析构
```

## 工作原理

```
epoll_wait (阻塞等待) → 事件就绪 → 触发对应 Fiber/Callback
             ↑
    tickle pipe 写入 (唤醒 epoll_wait)
```

- idle fiber 中调用 `epoll_wait` 阻塞等待 I/O 事件
- 通过 pipe（`m_tickleFds`）实现自我唤醒：有任务时 write 一个字节到 pipe
- 每个 fd 通过 `FdContext` 管理 READ/WRITE 事件及其挂载的 fiber/callback

## 核心类

- **IOManager** — 继承 Scheduler，epoll 事件循环
- **FdContext** — 文件描述符上下文，管理注册的事件和回调
- **EventContext** — 单个事件的上下文（scheduler + fiber + callback）
- **WorkerGroup** — 批量任务调度 + 等待完成，从 `chen/worker.h` 移入
- **WorkerManager** — 多命名调度器管理（`WorkerMgr` 单例）

## 关键方法

| 方法 | 说明 |
|------|------|
| `addEvent(fd, event, cb)` | 为 fd 注册事件和回调 |
| `delEvent(fd, event)` | 删除事件 |
| `cancelEvent(fd, event)` | 取消事件并触发回调 |
| `cancelAll(fd)` | 取消 fd 上所有事件 |
| `tickle()` | 通过 pipe 唤醒 epoll_wait |
| `idle()` | idle fiber 主循环：epoll_wait → 处理就绪事件 → 触发超时定时器 |

## WorkerGroup — 批量任务

```cpp
// 并发执行一批任务，等待全部完成
WorkerGroup::ptr wg = WorkerGroup::Create(4);  // 最多 4 个并发
for (auto& task : tasks) {
    wg->schedule([&task]() { process(task); });
}
wg->waitAll();  // 阻塞当前 fiber 直到全部完成
```

## WorkerManager — 多调度器管理

```cpp
// 配置多个命名调度器（例如 IO 线程池、计算线程池）
WorkerMgr::GetInstance()->init({
    {"io",       {{"thread_num", "4"}, {"type", "io"}}},
    {"compute",  {{"thread_num", "2"}, {"type", "compute"}}},
});

// 按名称提交任务
WorkerMgr::GetInstance()->schedule("io", []() {
    // 在 IO 线程池执行
});
```

## 配置

- `IOManager(threadCount, use_caller, name)` — 线程数、是否复用当前线程、调度器名称
