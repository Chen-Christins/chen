# RPC — 远程过程调用框架

模板驱动的 RPC 框架，自动序列化/反序列化参数。无需 IDL / proto 定义。

除点对点 RPC 外，`RpcServer` 支持**中心转发（relay/hub）模式**：服务全部连接到 hub，由 hub 按
`peer_id`/`func_id`/`bind_id` 路由转发并中继回包（对应旧版 Tunnel 的中心消息转发）。

## 核心类

| 类 | 说明 |
|----|------|
| `RpcServer` | RPC 服务端，继承 `TcpServer`；`setRelay(true)` 开启中心转发模式 |
| `RpcClient` | RPC 客户端，同步/异步调用 + 超时控制；支持注册/路由/单向通知 |
| `RpcClientPool` | 客户端连接池（地址复用 + 空闲回收） |
| `RpcHubRegistry` | hub 的服务注册表（`peer_id`/`func_id`/`bind_id` 索引，即 CSvrRouteMgr） |
| `RpcRouting` | 路由描述（DIRECT / GROUPID / BROADCAST / BIND_ID） |
| `Protocol` | 二进制传输帧，`ProtocolHeader + body` |
| `Serializer` | 函数参数编解码，支持 int/string/vector/map 及自定义类型 |
| `FunctionTraits` | 编译期函数签名萃取 |

## 使用示例

### 服务端（点对点）

```cpp
// 任意普通函数
int add(int a, int b) { return a + b; }
std::string echo(std::string s) { return s; }
void notify(int id) { /* ... */ }

RpcServer::ptr server(new RpcServer);
server->bind(addr);
server->registerMethod("add", add);
server->registerMethod("echo", echo);
server->registerMethod("notify", notify);
server->start();
```

### 客户端

```cpp
RpcClient::ptr client(new RpcClient);
client->connect(addr);

int sum = client->call<int>("add", 40, 20);           // → 60
std::string msg = client->call<std::string>("echo", "hello");
client->call<void>("notify", 123);
```

### 自定义类型

特化 `Serialization<T>` 即可：

```cpp
struct UserInfo { int id; std::string name; };

template <>
struct chen::rpc::Serialization<UserInfo> {
    static void write(ByteArray::ptr ba, const UserInfo& u) {
        ba->writeFint32(u.id);
        ba->writeStringVint(u.name);
    }
    static void read(ByteArray::ptr ba, UserInfo& u) {
        u.id = ba->readFint32();
        u.name = ba->readStringVint();
    }
};
```

支持 `std::vector<T>`、`std::map<K,V>`、`std::set<T>` 等 STL 容器嵌套。

### 超时控制

```cpp
// call 默认超时 3s
client->call<int>("method", args...);

// 自定义超时
client->callWithTimeout<int>(5000, "method", args...);  // 5s
```

### 异步调用

异步接口与同步接口一一对应，发起后**立即返回**，完成（响应/超时/断开）时回调被
调度为独立协程执行，不阻塞调用方。回调参数为 `RpcResult<R>`：

```cpp
// RpcStatus: OK / TIMEOUT / SEND_FAILED / CONNECTION_CLOSED / DECODE_ERROR
// RpcResult<R>: status（传输层）、code（服务端业务码，0=成功）、error、value
client->callAsync<int>("add", [](chen::rpc::RpcResult<int> r) {
    if (r.ok()) {
        INFO(logger) << "add = " << r.value;
    } else {
        ERROR(logger) << "add failed: code=" << r.code << " err=" << r.error;
    }
}, 40, 20);

// 自定义超时
client->callAsyncWithTimeout<std::string>(5000, "echo"
        , [](chen::rpc::RpcResult<std::string> r) { /* ... */ }, "hi");

// void 返回值
client->callAsync<void>("on_event", [](chen::rpc::RpcResult<void> r) { /* ... */ });
```

同步接口均有对应的异步版本（`callAsync` / `callAsyncWithTimeout` /
`callRoutedAsync` / `callRoutedAsyncWithTimeout`，首参同样支持方法名或 CmdID 两种重载），
回调紧跟在方法名（或 CmdID）之后、参数之前。异步路径不抛异常，所有错误通过
`RpcResult` 表达；仅方法名为空或 cmd=0 时与同步一致抛 `std::invalid_argument`。

### Future 调用

