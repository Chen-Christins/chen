# Module Lifecycle: Graceful Shutdown & Hot Reload

框架提供 nginx 级别的优雅关闭和零停机热重载。业务模块只需关心数据持久化和资源释放，
连接管理、事件清理、定时器兜底由框架处理。

热重载采用 **master/worker 多进程模型**：master 进程持有监听端口并监督 worker，
每个 worker 进程只加载一个版本的模块 `.so`。重载时新 worker 加载新模块，旧 worker
停止 accept、限时 drain 完遗留连接后退出。进程退出时由内核回收地址空间，**不做进程内
`dlclose`**，因此不存在旧 `.so` 函数指针悬空的问题。

## 生命周期方法总览

```
启动: onLoad() → onServerReady() → onServerUp() → [onTick()...]
                                                     ↓
关机: onDeactivate() → onUnload()

重载(新 worker): onLoad() → onServerReady() → onServerUp()
重载(旧 worker):                onDeactivate() → onUnload() → 进程退出
```

---

## 热重载时序

```
1. master fork 新 worker
2. 新 worker: CreateModule() → onLoad()
3. 新 worker: onServerReady()   ← 注册 servlet/handler/EventBus
4. 新 worker: onServerUp()      ← 开始 accept
5. master 通知旧 worker 停止 accept
6. 旧 worker: 限时 drain 遗留连接
7. 旧 worker: onDeactivate() → onUnload() → 退出
```

**关键**: 步骤 4 之后新请求即由新进程处理；旧进程的 in-flight 请求在步骤 7 之前自然完成。
整个过程中**不存在 404 窗口**，master 的监听 fd 在整个过程中保持打开。

### 内存态说明

新 worker 是全新进程，堆内状态（内存缓存、内存 session 等）会重置。需要跨重载保留的
数据应写入 Redis/MySQL，或在旧 worker 的 `onDeactivate()` 中 flush。

---

## 优雅关闭时序

```
1. server->stop()           ← 停止 accept, 不再接收新连接
2. drain (server.drain_timeout_ms) ← 现存连接自然完成
3. 取消 tick 定时器         ← 框架取消所有模块的 onTick
4. 停止 tick IOManager
5. onDeactivate()           ← flush 脏数据
6. onUnload()               ← clearAll EventBus + stop 定时器 + 释放 DB/Redis 资源
7. 停止 WorkerMgr           ← 停止 io/accept 线程
8. 进程退出                  ← 地址空间回收
```

> 框架不再代为释放 Redis：使用 DB/Redis 的模块需在自己的 `onUnload()` 中释放连接
> （如 `RedisMgr::GetInstance()->freeAll()`）。详见 [`docs/db-configuration.md`](db-configuration.md)。

---

## 各方法职责速查

### `bool onLoad()`

- 何时调用: 模块首次加载（worker 进程启动 / dlopen）
- 职责: 初始化模块内部状态（配置检查等）
- **不要**注册 servlet/handler（此时 server 未创建）
- **不要**连接数据库（在 onServerReady 里做）

### `bool onServerReady()`

- 何时调用: server 创建完毕、绑定端口后
- 职责（按顺序）:
  1. `clearAll()` — 清除 EventBus listener（保持清理习惯）
  2. 连接数据库
  3. 注册 HTTP servlet / WS servlet / RPC handler
  4. 初始化 EventBus listener
- 注意: 注册的 handler/servlet 必须是**本进程加载的 `.so` 中的代码**

### `bool onServerUp()`

- 何时调用: server 启动后，开始 accept 时
- 职责: 启动后台定时器、开始周期性任务
- 通常不需要做额外操作（tick 定时器由框架管理）

### `void onTick()`

- 何时调用: 每 `getTickIntervalMs()` 毫秒一次
- 职责: 定时发布任务、缓存刷新等周期性操作

### `bool onDeactivate()`

- 何时调用:
  - 重载: 旧 worker 停止 accept、drain 完成后
  - 关机: 所有模块的 onUnload 之前
- 职责:
  - 停止后台定时器
  - flush 脏数据到 DB/Redis
  - **不要**关闭 WebSocket 连接（框架的 drain 机制处理）

### `bool onUnload()`

- 何时调用: worker 进程退出前（重载旧 worker / 关机均会调用）
- 职责:
  - `clearAll()` — 清除 EventBus listener
  - 保存脏数据
  - 停止定时器
  - 释放资源，包括 DB/Redis 连接（如 `RedisMgr::GetInstance()->freeAll()`）

---

## EventBus 生命周期

EventBus 是**进程内单例**，listener 中的 `std::function` 存储指向模块 `.so` 中代码的
函数指针。多进程模型下，每个 worker 拥有独立的 EventBus 实例，随进程退出（地址空间
回收）一并销毁，因此不存在跨版本的重载悬空指针问题。

仍建议：

| 时机 | 调用 `clearAll()` | 说明 |
|------|-------------------|------|
| `onServerReady()` 开头 | 建议 | 保证本进程 EventBus 干净 |
| `onUnload()` | 建议 | 进程退出前清理资源 |

---

## 不同类型的 Server 注意事项

### HTTP Server

```cpp
// onServerReady:
dp->addServlet("/api/v1/user/info", new UserInfoServlet());
// addServlet 会原子覆盖同路径旧 servlet，无需先 clear()
```

### WebSocket Server

```cpp
// onDeactivate / onUnload:
closeAllConnections();  // 主动关闭所有 WS，客户端自动重连
```

### RPC Server

```cpp
// onServerReady 时重新 registerMethod 即可
```

---

## 数据持久化最佳实践

### 实时数据 → Redis

```cpp
// 每次更新直接写 Redis，reload 时数据不丢失
RedisMgr::GetInstance()->hset("article:" + id, "views", count);
```

### 脏数据缓存 → 定时 flush

```cpp
// onTick: 累加到内存缓存
// onDeactivate: 最后一次 flush
void ArticleMgr::stop() {
    flushDirtyData();       // 写入 MySQL
    cancelTimers();         // 停止 onTimer/onUpdateTimer
}
```

---

## 常见问题

### Q: 热重载时会丢数据吗？

A: 不会丢 DB/Redis 中的数据。但**新 worker 是全新进程，堆内存状态会重置**。需要保留
的数据要么放到 Redis，要么在旧 worker 的 `onDeactivate` 中 flush 到 DB。

### Q: 热重载后旧连接会立即断开吗？

A: 不会。master 让旧 worker 停止 accept 后，旧 worker 会限时（`server.drain_timeout_ms`）
等待现存连接自然完成，超时后才强制关闭并退出。

### Q: 无限热重载安全吗？

A: 安全。每个新 worker 都是独立进程，模块 `.so` 不共享、不 `dlclose`，无悬空指针累积。
