# Module — 动态模块加载

支持热重载的动态模块系统。模块编译为 `.so` 文件，通过 `dlopen` 加载，框架自动调用生命周期方法。

## 核心类

| 类 | 说明 |
|------|------|
| `Module` | 模块基类，定义生命周期接口 |
| `RockModule` | Rock 协议模块特化，继承 Module |
| `ModuleManager` | 模块管理器（`ModuleMgr` 单例），扫描 `bin/module/` 目录加载 |
| `Library` | dlopen/dlclose 封装 |

## 模块生命周期

```
CreateModule() → onLoad() → onServerReady() → onServerUp()
                                                    ↓
                                              [onTick()]   ← 周期性
                                                    ↓
热重载: onDeactivate() → DestroyModule() → dlclose()
关机:   onDeactivate() → onUnload() → DestroyModule() → dlclose()
```

所有方法都有默认空实现，子类按需覆写。

### 方法说明

| 方法 | 何时调用 | 典型操作 |
|------|----------|----------|
| `onLoad()` | dlopen 后 | 初始化内部状态 |
| `onServerReady()` | server 创建后 | clearAll + 注册 servlet/handler/EventBus |
| `onServerUp()` | server 开始 accept | 启动后台任务 |
| `onTick()` | 每 N 毫秒 | 定时任务 |
| `onDeactivate()` | 旧模块被替换 / 关机 | stop 定时器, flush 数据 |
| `onUnload()` | 仅关机 | clearAll EventBus, 释放资源 |

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
    // 清除旧模块 EventBus listener
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

### 热重载

```SIGHUP
kill -HUP <pid>
```

框架流程：
1. `dlopen` 新 `.so` → `CreateModule()` → 新模块 `onLoad()`
2. 新模块 `onServerReady()` — clearAll + 注册新的 servlet/handler/EventBus
3. `commitDispatch()` — 框架原子切换流量到新 handler
4. 新模块 `onServerUp()` — 感知 server 开始 accept
5. 旧模块 `onDeactivate()` — stop 定时器, flush 数据
6. `DestroyModule()` → `dlclose` 旧 .so

### 优雅关闭

框架流程：
1. 停止 accept, drain 连接
2. 取消 tick 定时器
3. `onDeactivate()` — flush 数据
4. `onUnload()` — clearAll EventBus, 释放资源
5. 停止 WorkerMgr → 进程退出

## ⚠️ EventBus 安全

EventBus listener 中的 `std::function` 通过 type-erasure 存储函数指针，指向模块 `.so` 中的代码。
`dlclose()` 后指针悬空，销毁会 segfault。

**规则：`clearAll()` 必须在 `.so` dlclose 之前调用。**

- `onServerReady()` 开头调 `clearAll()` — 安全（旧 .so 还活着）
- `onUnload()` 调 `clearAll()` — 安全（关机时 .so 还活着）
- `onDeactivate()` **不要**调 `clearAll()` — 会清掉新模块的 listener
