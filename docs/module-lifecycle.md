# Module Lifecycle: Graceful Shutdown & Hot Reload

框架提供 nginx 级别的优雅关闭和零停机热重载。业务模块只需关心数据持久化和资源释放，
连接管理、事件清理、定时器兜底由框架处理。

## 生命周期方法总览

```
启动:          onLoad() → onServerReady() → onServerUp() → [onTick()...]
                                                         ↓
关机:          onDeactivate() → onUnload() → dlclose()

热重载(新模块): onLoad() → onServerReady() → onServerUp()
热重载(旧模块):                  onDeactivate() → dlclose()
```

---

## 热重载时序

```
1. dlopen 新 .so → CreateModule() → onLoad()
2. onServerReady()     ← 新模块: clearAll() + 注册 servlet/handler/EventBus
3. commitDispatch()    ← 框架: 切换流量到新 handler（原子操作，无 404 窗口）
4. onServerUp()        ← 新模块: 感知 server 开始 accept
5. onDeactivate()      ← 旧模块: stop 定时器, flush 脏数据（不要清 EventBus）
6. DestroyModule()     ← dlclose 旧 .so
```

**关键**: 步骤 3 之后新请求即刻由新代码处理。旧模块的 in-flight 请求在步骤 5 之前自然完成。
整个过程中**不存在 404 窗口**。

### 为什么 clearAll 在 onServerReady 开头而不是 onDeactivate？

EventBus 的 listener 是 `std::function`，内部通过 type-erasure 存储函数指针。
这些函数指针指向模块 `.so` 中的代码。如果在旧模块 `onDeactivate()` 中调 `clearAll()`，
新模块刚注册的 listener 也会被清掉（新 .so 中的函数指针没问题，但功能丢失了）。

正确做法：新模块 `onServerReady()` 开头调 `clearAll()`——此时旧 `.so` 还未 dlclose，
旧 listener 的函数指针仍然有效，销毁安全。之后注册新 listener，dlclose 后只有新 listener 存活。

---

## 优雅关闭时序

```
1. server->stop()           ← 停止 accept, 不再接收新连接
2. drain 3s                 ← 现存连接自然完成
3. 取消 tick 定时器         ← 框架取消所有模块的 onTick
4. 停止 tick IOManager
5. onDeactivate()           ← flush 脏数据
6. onUnload()               ← clearAll() EventBus + stop 定时器 + 释放 DB/Redis 资源
7. 停止 WorkerMgr           ← cancelAllTimers() + 停止 io/accept 线程
8. 清理 pid 文件 → 进程退出  ← FoxThreadMgr 由静态析构自动 stop/join
```

> 框架不再代为释放 Redis 或停止 FoxThreadMgr：使用 DB/Redis 的模块需在自己的
> `onUnload()` 中释放连接（如 `RedisMgr::GetInstance()->freeAll()`）。详见
> [`docs/db-configuration.md`](db-configuration.md)。

---

## 各方法职责速查

### `bool onLoad()`

- 何时调用: 模块首次加载（启动 / dlopen）
- 职责: 初始化模块内部状态（配置检查等）
- **不要**注册 servlet/handler（此时 server 未创建）
- **不要**连接数据库（在 onServerReady 里做）

### `bool onServerReady()`

- 何时调用: server 创建完毕、绑定端口后
- 职责（按顺序）:
  1. `clearAll()` — 清除旧模块遗留的 EventBus listener
  2. 连接数据库
  3. 注册 HTTP servlet / WS servlet / RPC handler
  4. 初始化 EventBus listener
- 注意: 热重载时此方法会在 `commitDispatch()` 之前被调用，
  注册的 handler/servlet 必须是**新 .so 中的代码**

### `bool onServerUp()`

- 何时调用: server 启动后，开始 accept 时
- 职责: 启动后台定时器、开始周期性任务
- 通常不需要做额外操作（tick 定时器由框架管理）