Future 接口也是**发起即发送**（eager），但不在回调里处理结果，而是返回一个
`RpcFuture<R>` 句柄，之后在需要时调用 `get()` 挂起当前协程等待结果——适合
「并发发起多个请求，再按顺序取结果继续处理」的写法。因为基于协程，`get()`
只挂起当前 fiber，不阻塞线程。

```cpp
auto f1 = client->callFuture<int>("add", 40, 20);
auto f2 = client->callFuture<std::string>("echo", "hi future");

// ... 中间可以做别的事 ...

// get(): 返回 RpcResult<R>，不抛异常
auto r2 = f2.get();
if (r2.ok()) {
    INFO(logger) << "echo = " << r2.value;
}

// getValue(): 成功返回 value，失败抛 std::runtime_error
int sum = f1.getValue();
```

- `ready()` 可非阻塞查询结果是否就绪；`get()` 可多次调用（返回缓存结果）。
- future 接口与异步一一对应：`callFuture` / `callFutureWithTimeout` /
  `callRoutedFuture` / `callRoutedFutureWithTimeout`，首参同样支持方法名或 CmdID 两种重载。
- `get()`/`getValue()` 必须在 fiber/调度器上下文调用（与同步 `call` 相同）。
- 超时/断连/业务错误统一通过 `RpcResult` 表达，`getValue()` 在失败时抛异常。

## 传输协议

```
[magic:1B][version:1B][type:1B][routing:1B][real_random:1B]
[sequence:4B][length:4B]
[cmd:4B][src_peer_id:4B][dst_peer_id:4B][func_id:4B][group_id:4B][bind_id:4B][region:4B]
[body:length]
```

- **magic:** `0xCC`
- **version:** `0x03`（v0x02/v0x01 旧帧在解码层直接拒绝）
- **type:** `0`=REQUEST, `1`=RESPONSE, `2`=HEARTBEAT, `3`=NOTIFY(单向，无回包)
- **routing:** `0`=NONE, `1`=DIRECT, `2`=GROUPID, `3`=BROADCAST, `4`=BIND_ID
- **real_random:** GROUPID 动态拓扑标记（`0`=固定拓扑，`1`=按在线实例数实时选择）
- **cmd:** CmdID 分派（`0`=按 body 中方法名分派，非 `0`=按 CmdID 分派，body 直接为参数）
- **src_peer_id / dst_peer_id:** 对端身份（hub 路由/溯源用）
- **func_id / group_id / bind_id:** 目标功能类型 / 分片键 / 绑定业务 ID
- **region:** 预留来源区号（本区恒 0，跨区路由二期使用）
- **sequence:** 客户端分配，服务端回填，用于请求/响应匹配
- **body 编码格式：**

  ```
  方法名模式(cmd=0):  请求: [method_vint][args_binary...]
  CmdID 模式(cmd≠0):  请求: [args_binary...]
  响应: [code_int32][result_binary...]   (code=0)
       [code_int32][message_vint]       (code≠0)
  ```

  args/result 的二进制布局由 `Serializer` 根据 C++ 函数签名自动决定。

## 中心转发（Hub）模式

### 架构

```
业务服务 ──TCP──▶ hub(RpcServer relay) ──TCP──▶ 业务服务
  RpcClient        服务注册表+路由转发          RpcClient
```

- 所有服务只**主动连接** hub，不互连；消息经 hub 转发并中继回包
- hub 内 `RpcHubRegistry` 维护：`peer_id → conn`、`func_id → [instance_id → conn]`、
  `(func_id, bind_id) → conn`
- 转发**非阻塞**：登记中继上下文后立即返回，同一连接可同时有多个 in-flight 请求；
  目标回包由目标连接的收包循环直接中继回调用方
- 中继超时（默认 3s）回 `code=504`；目标断开回 `code=502`

### 配置

```yaml
servers:
  - address: ["0.0.0.0:6000"]
    type: rpc
    name: rpc_hub
    args:
      relay: "1"
      relay_timeout: 3000
```

### 服务侧（注册 + 接收转发请求）

```cpp
RpcClient::ptr svc(new RpcClient);
svc->connect(hub_addr);
// 注册：peer_id 配置分配、按 hub 唯一；重复注册被拒绝
svc->registerService(1001, 1, 1, {100, 200});   // peer_id, func_id, instance_id, bind_ids

// 注册本地方法处理器，接收 hub 转发来的请求（REQUEST 自动回包，NOTIFY 只处理不回包）
svc->registerMethod("get_player", [](uint32_t uid) { return PlayerInfo{...}; });
svc->registerMethod("on_event", []() { /* NOTIFY */ });
```

