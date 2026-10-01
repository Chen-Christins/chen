# Rock — Rock 协议

自定义 RPC 协议，支持请求/响应和通知模式。

## 核心类

| 类 | 说明 |
|----|------|
| `RockProtocol` | 协议帧定义 |
| `RockServer` | Rock 协议服务器 |
| `RockClient` | Rock 协议客户端 |
| `RockStream` | Rock 协议流式连接 |
| `RockModule` | Rock 协议模块基类 |
| `RockRequest` | 请求消息 |
| `RockResponse` | 响应消息 |
| `RockNotify` | 通知消息（单向，无需响应） |

## 协议帧

```
[magic:2B][type:1B][flag:1B][length:4B][body:length]
```

## 消息模式

- **请求/响应**：客户端发送 Request，服务端返回 Response
- **通知**：客户端发送 Notify，服务端不回复

## RockModule

继承 `Module`，需实现：
- `handleRockRequest(request, response, stream)` — 处理请求
- `handleRockNotify(notify, stream)` — 处理通知

## 与 RPC 的区别

Rock 是更轻量的自定义协议，不依赖 Protobuf。RPC 模块（`chen/rpc/`）是基于 Protobuf + 标准 RPC 协议的实现。
