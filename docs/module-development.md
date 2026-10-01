# Module 开发指南：生命周期方法

框架不要求继承任何特定方法——全部有默认实现。但要让模块正常工作，需要按业务类型选择性继承。

## 方法分类

### 必须继承

没有纯虚函数。但要处理请求，至少要实现这一个：

| 方法 | 做什么 |
|------|--------|
| `onServerReady()` | 注册 servlet/handler/EventBus。不注册 = 模块不处理任何请求 |

### 强烈建议

| 方法 | 不实现的后果 |
|------|-------------|
| `onUnload()` | 关闭时 EventBus listener 悬空（segfault 风险）、脏数据不 flush |
| `onLoad()` | 大部分模块需要初始化内部状态，默认只返回 true |

### 按需继承

| 方法 | 什么时候需要 |
|------|-------------|
| `onTick()` + `getTickIntervalMs()` | 定时任务（定时发布文章、定期 flush 缓存） |
| `onDeactivate()` | 热重载时有定时器要停、脏数据要 flush |
| `onServerUp()` | 需要感知 server 开始 accept 的时刻 |
| `onConnect(stream)` | 基于 Rock 协议的连接就绪回调 |
| `onDisconnect(stream)` | 基于 Rock 协议的连接断开回调 |
| `onBeforeArgsParse(argc, argv)` | 需要在参数解析前介入命令行参数 |
| `onAfterArgsParse(argc, argv)` | 需要在参数解析后介入命令行参数 |

---

## 典型模块继承模式

### 纯 HTTP 模块（只有 REST API）

```cpp
class MyModule : public chen::Module {
public:
    bool onLoad() override {
        return true;
    }

    bool onServerReady() override {
        // 清除旧模块 EventBus listener
        chen::EventBusMgr::GetInstance()->clearAll();

        chen::Application::GetInstance()->getServer("http", m_servers);
        for (auto& s : m_servers) {
            auto hs = std::dynamic_pointer_cast<chen::http::HttpServer>(s);
            hs->getServletDispatch()->addServlet("/api/v1/foo", new FooServlet());
        }
        return true;
    }

    bool onUnload() override {
        chen::EventBusMgr::GetInstance()->clearAll();
        for (auto& s : m_servers) {
            auto hs = std::dynamic_pointer_cast<chen::http::HttpServer>(s);
            hs->getServletDispatch()->clear();
        }
        m_servers.clear();
        return true;
    }

private:
    std::vector<chen::TcpServer::ptr> m_servers;
};
```

### HTTP + WebSocket + 定时任务 + EventBus

```cpp
class BlogModule : public chen::Module {
public:
    bool onLoad() override { return true; }

    bool onServerReady() override {
        // 1. 清除旧 EventBus listener（旧 .so 还未 dlclose，安全）
        chen::EventBusMgr::GetInstance()->clearAll();

        // 2. 初始化数据库
        initMySQL();

        // 3. 注册 servlet
        registerServlets();       // HTTP
        registerWSServlets();     // WS
        registerRPCMethods();     // RPC

        // 4. 初始化 EventBus
        EventMsgsInit();

        // 5. 启动定时任务
        ArticleMgr::start();
        return true;
    }

    void onTick() override {
        ArticleMgr::onTimer();
        ArticleMgr::onUpdateTimer();
    }

    uint64_t getTickIntervalMs() override { return 60000; }

    // ======== 热重载 ========
    bool onDeactivate() override {
        ArticleMgr::stop();  // flush + 停定时器
        // 不清 EventBus——由新模块 onServerReady 处理
        return true;
    }

    // ======== 优雅关闭 ========
    bool onUnload() override {
        chen::EventBusMgr::GetInstance()->clearAll();  // .so 尚未 dlclose，安全
        ArticleMgr::stop();
        closeAllConnections();
        chen::RedisMgr::GetInstance()->freeAll();  // 释放 DB/Redis 连接
        return true;
    }
};
```

> 框架不代为初始化/释放数据库。Redis / MySQL / SQLite3 的配置与模块内用法见
> [`docs/db-configuration.md`](db-configuration.md)。

### 纯 RPC/Game 模块

```cpp
class GameModule : public chen::Module {
public:
    bool onLoad() override { return true; }

    bool onServerReady() override {
        chen::EventBusMgr::GetInstance()->clearAll();

        chen::Application::GetInstance()->getServer("rpc", m_rpcServers);
        registerRpcMethods();
        return true;
    }

    bool onUnload() override {
        chen::EventBusMgr::GetInstance()->clearAll();
        return true;
    }
};
```

---

## 继承决策流程图

```
模块需要处理请求?
├─ 是 → 必须实现 onServerReady()
└─ 否 → 纯工具模块, onLoad 即可

有 EventBus listener?
├─ 是 → onServerReady 开头调 clearAll(), 结尾调 EventMsgsInit()
│       onUnload() 调 clearAll()
└─ 否 → 不需要

有数据要存?
├─ 是 → onDeactivate() 里 flush 脏数据
└─ 否 → onDeactivate 可省略

有定时任务?
├─ 是 → 实现 onTick() + getTickIntervalMs()
└─ 否 → 不需要
```

