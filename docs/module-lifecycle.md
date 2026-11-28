# Module Lifecycle: Graceful Shutdown & Hot Reload

框架提供 nginx 级别的优雅关闭和零停机热重载。业务模块只需关心数据持久化和资源释放，
连接管理、事件清理、定时器兜底由框架处理。

热重载采用 **master/worker 多进程模型**：master 进程持有监听端口并监督 worker，
每个 worker 进程只加载一个版本的模块 `.so`。重载时新 worker 加载新模块，旧 worker
停止 accept、等待在途 HTTP/1.1 / HTTP/2 请求处理完（超时强制关闭）后退出。进程退出时由内核回收地址空间， **不做进程内
`dlclose`**，因此不存在旧 `.so` 函数指针悬空的问题。

## 进程模型

```
                 ┌──────────────────────────────┐
                 │            master            │
                 │  绑定监听 socket（持有 fd）  │
                 │  写 pidfile / 处理信号       │
                 └───────────────┬──────────────┘
                                 │ fork() 继承监听 fd
                 ┌───────────────┴───────────────┐
                 │                               │
          ┌──────▼───────┐               ┌───────▼──────┐
          │  worker v1   │   SIGHUP →    │  worker v2   │
          │  加载 v1 .so │  旧 worker    │  加载 v2 .so │
          │  处理连接    │  drain 后退出 │  接管新连接  │
          └──────────────┘               └──────────────┘
```

- **master**：单线程，不加载模块。`setupServers()` 绑定全部监听 socket 后 `fork()` 出 worker；
  收到 `SIGHUP` 时 fork 新 worker，就绪后向旧 worker 发 `SIGQUIT`；收到 `SIGTERM/SIGINT` 时
  停止全部 worker；worker 非预期崩溃时自动重启。
- **worker**：`fork` 自 master，继承监听 fd；`dlopen` 磁盘上的模块，注入 worker 线程池后
  `start()`。收到 `SIGQUIT/SIGTERM` 后停止 accept、限时 drain、`onDeactivate/onUnload`、退出。
- 监听 socket 仅由 master 绑定一次，worker 通过继承获得；旧 worker 关闭自己的 fd 副本
  不影响 master 与新 worker。

### 相关配置（`server:` 段）

| 配置项                       | 默认  | 说明                                                       |
|------------------------------|-------|------------------------------------------------------------|
| `drain_timeout_ms`           | 3000  | 旧 worker 停止 accept 后等待在途请求的毫秒数，超时强制关闭 |
| `worker_ready_timeout_ms`    | 30000 | master 等待新 worker 就绪的超时毫秒数                      |
| `worker_restart_interval_ms` | 2000  | worker 崩溃后 master 重启前的等待毫秒数                    |
| `housekeeping_interval_ms`   | 500   | worker 检查关闭信号的定时器间隔                            |

---

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
整个过程中 **不存在 404 窗口**，master 的监听 fd 在整个过程中保持打开。

### 内存态说明

新 worker 是全新进程，堆内状态（内存缓存、内存 session 等）会重置。需要跨重载保留的
数据应写入 Redis/MySQL，或在旧 worker 的 `onDeactivate()` 中 flush。

---

## 模块 `.so` 替换部署

「替换模块 `.so` 再 reload」在机制上是支持的，但 **必须原子替换**。

### 必须用 rename（原子替换）

- ✅ 安全：`mv new.so libblog.so` / `install new.so libblog.so`（`rename` 换 inode）。
  旧 worker 持有旧 inode 的映射，新 worker 打开新 inode，二者互不影响。
- ❌ 不安全：`cp new.so libblog.so` 这类 **原地覆盖**（truncate + 重写同一 inode）。
  旧 worker 在 drain 窗口内仍然存活，而它的 `.so` 是 mmap 的可执行代码页；原地重写会让
  这些页变成半截/不一致，可能导致旧 worker `SIGBUS`/`SIGSEGV` 崩溃。

> 结论：用 `install`/`mv` 覆盖， **不要**用 `cp`；文件名不需要带版本号，同名替换即可。

### 触发方式

换 so 后需要发 `SIGHUP` 触发重载：

```bash
bin/server -s reload
```

`FileWatcher` 只监听配置目录的 `.yml`/`.xml`， **不监听模块目录**，换 so 不会自动 reload。

### 临时文件命名

`ModuleManager::init()` 会跳过点号开头的隐藏文件，但 **不跳过** `.so` 结尾的文件。因此
临时文件不要以 `.so` 结尾（如 `libblog.tmp.so` 会被当作模块加载），应放在模块目录之外，
或使用点号前缀（如 `.libblog.so.tmp`）并确保最终以 rename 就位。

### 与 libchen 的 ABI

模块在链接时把 `libchen` 的 **SONAME** 写进自己的 `DT_NEEDED`（当前是 `libchen.so.1`，
由 `CMakeLists.txt` 的 `SOVERSION` 决定），而不是写死完整版本文件名。`lib/` 下的布局是：

