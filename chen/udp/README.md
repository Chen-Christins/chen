# UdpServer — UDP 服务器

面向报文的 UDP 服务器，与 `TcpServer` **独立平行**，不继承关系。适合 DNS、NTP、实时游戏同步、日志上报等场景。

## 与 TcpServer 的区别

| 特性 | TcpServer | UdpServer |
|------|-----------|-----------|
| 传输层 | 面向连接 (SOCK_STREAM) | 面向报文 (SOCK_DGRAM) |
| 监听方式 | bind → listen → accept 循环 | bind 后直接 recvfrom 循环 |
| 客户端模型 | 每个 accept 产生独立 Socket，跟踪 m_activeClients | 无连接概念，每报文一次 recvfrom，不维护客户端集合 |
| SSL | 支持 | 不支持（DTLS 暂未实现） |
| worker 分工 | accept_worker / io_worker / process_worker | worker（recv 循环）/ io_worker（报文处理） |

## 工作流程

```
bind → start() → recvfrom fiber 循环 → handleRecv(sock, data, from)
                           ↑
                      stop() → 关闭 socket → recvfrom fiber 退出
```

- **worker**：负责 recvfrom 数据报循环（替代 TcpServer 的 accept_worker）
- **io_worker**：负责调度 handleRecv 回调（报文处理可在任意 IOManager 上执行）
- `m_isStop` 控制 recvfrom 循环，`stop()` 异步关闭 socket

## 核心类

### UdpServer

```cpp
class UdpServer : public std::enable_shared_from_this<UdpServer>, Noncopyable {
public:
    typedef std::shared_ptr<UdpServer> ptr;

    UdpServer(IOManager* worker = IOManager::GetThis(),
              IOManager* io_worker = IOManager::GetThis());

    bool bind(Address::ptr addr);
    bool bind(const std::vector<Address::ptr>& addrs,
              std::vector<Address::ptr>& fails);
    bool start();
    void stop();
    void handleRecv(Socket::ptr sock, const char* data, size_t len,
                    Address::ptr from);  // virtual

    // getter / setter
    std::string getName() const;
    void setName(const std::string& v);
    std::string getType() const;
    void setType(const std::string& v);
    bool isStop() const;
    size_t getRecvBufSize() const;
    void setRecvBufSize(size_t v);
};
```

### UdpServerConf

配置结构，通过 `LexicalCast` 支持 YAML 序列化/反序列化。核心字段：

| 字段 | 类型 | 含义 |
|------|------|------|
| address | `vector<string>` | 监听地址列表，`"0.0.0.0:port"` 格式 |
| type | `string` | 服务器类型，默认为 `"udp"` |
| name | `string` | 服务器名称 |
| timeout | `int` | 超时时间（milliseconds） |
| recv_buf_size | `uint64_t` | recv 缓冲区大小，默认 64KB |
| io_worker / process_worker | `string` | 指定使用的 worker 名称 |
| args | `map<string,string>` | 扩展参数，子类自定义协议使用 |

### UdpServerFactory

与 `TcpServerFactory` 平行的独立工厂，支持通过 `type` 注册不同的 UDP 协议实现。

```cpp
bool UdpServerFactory::Register(const std::string& type, Creator cb);
UdpServer::ptr UdpServerFactory::Create(const UdpServerConf& conf, ...);
```

## 使用示例

### 直接编码使用

```cpp
class EchoServer : public UdpServer {
    void handleRecv(Socket::ptr sock, const char* data, size_t len,
                    Address::ptr from) override {
        INFO(LOG_ROOT()) << "recv " << len << " bytes from "
                         << from->toString();
        // 回显
        sock->sendTo(data, len, from);
    }
};

void run() {
    auto server = std::make_shared<EchoServer>();
    auto addr = Address::LookupAny("0.0.0.0:9090");
    server->bind(addr);
    server->start();
}

int main() {
    IOManager iom;
    iom.schedule(run);
    return 0;
}
```

### 自定义子类

继承 `UdpServer` 并重写 `handleRecv`，处理收到的数据报：

```cpp
class DnsServer : public UdpServer {
    void handleRecv(Socket::ptr sock, const char* data, size_t len,
                    Address::ptr from) override {
        // 解析 DNS 查询、构造回复、sendTo
    }
};
```

需要注册工厂时：

```cpp
UdpServerFactory::Register("dns", [](const UdpServerConf& conf, ...) {
    return std::make_shared<DnsServer>();
});
```

## 设计决策

1. **不继承 TcpServer**：UDP 是面向报文的，与 TCP 面向连接的语义差异太大。继承会引入大量不适用的成员（`m_activeClients`、`m_ssl`、`loadCertificates`、`closeAllClients` 等），违背接口隔离原则。

2. **独立的 UdpServerFactory**：与 `TcpServerFactory` 平行。`Application` 将来接入时需同时处理两个工厂。

3. **recvfrom 走 hooked syscall**：通过框架的 hook 机制，`::recvfrom` 会被拦截，在 EAGAIN 时将 fd 注册到 IOManager 并 yield，不会阻塞线程。