### 调用侧（路由调用）

```cpp
RpcClient::ptr caller(new RpcClient);
caller->connect(hub_addr);
caller->registerService(9001, 9, 1, {});

// DIRECT：发给明确 peer_id
int r = caller->callRouted<int>({RoutingMethod::DIRECT, .dst_peer_id = 1001}, "get_player", 123);

// GROUPID：按 func_id + group_id 拓扑选实例
//   固定拓扑 InstanceID = 1 + group_id % max_instance
//   real_random=true 动态拓扑 = 1 + group_id % 在线实例数
int r = caller->callRouted<int>({RoutingMethod::GROUPID, .func_id = 1, .group_id = 5}, "get_player", 123);

// BIND_ID：按 func_id + bind_id 绑定关系路由
int r = caller->callRouted<int>({RoutingMethod::BIND_ID, .func_id = 1, .bind_id = 100}, "get_player", 123);

// BROADCAST：fan-out 到 func_id 全部实例，调用方即时收 code=0，不等 fan-out 完成
caller->callRouted<void>({RoutingMethod::BROADCAST, .func_id = 1}, "on_event");

// NOTIFY 单向（fire-and-forget，无回包）：DIRECT/GROUPID/BIND_ID/BROADCAST 均支持
caller->notifyRouted({RoutingMethod::DIRECT, .dst_peer_id = 1001}, "on_event");
```

> 注意：`RpcRouting` 为聚合初始化，字段按声明顺序。

### CmdID 分派

方法名分派 body 里带方法名字符串（开发调试直观）；线上热路径推荐用 **CmdID**——
`cmd != 0` 时 body 直接是参数，无字符串，按 `uint32_t` 命令号查表分派。

调用侧与 `registerMethod` 一样只用**一组接口**：首参传方法名字符串走方法名分派，
传 `uint32_t` 命令号走 CmdID 分派（`cmd` 必须非 0，否则抛 `std::invalid_argument`），
方法名与 CmdID 共用同一张分发表（`HandlerKey` = `std::variant<uint32_t, std::string>`，
方法名 `"16"` 与 cmd `16` 互不冲突）。

```cpp
// 服务侧注册（RpcServer / RpcClient 均可）
server->registerMethod(0x01, [](uint32_t v) { return v * 2; });
server->registerMethod(0x02, []() { return (uint32_t)1001; });
server->registerMethod(0x03, []() { /* NOTIFY */ });

// 调用侧（与方法名调用共用接口，仅首参类型不同）
client->call<uint32_t>(0x01, 21);                            // → 42（直连）
client->callRouted<uint32_t>({RoutingMethod::DIRECT, .dst_peer_id = 1001}, 0x02);
client->callRouted<uint32_t>({RoutingMethod::GROUPID, .func_id = 1, .group_id = 5}, 0x02);
client->notifyRouted({RoutingMethod::BROADCAST, .func_id = 1}, 0x03);

// 注销
server->unregisterMethod(0x01);
```

`cmd=0` 时回退到方法名分派（`@register` 等保留方法即此模式），两种模式可并存。

## 高级用法

### 连接池

```cpp
RpcClientPoolMgr::GetInstance()->getClient("127.0.0.1:8080");
RpcClientPoolMgr::GetInstance()->pruneIdle(60000);  // 回收 60s 空闲连接
```

### 热重载

`registerMethod` 将函数存入 lambda。热重载时：

```cpp
server->unregisterMethod("old_func");
// 再重新注册
```

避免旧 `.so` 卸载后函数指针悬垂。RpcClient 的 `registerMethod`（接收转发请求）同样在
`unload` 时需 `unregisterMethod` 清理。

### 心跳

默认每 30s 发一次心跳，连续 3 次无响应自动断开：

```cpp
RpcClient client(false);        // 关闭心跳
client.setHeartbeatInterval(10000);  // 改为 10s
```

### 序列化防护

自定义类型必须特化 `Serialization<T>`，否则编译报错：

```
error: static_assert failed: "Serialization not specialized for this type"
```

## 性能要点

- 帧头解析/拼接在栈 buffer 完成（大端读写见 `util/endian.h`），避免每条消息的
  `ByteArray` 堆分配
- hub 转发非阻塞 + 每连接独立出站 seq + 按连接分片的中继表与写信号量，无全局锁竞争
- `TCP_NODELAY` 已开启，小包单次 write