### `void onTick()`

- 何时调用: 每 `getTickIntervalMs()` 毫秒一次
- 职责: 定时发布任务、缓存刷新等周期性操作
- 注意: 热重载后框架自动为新模块重建 tick 定时器

### `bool onDeactivate()`

- 何时调用:
  - 热重载: 旧模块被替换时
  - 关机: 所有模块的 onUnload 之前
- 职责:
  - 停止后台定时器
  - flush 脏数据到 DB/Redis
  - **不要**清空 EventBus（由新模块 onServerReady 处理）
  - **不要**关闭 WebSocket 连接（框架的 drain 机制处理）

### `bool onUnload()`

- 何时调用: 仅在优雅关闭时（热重载不调此方法）
- 职责:
  - `clearAll()` — 清除 EventBus listener（.so 尚未 dlclose，安全）
  - 保存脏数据
  - 停止定时器
  - 释放资源，包括 DB/Redis 连接（如 `RedisMgr::GetInstance()->freeAll()`）

---

## EventBus 生命周期（⚠️ 重要）

EventBus 是全局单例，listener 中的 `std::function` 通过 type-erasure 存储函数指针，
这些指针指向模块 `.so` 中的代码。`dlclose()` 后指针悬空，销毁会 segfault。

### 安全规则

| 时机 | 调用 `clearAll()` | 原因 |
|------|-------------------|------|
| `onServerReady()` 开头 | ✅ 安全 | 旧 .so 还未 dlclose，旧 listener 的函数指针有效 |
| `onDeactivate()` | ❌ 不要调 | 会清掉新模块刚注册的 listener |
| `onUnload()` | ✅ 安全 | 仅关机时调用，.so 尚未 dlclose |
| dlclose 之后 | ❌ 崩溃 | 函数指针悬空，销毁 std::function → SIGSEGV |

### 热重载时 EventBus 变化

```
v1.0 运行中:    EventBus = [v1.0 listeners]

热重载 v1.0 → v2.0:
  1. v2.0 onServerReady: clearAll() → 注册 v2.0 listeners
     EventBus = [v2.0 listeners]                     ← v1.0 已清除
  2. v1.0 onDeactivate: stop() only
  3. dlclose(v1.0)                                   ← 安全，v1.0 listener 已不存在

热重载 v2.0 → v3.0:
  1. v3.0 onServerReady: clearAll() → 注册 v3.0 listeners
     EventBus = [v3.0 listeners]                     ← v2.0 已清除
  2. v2.0 onDeactivate: stop() only
  3. dlclose(v2.0)                                   ← 安全
```

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
// 框架的 commitDispatch 会原子切换
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

### Q: 为什么 `onUnload` 不在热重载时调用？

A: 热重载时旧模块通过 `onDeactivate()` 清理资源后直接 dlclose。
`onUnload()` 仅在关机时调用，因为关机后不需要保留模块注册。

### Q: 热重载时会丢数据吗？

A: 不会丢 DB/Redis 中的数据。但**进程堆内存中的数据**（如 `std::map` 缓存的浏览计数）
在旧模块 dlclose 后会丢失。解决方式: 要么放到 Redis，要么在 `onDeactivate` 中 flush 到 DB。

### Q: cancelAllTimers 会取消我的业务定时器吗？

A: `cancelAllTimers()` 只在 WorkerMgr::stop() 阶段调用（关机步骤 9），
此时你的 `onDeactivate` 已经完成了所有清理。框架清的是 `do_io` 创建的内部
条件超时器，不是你的业务 tick。

### Q: 无限热重载安全吗？

A: 安全。每次热重载时新模块 `onServerReady()` 开头调 `clearAll()` 清除所有旧 listener，
然后注册新 listener。dlclose 时 EventBus 中只有新模块的 listener，不会累积悬空指针。
已验证 3+ 次连续热重载 + 关机均无 segfault。
