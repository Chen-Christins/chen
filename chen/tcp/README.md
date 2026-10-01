# TcpServer — TCP 服务器

通用 TCP 服务器框架，支持 SSL/TLS，可扩展自定义协议。

## 核心类

- **TcpServer** — TCP 服务器基类
- **TcpServerConf** — 服务器配置（地址、类型、超时、SSL 证书等）
- **TcpServerFactory** — 工厂模式，按 type 创建不同协议的服务器实例

## 工作流程

```
bind → listen → start() → accept fiber 循环 → handleClient(client)
                         ↑
                    stop() → 关闭监听 socket → accept fiber 退出
```

- **accept worker**：负责 accept 新连接
- **io worker**：负责处理已连接客户端的 I/O
- `m_isStop` 控制 accept 循环，`stop()` 异步调度 socket 关闭任务

## 子类

- `HttpServer` — HTTP/HTTP2 服务器
- `RpcServer` — RPC 服务器
- `GenericProtocolServer` — 通用二进制协议服务器（Game 协议）

## 配置

```yaml
server:
  - type: http
    name: game_http_server
    address: ["0.0.0.0:8080"]
    timeout: 30000
    ssl: false
```

## 连接限流

每个 server 可通过 `args` 配置连接级限流（在 accept 阶段生效，超限直接关闭新连接并记日志）：

```yaml
server:
  - type: http
    address: ["0.0.0.0:8080"]
    args:
      accept_qps: 1000      # 每秒最多接受的新连接数（<= 0 或缺失 = 不限速）
      accept_burst: 2000    # 瞬时突发上限（缺省 = accept_qps）
      max_conn: 10000       # 最大并发连接数（<= 0 或缺失 = 不限）
```

- 限流实现见 `chen/rate_limiter/`：`RateLimiter`（令牌桶）+ `ConnLimiter`（准入控制器，组合速率与并发上限）。
- 三个参数都不配置时，不启用限流（零开销）。
- 覆盖 http / http2 / ws / rpc / game 全部 `TcpServer` 类型。
