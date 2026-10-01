# Socket — 网络套接字封装

跨平台 Socket API 的 C++ 封装。

## 使用示例

```cpp
// TCP 客户端
Socket::ptr sock = Socket::CreateTCP(Address::LookupAny("127.0.0.1:8080"));
sock->connect();
sock->send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
std::string buf(4096, '\0');
sock->recv(&buf[0], buf.size());

// TCP 服务端
Socket::ptr listen_sock = Socket::CreateTCP(Address::LookupAny("0.0.0.0:8080"));
listen_sock->bind();
listen_sock->listen();
Socket::ptr client = listen_sock->accept();
client->setRecvTimeout(5000);  // 5s 接收超时
```

## 注意

在 Scheduler/IOManager 的工作线程中，**Socket 的 I/O 操作会被 Hook 自动转为 fiber 异步**（非阻塞 + epoll + fiber 切换）。
应用层代码正常调用 `send/recv/connect/accept` 即可，不需要自己处理 EAGAIN 或 select/poll/epoll。

## 核心类

| 类 | 说明 |
|----|------|
| `Socket` | 基础 Socket，支持 TCP/UDP/Unix Domain |
| `SSLSocket` | SSL/TLS Socket，基于 OpenSSL |
| `Address` | 地址基类 |
| `IPAddress` | IPv4/IPv6 地址 |
| `UnixAddress` | Unix Domain Socket 地址 |
| `SocketStream` | Socket 流式读写封装 |

## 关键方法

| 方法 | 说明 |
|------|------|
| `Socket::CreateTCP(addr)` | 创建 TCP Socket |
| `Socket::CreateUDP(addr)` | 创建 UDP Socket |
| `bind(addr)` | 绑定地址 |
| `listen()` | 开始监听 |
| `accept()` | 接受连接，返回新的 Socket |
| `connect(addr)` | 连接远端 |
| `send(data, len)` | 发送数据 |
| `recv(buffer, len)` | 接收数据 |
| `close()` | 关闭 Socket |
| `cancelAll()` | 取消该 fd 在 IOManager 上注册的所有事件 |

## SocketStream

提供面向流的读写接口：
- `readFixSize(size)` — 定长读取
- `readLine()` — 行读取
- `writeFixSize(data, size)` — 定长写入
