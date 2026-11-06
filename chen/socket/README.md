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

在 Scheduler/IOManager 的工作线程中， **Socket 的 I/O 操作会被 Hook 自动转为 fiber 异步**（非阻塞 + epoll + fiber 切换）。
应用层代码正常调用 `send/recv/connect/accept` 即可，不需要自己处理 EAGAIN 或 select/poll/epoll。

## 核心类

| 类             | 说明                                  |
|----------------|---------------------------------------|
| `Socket`       | 基础 Socket，支持 TCP/UDP/Unix Domain |
| `SSLSocket`    | SSL/TLS Socket，基于 OpenSSL          |
| `Address`      | 地址基类                              |
| `IPAddress`    | IPv4/IPv6 地址                        |
| `UnixAddress`  | Unix Domain Socket 地址               |
| `SocketStream` | Socket 流式读写封装                   |

## 关键方法

| 方法                      | 说明                                    |
|---------------------------|-----------------------------------------|
| `Socket::CreateTCP(addr)` | 创建 TCP Socket                         |
| `Socket::CreateUDP(addr)` | 创建 UDP Socket                         |
| `bind(addr)`              | 绑定地址                                |
| `listen()`                | 开始监听                                |
| `accept()`                | 接受连接，返回新的 Socket               |
| `connect(addr)`           | 连接远端                                |
| `send(data, len)`         | 发送数据                                |
| `recv(buffer, len)`       | 接收数据                                |
| `close()`                 | 关闭 Socket                             |
| `cancelAll()`             | 取消该 fd 在 IOManager 上注册的所有事件 |

> `cancel*` 在 fd 无效（`isValid() == false`）或当前线程没有 `IOManager` 时返回 `false`，不会阻塞或崩溃；
> 负 fd 在 `IOManager` 侧也会被直接拒绝。

## SSLSocket（TLS 客户端 / 服务端）

### 客户端

```cpp
SSLSocket::ptr sock = SSLSocket::CreateTCP(addr);   // addr 用域名解析出的 IP
sock->setServerName("api.example.com");             // SNI + 证书主机名校验
sock->setVerifyPeer(true);                          // 默认就是 true，自签名服务可关
sock->connect(addr);
```

也可以直接带配置创建：

```cpp
chen::tls::TlsConfig conf = chen::tls::TlsConfig::DefaultClient();
conf.ca_file = "/etc/chen/certs/ca.crt";            // 私有 CA
conf.alpn = {"http/1.1"};
auto sock = SSLSocket::CreateTCP(addr, conf);
sock->setServerName("api.example.com");
sock->connect(addr);
```

- 校验默认 **开启**：系统 CA，或 `TlsConfig.ca_file` 指定私有 CA
- `SNI` 与主机名校验使用 `setServerName()` 的域名；IP 字面量自动走 IP SAN 校验，且不发送 SNI
- 客户端 ALPN 由 `TlsConfig.alpn` 指定；`TlsConfig::DefaultClient()` 默认只声明 `http/1.1`
- 握手与 `SSL_read/SSL_write` 的 `WANT_READ/WANT_WRITE` 都在 IOManager 上等待，协程挂起而不占线程
- 握手超时由 `TlsConfig.handshake_timeout_ms` 控制，默认 10000ms

### 服务端

```cpp
chen::tls::TlsConfig conf = chen::tls::TlsConfig::DefaultServer();
conf.cert_file = "/etc/chen/certs/server.crt";
conf.key_file = "/etc/chen/certs/server.key";
conf.alpn = {"h2", "http/1.1"};

auto listen = SSLSocket::CreateTCP(addr, conf);
listen->bind(addr);
listen->listen();
```

- `setTlsConfig()` 会在服务端立即创建 `TlsContext`
- `accept()` 只建立 TCP 连接并继承监听 socket 的 `TlsContext`
- **过渡期说明**：当前 `SSLSocket::init()` 仍会在 accept 阶段自动握手，保持现有 HTTPS 行为；
  Step 3 接入 `TcpServer` 后改为由 io worker 显式调用 `handshake(timeout_ms)`
- 显式握手 API 已就绪，握手成功后可读取协商结果：

```cpp
auto client = listen->accept();
if (client && client->handshake(10000)) {
    std::string proto = client->getAlpnProtocol();   // "h2" / "http/1.1" / ""
}
```

- `getHandshakeStatus()` 返回 `NOT_STARTED / OK / TIMEOUT / VERIFY_FAILED / PEER_CLOSED / ERROR`，供日志与指标分类
- `loadCertificates(cert, key)` 保留为兼容接口，内部构造服务端 `TlsConfig`

## SocketStream

提供面向流的读写接口：

- `readFixSize(size)` — 定长读取
- `readLine()` — 行读取
- `writeFixSize(data, size)` — 定长写入
