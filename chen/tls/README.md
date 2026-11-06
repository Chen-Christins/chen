# TLS — TLS 配置与上下文

`chen/tls/` 把 OpenSSL 的 `SSL_CTX` 生命周期、证书加载、版本/套件限制、ALPN 和校验策略收敛到统一模块，
供 `socket`、`tcp`、`http` 等上层复用。

## 目录

| 文件                | 说明                                                        |
|---------------------|-------------------------------------------------------------|
| `tls_config.h/.cc`  | `TlsConfig` / `SniCert` / TLS 版本解析 / 配置校验           |
| `tls_context.h/.cc` | `TlsContext`：`SSL_CTX` 的 RAII 封装、ALPN 回调、`reload()` |

依赖方向：

```
tls ← socket ← tcp ← http
```

`tls` 不依赖 socket / tcp / http，也不直接读取 YAML；配置映射由上层（`TcpServerConf`）完成。

## 核心类

| 类           | 说明                                                       |
|--------------|------------------------------------------------------------|
| `TlsConfig`  | 服务端/客户端共用的 TLS 配置                               |
| `SniCert`    | SNI 备用证书配置（M2 使用）                                |
| `TlsContext` | 持有一份 `SSL_CTX`，提供创建、读取、原子 reload、ALPN 查询 |

## 使用示例

### 服务端

```cpp
#include "chen/tls/tls_context.h"

chen::tls::TlsConfig conf = chen::tls::TlsConfig::DefaultServer();
conf.cert_file = "/etc/chen/certs/server.crt";
conf.key_file = "/etc/chen/certs/server.key";
conf.min_version = "TLS1.2";
conf.alpn = {"h2", "http/1.1"};

auto ctx = chen::tls::TlsContext::CreateServer(conf);
if (!ctx) {
    // 配置或证书错误，已打印日志
}

// 创建 SSL 时使用 ctx->getShared()，保证 reload 期间旧 ctx 不被释放
std::shared_ptr<SSL_CTX> ssl_ctx = ctx->getShared();
SSL* ssl = SSL_new(ssl_ctx.get());
```

### 客户端

```cpp
chen::tls::TlsConfig conf = chen::tls::TlsConfig::DefaultClient();
conf.ca_file = "/etc/chen/certs/ca.crt";   // 私有 CA；为空时使用系统 CA
conf.verify_peer = true;
conf.min_version = "TLS1.2";

auto ctx = chen::tls::TlsContext::CreateClient(conf);
```

### 握手后读取 ALPN

```cpp
std::string proto = chen::tls::TlsContext::GetAlpn(ssl); // "h2" / "http/1.1" / ""
bool is_h2 = chen::tls::TlsContext::IsAlpnH2(ssl);
```

## 配置项

| 字段                     | 默认值   | 说明                                                        |
|--------------------------|----------|-------------------------------------------------------------|
| `enabled`                | `false`  | 是否启用 TLS；`CreateServer/CreateClient` 会强制置为 `true` |
| `server`                 | `true`   | `true` 服务端 / `false` 客户端                              |
| `cert_file` / `key_file` | 空       | 服务端必填；客户端可选（双向 TLS）                          |
| `ca_file` / `ca_path`    | 空       | 校验对端证书的 CA；为空时用系统 CA                          |
| `ciphers`                | 空       | TLS 1.2 及以下的 cipher list                                |
| `ciphersuites`           | 空       | TLS 1.3 ciphersuites（OpenSSL 1.1.1+）                      |
| `min_version`            | `TLS1.2` | 支持 `TLS1.0` ~ `TLS1.3`，空表示不限制                      |
| `max_version`            | 空       | 空表示不限制                                                |
| `verify_peer`            | `true`   | 客户端是否校验证书链与主机名                                |
| `verify_client`          | `false`  | 服务端是否要求客户端证书（mTLS，M2 接入配置）               |
| `verify_depth`           | `4`      | 证书链最大深度                                              |
| `alpn`                   | 空       | 由上层按协议类型决定；HTTP 服务端默认 `h2,http/1.1`         |
| `prefer_server_ciphers`  | `true`   | 服务端是否优先使用自己的 cipher 顺序                        |
| `session_tickets`        | `true`   | 服务端是否启用 session ticket                               |
| `handshake_timeout_ms`   | `10000`  | TLS 握手超时                                                |

### ALPN 约定

- 服务端 `alpn` 为空时不做 ALPN 协商；HTTP/HTTP2 服务端由 `TcpServerConf` 注入默认
  `["h2", "http/1.1"]`。
- 客户端 `TlsConfig::DefaultClient()` 默认只声明 `http/1.1`，避免普通 HTTPS 客户端误协商到 h2。
  需要 h2 的客户端显式设置 `alpn = {"h2"}`。
- 服务端按配置顺序选择双方都支持的协议；没有交集时返回 `SSL_TLSEXT_ERR_NOACK`，握手继续，
  由 HTTP 层决定是否拒绝连接。

## 生命周期与线程安全

- `TlsContext` 内部用 `shared_ptr<SSL_CTX>` 持有上下文；`getShared()` 返回一份引用，调用期间不会因
  `reload()` 被释放。
- `reload()` 会重新读取证书/私钥，成功后一次性替换内部 `SSL_CTX`；失败保留旧上下文。
- 已建立的 `SSL` 对象由 OpenSSL 自身引用计数持有旧 `SSL_CTX`，热替换不会影响存量连接。
- `getConfig()` 返回创建时的配置，创建后不可变。

## 当前范围

已实现：

- 服务端/客户端上下文创建
- 证书链、私钥匹配检查
- CA 校验、校验深度
- TLS 版本、cipher/ciphersuite 限制
- ALPN 服务端选择、握手结果查询
- `reload()` 原子替换

未实现（后续里程碑）：

- SNI 多证书与 `SSL_CTX` 切换（M2）
- 证书文件监听、debounce、自动 reload（M2）
- mTLS 的 YAML 配置接入（M2）
- `TlsContextMgr` 进程级管理（M2）