```
libchen.so          -> libchen.so.1        # 开发/链接用（CMake NAMELINK）
libchen.so.1        -> libchen.so.1.6.0    # SONAME，运行时真正被查找的名字
libchen.so.1.6.0                           # 实际文件，版本号 = project(VERSION)
```

由此得到两条规则：

- **minor / patch 升级**（例如 1.6.0 → 1.7.0，major 不变）：SONAME 仍是 `libchen.so.1`，
  已编译的模块不需要重编，直接替换 `lib/` 下的库即可；也正因为如此，新增/修改框架接口
  时**不要**删除已有导出符号，只能新增重载或改内部实现。
- **major 升级**（破坏 ABI，例如删改既有导出符号或类布局）：提升 `project(chen VERSION x.y.z)`
  的主版本号，SONAME 变成 `libchen.so.2`，此时**必须**用新库重编所有模块——旧模块仍会去
  找 `libchen.so.1`，加载失败是预期行为（而不是静默错配）。

排查用：

```bash
readelf -d lib/libchen.so | grep SONAME        # 库自己的 SONAME
readelf -d bin/module/libblog.so | grep NEEDED # 模块依赖的库名
```

各 worker 使用其加载时刻的库映射；**只替换模块、libchen 不变**时没有问题；若同时改动了
`libchen` 的对外接口，需要连同 `libchen` 一起升级。

---

## 优雅关闭时序

```
1. server->stop()           ← 停止 accept, 不再接收新连接
2. drain (server.drain_timeout_ms) ← 关闭空闲连接，等在途请求自然完成
3. 取消 tick 定时器         ← 框架取消所有模块的 onTick（定时器挂在 main IOManager 上）
4. onDeactivate()           ← flush 脏数据
5. onUnload()               ← clearAll EventBus + stop 定时器 + 释放 DB/Redis 资源
6. 停止 WorkerMgr           ← 停止 io/accept 线程
7. 进程退出                  ← 地址空间回收
```

### 各协议的 drain 边界

所有内置 server 都实现了协作式 drain（`supportsGracefulDrain()` 返回 true），区别只在于
「什么算一次在途请求」：

| server                          | 在途请求边界                                        | draining 时的收尾动作                                                                                   |
|---------------------------------|-----------------------------------------------------|---------------------------------------------------------------------------------------------------------|
| `HttpServer`                    | 一次 HTTP 请求（含响应发送）；HTTP/2 以整个会话计入 | H1 处理完当前请求后不再读下一个 keepalive 请求；H2 先发 GOAWAY，等在途 stream 结束后关连接              |
| `WSServer`                      | 一条 WebSocket 消息的处理                           | 处理完当前消息后回 CLOSE 帧再关连接；空闲会话由 `onDrain()` 发 CLOSE 帧，再由 `closeIdleClients()` 关闭 |
| `RpcServer`                     | 一次 RPC 调用（含回包）                             | 回包后关闭连接；relay 连接同样在当帧处理完后退出                                                        |
| `RockServer`                    | 一次 `RockRequest`/`RockNotify` 的处理 + 回包       | 回包后关闭连接（`RockStream::setRequestScopeFactory()` 把请求作用域挂到 `TcpServer::RequestGuard`）     |
| `GenericProtocolServer`（game） | 一个协议包的处理                                    | 处理完当前包后关闭连接                                                                                  |

未覆写 `supportsGracefulDrain()` 的自定义 server 仍走旧路径：`closeAllClients()` 立即断开，
不等待在途请求。drain 的超时由 `server.drain_timeout_ms` 控制，超时后一律强关。

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
- 注意: 注册的 handler/servlet 必须是 **本进程加载的 `.so` 中的代码**

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

EventBus 是 **进程内单例**，listener 中的 `std::function` 存储指向模块 `.so` 中代码的
函数指针。多进程模型下，每个 worker 拥有独立的 EventBus 实例，随进程退出（地址空间
回收）一并销毁，因此不存在跨版本的重载悬空指针问题。

仍建议：

| 时机                   | 调用 `clearAll()` | 说明                     |
|------------------------|-------------------|--------------------------|
| `onServerReady()` 开头 | 建议              | 保证本进程 EventBus 干净 |
| `onUnload()`           | 建议              | 进程退出前清理资源       |

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

A: 不会丢 DB/Redis 中的数据。但 **新 worker 是全新进程，堆内存状态会重置**。需要保留
的数据要么放到 Redis，要么在旧 worker 的 `onDeactivate` 中 flush 到 DB。

### Q: 热重载后旧连接会立即断开吗？

A: 不会。master 让旧 worker 停止 accept 后，旧 worker 会限时（`server.drain_timeout_ms`）
等待现存连接自然完成，超时后才强制关闭并退出。

### Q: 无限热重载安全吗？

A: 安全。每个新 worker 都是独立进程，模块 `.so` 不共享、不 `dlclose`，无悬空指针累积。
