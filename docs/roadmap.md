# 版本路线图

本文档规划框架后续版本的演进方向与范围。当前版本为 v1.6.0。

## 总览

| 版本               | 主题                 | 核心内容                                                                                                |
|--------------------|----------------------|---------------------------------------------------------------------------------------------------------|
| **v1.6.0**（当前） | 指标与进程模型       | Metrics、master/worker 热重载与优雅 draining                                                            |
| v1.7.0             | HTTPS 与 HTTP 完整化 | TLS 校验/SNI/ALPN/mTLS/证书热加载、H2 over TLS、静态文件/Range/CORS/gzip/中间件、H2 streaming           |
| v1.8.0             | 动态配置与协议扩展   | `servers` 动态增删/重绑、UDP 接入 Application、异步 DNS、WS 心跳/deflate/Origin、RPC 重试/熔断/TLS/压缩 |
| v1.9.0             | 弹性与生态           | 细粒度限流/鉴权、OpenTelemetry tracing、Admin UI/配置校验、压测与容量模型                               |
| v2.0.0             | 分布式与多活         | 服务发现/注册中心、弹性扩缩容、Hub 高可用、跨区多活、K8s/LB 集成                                        |

---

## v1.6.0 — 指标与进程模型（当前版本）

v1.6.0 已发布（2026-10-18），交付内容是 Metrics 采集能力和 master/worker 多进程热重载 + 优雅 draining。

### Metrics

- `chen/metrics/`：`Counter` / `Gauge` / `GaugeFunc` / `Histogram`，支持标签、同名同标签复用、类型冲突检测
- `RegistryMgr` 进程级注册表，线程安全；Counter / Histogram 每线程分片
- Prometheus / OpenMetrics / JSON 渲染
- `chen/http/metrics_servlet.h`：业务模块在 `onServerReady()` 里挂采集接口

### 热重载与优雅 draining

- master 持有监听 socket 与 pidfile，`SIGHUP` fork 新 worker
- 新 worker ready 后，旧 worker 停止 accept、`closeAllClients()`、限时 drain（`server.drain_timeout_ms`，默认 3000ms），再
  `onDeactivate()` → `onUnload()` 退出
- worker 非预期崩溃由 master 自动重启（带 backoff）
- 配置：`server.worker_ready_timeout_ms`、`server.worker_restart_interval_ms`

### 业务侧职责

- 异步日志：框架日志为同步实现，业务按需自行封装
- Admin / Health：业务按需自行实现

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

## v2.0.0 — 分布式与多活

当前（v1.x）的分布式形态是「多进程部署 + RPC Hub 中心转发」：

- 业务进程作为 `RpcClient` 连到 Hub，通过 `registerService(peer_id, func_id, instance_id, bind_ids)` 注册
- Hub 是单点 relay（`RpcServer` + `relay: "1"`），只做转发，不持久化、不重放
- `peer_id` / `func_id` / `instance_id` 全部由配置分配，没有服务发现
- 部署形态为 master + 单个 active worker，热重载够用，但不支持多活 / 弹性扩缩容
- 传输帧里的 `region` 字段是预留，跨区路由尚未实现

v2.0.0 计划补齐下面这些能力；如果短期不做，则明确走「外部注册中心 + 外部负载均衡」的集成方案。

### 服务发现与注册中心

- 实例自动注册 / 注销、健康检查、元数据，替代配置分配 `peer_id` / `instance_id`
- 对接 Nacos / Consul / K8s Service，或提供内置轻量注册中心
- Hub 地址发现与故障切换，客户端不再硬编码 hub 地址

### 弹性扩缩容

- 新实例注册后自动进入 GROUPID 动态拓扑；缩容时先摘流，等在途请求完成再下线
- 与 draining / readiness 打通，扩缩容过程不丢请求
- 负载均衡策略：加权、最少连接、一致性哈希（当前只有取模）

### 多活与高可用

- Hub 多实例、注册表同步 / 主备切换，消除 relay 单点
- 落地 `region`：同城 / 异地多活、就近接入、跨区路由
- 跨区数据同步的冲突解决与最终一致性约定

### 与外部基础设施集成

- Kubernetes：Service / Endpoint、Headless Service、Pod readiness 与实例注册联动
- 外部 L4/L7 负载均衡：框架只暴露健康检查与指标，不重复造轮子
- 可选：服务网格 / Sidecar 模式下的地址与协议兼容

### 边界

- 分布式事务、跨分片查询 / JOIN 不在框架范围内
- 数据一致性仍由业务定义（幂等、版本号、补偿）

---

## 已确认决策

- v1.6.0 聚焦 Metrics 与 master/worker 热重载、优雅 draining；TLS/HTTPS 放到 v1.7.0。
- 部署形态为 master + 单个 active worker；Metrics 无需跨进程聚合。
- `docs/v1.6.0-plan.md` 为规划稿，其中异步日志、Admin/Health、访问日志未纳入 v1.6.0；实际交付以 CHANGELOG 为准。
- v1.x 的分布式形态为「多进程 + RPC Hub 中心转发」：Hub 单点、无服务发现、无弹性扩缩容，这些能力列入
  v2.0.0；短期可选择外部注册中心 + 外部负载均衡。
