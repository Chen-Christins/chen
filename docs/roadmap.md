# 版本路线图

本文档规划框架后续版本的演进方向与范围。当前版本为 v1.5.5。

## 总览

| 版本 | 主题 | 核心内容 |
|------|------|---------|
| **v1.6.0** | 可观测性与稳定性 | 异步日志、Metrics/Admin、访问日志、真正优雅 draining、测试/CI 基线 |
| v1.7.0 | HTTPS 与 HTTP 完整化 | TLS 校验/SNI/ALPN/mTLS/证书热加载、H2 over TLS、静态文件/Range/CORS/gzip/中间件、H2 streaming |
| v1.8.0 | 动态配置与协议扩展 | `servers` 动态增删/重绑、UDP 接入 Application、异步 DNS、WS 心跳/deflate/Origin、RPC 重试/熔断/TLS/压缩 |
| v1.9.0 | 弹性与生态 | 细粒度限流/鉴权、OpenTelemetry tracing、Admin UI/配置校验、压测与容量模型 |

---

## v1.6.0 — 可观测性与稳定性

本版本聚焦生产可用性：让服务跑起来后「看得见、停得稳、查得到问题」。

详细实施计划见 [`v1.6.0-plan.md`](v1.6.0-plan.md)。

已确认的设计决策：

| 决策项 | 结论 |
|--------|------|
| Admin 服务形态 | 独立 `type: admin` server，独立端口 |
| 长连接 draining | HTTP/2 发 GOAWAY、WS 发 CLOSE 1001、SSE/流式直接关闭，deadline 后强断 |
| 访问日志格式 | 单行 JSON |
| Readiness 范围 | 仅全局 `ServiceState`，v1.6.0 不做业务探针 |

核心内容：

- **异步日志**：有界队列 + 独立日志线程、溢出策略、fork/reload 安全、日志指标
- **Metrics**：Counter/Gauge/Histogram 注册表、Prometheus `/metrics`、内置运行时/网络/RPC 指标
- **Admin/Health**：独立 `admin` server、`/healthz`、`/readyz`、`/debug/servers`、`/debug/version`
- **访问日志**：HTTP/1.1 + HTTP/2 单行 JSON、采样、请求 ID
- **优雅 draining**：停止 accept → 等在途完成 → deadline 强断，覆盖 HTTP/WS/RPC/Game/H2
- **测试/CI**：ctest 接入、新功能测试、GitHub Actions 构建 + 测试 + ASAN

### 明确不做（留给后续版本）

TLS/HTTPS/H2 over TLS、静态文件与中间件、动态监听管理、UDP 接入、异步 DNS、RPC 韧性、业务 readiness 探针。

---

## v1.7.0 — HTTPS 与 HTTP 完整化

### TLS / HTTPS

- 修复客户端证书校验缺失：`SSL_connect` 增加 verify、SNI
- 支持 ALPN，补齐 HTTP/2 over TLS（当前仅 h2c）
- 支持 mTLS、多域名 SNI
- 证书/私钥热加载，配合 inotify 实现证书轮换不重启
- 修复非阻塞 `SSL_connect` / `SSL_read` / `SSL_write` 的 `WANT_READ` / `WANT_WRITE` 处理

### HTTP 服务端

- 静态文件 Servlet：`sendfile`、Range、ETag、Last-Modified、If-Modified-Since
- CORS 中间件
- 响应 gzip/deflate 压缩
- 通用中间件/拦截器管线（鉴权、日志、限流、请求 ID）
- HTTP/2 流式响应：补齐 `h2_session.cc` 中的 `DATA` 命令 TODO，支持大文件与 SSE over H2

---

## v1.8.0 — 动态配置与协议扩展

### 动态配置

- `servers` 配置热更新：增删监听、修改端口/证书无需重启
- master 侧监听 socket 生命周期管理，配合 worker reload 平滑切换

### 协议扩展

- UDP 接入 `Application`：`UdpServerFactory` 与配置解析纳入统一启动流程
- 异步 DNS 解析：避免 `getaddrinfo` 阻塞 IO 线程，支持超时与 IPv6 happy-eyeballs
- WebSocket：permessage-deflate、自动 ping/pong 心跳、Origin 校验
- RPC：重试、熔断、TLS、压缩

---

## v1.9.0 — 弹性与生态

- 细粒度限流：按 IP / 路由 / 方法 / 用户维度
- 鉴权与 Session：JWT、Cookie Session、CSRF
- OpenTelemetry tracing（HTTP/RPC/DB 链路）
- Admin UI、配置 schema 校验与配置 dump
- 压测基准与容量模型文档

---

## 已确认决策

- v1.6.0 按「可观测性优先」推进，TLS/HTTPS 放到 v1.7.0。
- 部署形态为 master + 单个 active worker；Metrics 无需跨进程聚合。
- 异步日志 v1.6.0 默认关闭，示例配置开启；v1.7.0 再评估默认开启。
- v1.6.0 的 Admin、draining、访问日志与 readiness 方案见 [`v1.6.0-plan.md`](v1.6.0-plan.md)。