---

## 定时器与数据安全

框架在关闭过程中会取消 tick 定时器，但数据安全由业务保证：

```
优雅关闭流程:
  框架取消 tick → onDeactivate() → flush 脏数据 → onUnload() → clearAll
                                ↑
                           你在这里保存数据

热重载流程:
  框架取消旧 tick → 重建新 tick → onDeactivate() → flush 脏数据 → dlclose
                                                ↑
                                           你在这里保存数据
```

**框架只负责定时器生命周期，不操作业务数据。**
只要你 `onDeactivate` 里做了 flush，数据就丢不了。

---

## 异步任务调度：同一个池还是另一个池

IOManager 本质是 N 个协程在 M 个线程上调度（N:M 协程池）。每个 io worker、accept worker
都是一个 IOManager 实例，拥有自己的协程池。`schedule(cb)` 就是往这个池子里再投一个协程。

WorkerMgr 只是额外创建另一组 IOManager 池，和 io/accept 互不干扰。

### 在同一个池里调度：`IOManager::GetThis()->schedule(cb)`

任务加入**当前线程所属的** IOManager 池，和其他 HTTP/WS 请求的协程在同一个 N:M 池里跑。

```cpp
// 在 servlet 里，属于 io pool
chen::IOManager::GetThis()->schedule([data]() {
    // 也在 io pool 里执行，IO 操作 hook 自动 yield，不占线程
    auto rsp = httpClient.post("https://hooks.example.com", data);
});
```

适合绝大多数异步场景：发 webhook、写日志、调外部 API、数据库写入。IO 密集时 hook 自动 yield 让出线程给其他协程。

### 在另一个池里调度：`WorkerMgr::getAsIOManager("name")->schedule(cb)`

任务加入**另一个独立**的 IOManager 池，和 io/accept 的协程互不争抢线程。

```cpp
auto worker = chen::WorkerMgr::GetInstance()->getAsIOManager("process");
if (worker) {
    worker->schedule([data]() {
        heavyWork(data);  // 在 process pool 里执行
    });
}
```

适合 CPU 密集任务需要隔离（图片处理、AI 推理），避免抢占 HTTP 请求的协程。同一个框架、同一个协程模型、同一个 hook 机制。

### 对比

| | 同一个池 | 另一个池 |
|---|---|---|
| API | `IOManager::GetThis()->schedule(cb)` | `WorkerMgr::getAsIOManager("name")->schedule(cb)` |
| 在哪跑 | 当前线程所属的 N:M 协程池 | 独立的 N:M 协程池 |
| 和谁共享线程 | HTTP/WS handler 的协程 | 只有你投进去的协程 |
| 典型场景 | webhook、日志、DB 写入 | 图片处理、AI 推理 |

### 选哪个？

| 场景 | 用什么 |
|------|--------|
| 发 webhook、写日志、更新计数 | 同一个池 |
| 等数据库返回、调外部 API | 同一个池（IO 密集，hook 自动 yield） |
| 图片压缩、AI 推理、批量计算 | 另一个池（CPU 密集，需要隔离） |
| 定时任务 | 框架的 `onTick()` |

### 批量并发等待：WorkerGroup

当你需要同时发起多个异步任务并等待全部完成时，用 `WorkerGroup`。它通过信号量限制并发数，`waitAll()` 会阻塞当前协程直到所有任务执行完毕。

```cpp
// 批量查询 100 个用户信息，同时最多 10 个并发
auto group = chen::WorkerGroup::Create(10);  // batch_size = 10
for (int i = 0; i < 100; ++i) {
    group->schedule([i]() {
        queryUser(i);
    });
}
group->waitAll();  // 阻塞直到 100 个任务全部完成
// 所有查询完成，继续处理
```

`schedule(cb, thread)` 的第二个参数可以指定执行线程，`-1`（默认）表示任意线程。

**对比三种调度方式：**

| | schedule(cb) | WorkerGroup | WorkerMgr |
|---|---|---|---|
| 用途 | 单个异步任务 | 批量任务，等全部完成 | 隔离到另一个池 |
| 等待完成 | 自行协调 | `waitAll()` 阻塞等待 | 自行协调 |
| 并发控制 | 无 | batch_size 信号量 | 池的线程数 |
| 典型场景 | webhook、日志 | 批量查询、并发爬取 | CPU 密集隔离 |

### 注意事项

- 绝大多数业务场景不需要另一个池——同一个池就够用了。只有当 CPU 计算明显拖慢 HTTP 响应时才考虑隔离。
- 热重载时 WorkerMgr 的池会正常 stop/restart，旧池里排队的协程会被丢弃。有不能丢的任务时在 `onDeactivate` 里等待完成。
