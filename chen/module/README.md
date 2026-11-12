# Module — 动态模块加载

支持热重载的动态模块系统。模块编译为 `.so` 文件，通过 `dlopen` 加载，框架自动调用生命周期方法。

## 核心类

| 类              | 说明                                                        |
|-----------------|-------------------------------------------------------------|
| `Module`        | 模块基类，定义生命周期接口                                  |
| `RockModule`    | Rock 协议模块特化，继承 Module                              |
| `ModuleManager` | 模块管理器（`ModuleMgr` 单例），扫描 `bin/module/` 目录加载 |
| `Library`       | dlopen/dlclose 封装                                         |

## 模块生命周期

```
启动（worker）: CreateModule(dlopen)
               → onBeforeArgsParse → onAfterArgsParse
               → onLoad → onServerReady → onServerUp → [onTick()...]

退出（热重载 drain / 关机）:
               停止 accept → 关闭连接 → 限时 drain（server.drain_timeout_ms）
               → 取消 tick 定时器 → onDeactivate → onUnload → _exit(0)
```

> 正常启停和热重载 **不做进程内 `dlclose`**：`ModuleMgr` 是 `LeakySingleton`，
> worker 用 `_exit(0)` 退出，地址空间由内核回收。只有显式调用
> `ModuleMgr::del()/delAll()` 时才会析构模块并 `dlclose`。

所有方法都有默认空实现，子类按需覆写。

### 方法说明

| 方法              | 何时调用            | 典型操作                                                         |
|-------------------|---------------------|------------------------------------------------------------------|
| `onLoad()`        | dlopen 后           | 初始化内部状态                                                   |
| `onServerReady()` | server 创建后       | 注册 servlet/handler/EventBus（`clearAll` 可选，且需在其后注册） |
| `onServerUp()`    | server 开始 accept  | 启动后台任务                                                     |
| `onTick()`        | 每 N 毫秒           | 定时任务                                                         |
| `onDeactivate()`  | worker drain / 关机 | flush 数据、收尾（tick 定时器已由框架取消）                      |
| `onUnload()`      | worker 退出前       | clearAll EventBus，释放资源                                      |

## 使用

### 定义一个模块

```cpp
extern "C" {
Module* CreateModule() { return new MyModule; }
void DestroyModule(Module* m) { delete m; }
}
```

编译为 `.so`，放到 `bin/module/` 下，框架自动加载。

### 注册 RPC / HTTP / WS servlet

```cpp
bool MyModule::onServerReady() {
    // EventBus 是进程内单例，新 worker 无需清理旧版本；
    // 若保留 clearAll，模块 listener 必须在其之后注册
    chen::EventBusMgr::GetInstance()->clearAll();

    // 获取 RPC 服务器并注册方法
    std::vector<RpcServer::ptr> rpc_servers;
    getAllRpcServer(rpc_servers);
    for (auto& s : rpc_servers) {
        s->registerMethod("my_method", my_func);
    }

    // 获取 HTTP 服务器
    std::vector<HttpServer::ptr> http_servers;
    getAllHttpServer(http_servers);
    for (auto& s : http_servers) {
        s->getServletDispatch()->addServlet("/api", my_servlet);
    }
    return true;
}
```

### 热重载（多进程模型）

```SIGHUP
kill -HUP <master_pid>
```

采用 master/worker 多进程模型：master 持有监听端口，收到 SIGHUP 后 fork 出一个新
worker 进程加载磁盘上的新模块；新 worker 就绪后，master 通知旧 worker 停止 accept，
旧 worker 限时 drain 完遗留连接后退出。每个进程只加载一个版本的 `.so`，退出时由内核
回收地址空间， **不做进程内 `dlclose`**。

框架流程：

1. master fork 新 worker，新 worker `CreateModule()` → `onLoad()` → `onServerReady()` → `onServerUp()`
2. 新 worker 就绪后开始 accept
3. 旧 worker 停止 accept → 限时 drain → `onDeactivate()` → `onUnload()` → 进程退出

### 优雅关闭

框架流程：

1. 停止 accept, drain 连接
2. 取消 tick 定时器
3. `onDeactivate()` — flush 数据
4. `onUnload()` — 释放资源
5. 停止 WorkerMgr → 进程退出

## EventBus 安全

EventBus 是进程内单例，listener 中的 `std::function` 存储模块 `.so` 中的函数指针。
多进程模型下，每个 worker 拥有独立的 EventBus 实例，进程退出时随地址空间一并回收，
不存在跨版本的悬空指针问题。各 worker 的 `onServerReady()` 无需为兼容旧版本而调用
`clearAll()`。若显式调用 `ModuleMgr::del()` 卸载模块，则必须先清理 listener / 回调。
