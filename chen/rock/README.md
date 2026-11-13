# Rock — Rock 协议

自定义二进制 RPC 协议，支持请求/响应和单向通知，body 可选 Protobuf（`RockBody::getAsPB/setAsPB`）。

## 核心类

| 类                   | 说明                                          |
|----------------------|-----------------------------------------------|
| `RockMsgHeader`      | 协议头（magic + version + flag + length）     |
| `RockMessageDecoder` | 帧编解码，支持 gzip                           |
| `RockRequest`        | 请求（sn + cmd + body）                       |
| `RockResponse`       | 响应（sn + cmd + result + result_str + body） |
| `RockNotify`         | 通知（notify + body，单向无需响应）           |
| `RockStream`         | 异步流，负责请求/响应匹配与超时               |
| `RockSession`        | 服务端连接会话（不自动重连）                  |
| `RockConnection`     | 客户端连接（断线自动重连）                    |
| `RockServer`         | Rock 协议服务器，继承 `TcpServer`             |
| `RockModule`         | Rock 协议模块基类，继承 `Module`              |
| `RockSDLoadBalance`  | 基于服务发现的客户端负载均衡                  |

## 协议帧

```
[magic:2B=0xabcd][version:1B=0x01][flag:1B][length:4B 大端][body:length]
```

- `flag` bit0 = gzip（body 压缩）
- `body` 首字节是消息类型：`1` 请求、`2` 响应、`3` 通知
- 压缩阈值与最大长度可配置：

```yaml
rock:
  protocol:
    max_length: 67108864   # 默认 64MB
    gzip_min_length: 4096  # 默认 4KB
```

## 服务端接入

在 yml 中声明 `type: rock` 即可启动：

```yaml
servers:
  - type: rock
    name: rock
    address: ["0.0.0.0:9000"]
```

实现 `RockModule` 并编译成 `.so` 放到 `bin/module/`：

```cpp
#include "chen/module/module.h"

class MyModule : public chen::RockModule {
public:
    MyModule() : RockModule("my_rock", "1.0", "") {}

    bool onServerReady() override {
        // 把自己注册到服务发现（需要 Application 已设置 IServiceDiscovery）
        registerService("rock", "astartes", "rock");
        return true;
    }

    bool handleRockRequest(chen::RockRequest::ptr req,
                           chen::RockResponse::ptr rsp,
                           chen::RockStream::ptr stream) override {
        rsp->setResult(0);
        rsp->setBody("echo:" + req->getBody());
        return true;  // 返回 false 会先回包再关闭连接
    }

    bool handleRockNotify(chen::RockNotify::ptr nty,
                          chen::RockStream::ptr stream) override {
        return true;
    }
};

extern "C" {

chen::Module* CreateModule() { return new MyModule; }

void DestroyModule(chen::Module* ptr) { delete ptr; }

}
```

`RockServer::handleClient` 会遍历所有 `Module::ROCK` 类型的模块分发请求，
第一个返回 `true` 的模块视为已处理；全部返回 `false` 时回包后关闭连接。

## 客户端直连

```cpp
auto addr = chen::Address::LookupAny("127.0.0.1:9000");
chen::RockConnection::ptr conn(new chen::RockConnection);
conn->connect(addr);
conn->start();  // 启动读写协程，断线后自动重连

chen::RockRequest::ptr req(new chen::RockRequest);
req->setSn(1);
req->setCmd(100);
req->setBody("hello");

auto result = conn->request(req, 3000);
if (result->result == chen::AsyncSocketStream::OK) {
    // result->response->getBody()
}
```

`request()` 必须在 `IOManager` 的协程中调用（内部使用 `addTimer` 做超时）。

返回值语义：

| result                           | 含义                   |
|----------------------------------|------------------------|
| `AsyncSocketStream::OK`          | 成功收到响应           |
| `AsyncSocketStream::TIMEOUT`     | 超时                   |
| `AsyncSocketStream::NOT_CONNECT` | 连接未建立             |
| 其他                             | 响应中的业务 result 码 |

## 服务发现与负载均衡

`RockSDLoadBalance` 不自动启动，由使用方组装：

```yaml
# 静态服务发现
service_discovery:
  domains:
    astartes:
      rock:
        - name: "rock-1"
          address: "127.0.0.1:9001"
          weight: 100
        - name: "rock-2"
          address: "127.0.0.1:9002"
          weight: 300

# 负载均衡策略：round_robin / weight / fair(默认)
rock_services:
  astartes:
    rock: round_robin

rock:
  # 建连使用的 worker 调度器名称，留空使用当前调度器
  service_worker: ""
  # 无响应时的最大重试次数（默认 2），timeout_ms 是包含重试的总预算
  request_max_retry: 2

# 被动健康检查：连续失败达到阈值后在冷却期内摘除实例
load_balance:
  fail_threshold: 3
  fail_cooldown_ms: 5000
```

```cpp
auto sd = std::make_shared<chen::StaticServiceDiscovery>();
chen::RockSDLoadBalance lb(sd);
lb.start();  // 读取 rock_services 配置

auto result = lb.request("astartes", "rock", req, 900);
```

`request()` 在未收到响应（`TIMEOUT` / `IO_ERROR` / `NOT_CONNECT`）时会换实例重试，
收到响应后即使业务 `result` 非 0 也不重试。因此语义是 **at-least-once**，被调用接口需要幂等。
服务端模块可通过 `Module::registerService(server_type, domain, service)` 自动注册自己，
`onDeactivate()` 默认会自动调用 `unregisterServer` 注销（覆写 `onDeactivate` 时需调用基类实现）。
（监听 `0.0.0.0` 时会使用本机非回环 IPv4 地址，回环地址会被跳过。）

> 注意：`ServiceItemInfo` 目前只支持 IPv4 地址。

## 与 RPC 的区别

Rock 是更轻量的自定义协议，body 不强制 Protobuf；RPC 模块（`chen/rpc/`）是基于
Protobuf + 标准 RPC 协议的实现，支持方法注册与路由。
