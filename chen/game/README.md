# Game — 游戏协议服务器

自定义二进制游戏协议服务器，支持消息路由和回声测试。

## 核心类

- **GenericProtocolServer** — 通用二进制协议服务器，继承 TcpServer
- **IMessageHandler** — 消息处理器接口
- **EchoHandler** — 默认 echo 处理器

## 协议格式

```
[magic:4B][cmd:4B][seq:4B][len:4B][header:variable][body:variable]
```

- magic: 协议魔数
- cmd: 命令 ID
- seq: 序列号（请求/响应匹配）
- len: body 长度
- header: 自定义头部
- body: Protobuf 或其他格式的消息体

## 消息路由

通过 `registerHandler(cmdId, handler)` 按 cmdId 注册处理器。

```cpp
auto server = std::make_shared<GenericProtocolServer>();
server->registerHandler(1, std::make_shared<MyHandler>());
```

## 消息分发

`handleClient` 循环读取协议帧 → 解析 header → 按 cmdId 查找 handler → 调用 `handler->handle()`
