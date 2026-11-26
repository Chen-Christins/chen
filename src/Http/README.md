# HTTP — HTTP/HTTP2/WebSocket/SSE

完整的 HTTP 协议栈实现。

## 使用示例

```cpp
// HttpServer + servlet
HttpServer::ptr server(new HttpServer);
server->bind(addr);
server->getServletDispatch()->addServlet("/api", [](HttpRequest::ptr req
    , HttpResponse::ptr rsp, HttpSession::ptr session) {
    rsp->setBody("{\"status\":\"ok\"}");
    return 0;
});
server->start();

// WebSocket server
WSServer::ptr ws(new WSServer);
ws->bind(ws_addr);
ws->getWSServletDispatch()->addServlet("/chat", my_chat_servlet);
ws->start();

// WebSocket client
auto conn = WSConnection::Create("ws://127.0.0.1:8020/chat", 3000).second;
conn->sendMessage("hello");
auto msg = conn->recvMessage();
```

## 协议支持

| 协议      | 核心类                                  | 说明                                     |
|-----------|-----------------------------------------|------------------------------------------|
| HTTP/1.1  | `HttpConnection`, `HttpServer`          | Ragel 解析器，支持路由、中间件、静态文件 |
| HTTP/2    | `H2Connection`, `H2Session`, `H2Stream` | 基于 nghttp2，多路复用                   |
| WebSocket | `WSConnection`, `WSServer`              | 协议升级、帧编解码                       |
| SSE       | `SSEServlet`, `SSESession`, `SSEClient` | Server-Sent Events 流式推送（H1/H2）     |

## HTTP/1.1

- Ragel 状态机解析 HTTP 请求/响应
- `HttpServer` 继承 `TcpServer`，在 `handleClient` 中创建 `HttpConnection`
- Servlet 分发：路径匹配 → Handler 回调
- 支持静态文件服务、Range 请求

## 异步请求（HttpClient）

`HttpClient` 把同步的 `HttpConnection::DoRequest` 提交到调度器执行，配合 hook 实现"写入即异步"：

```cpp
#include "Chen/Http/HttpClient.hpp"

// 1) 只关心发出去、失败打日志（fire-and-forget）
chen::http::HttpClient::submit(
    chen::http::HttpMethod::POST, url, /*timeout_ms=*/3000, /*cb=*/nullptr,
    {{"Content-Type", "application/json"}}, payload);

// 2) 需要结果：提交后立刻返回，之后在 fiber 里 get() 只挂起协程
auto future = chen::http::HttpClient::post(url, 3000, {{"Content-Type", "application/json"}}, payload);
// ... 中间可以做别的事
chen::http::HttpResult::ptr result = future.get();

// 3) 回调形式
chen::http::HttpClient::submit(chen::http::HttpMethod::GET, url, 3000,
    [](chen::http::HttpResult::ptr r) {
        if (!r || r->result != 0) { /* 失败处理 */ }
    });
```

要点：

- `submit()` 返回 `false` 表示当前线程没有可用调度器（`IOManager` → `Scheduler` 都取不到），任务未提交，调用方可降级为同步发送
- `request()`/`get()` 等在无调度器时返回一个已完成、值为 `nullptr` 的 `Future`
- 并发能力来自 fiber + hook：同一线程上多个请求会各自挂起，不会互相阻塞
- **DNS 解析（`getaddrinfo`）不在 hook 范围内**，域名请求会在调度线程上短暂阻塞
- 便捷方法：`get/post/put/patch/del/head/options`，另有通用 `request(method, ...)`

### 连接复用（连接池）

`HttpClient` 默认按 `scheme://host:port` 复用连接，省掉每次请求的 TCP/TLS 握手：

| 配置项                       | 默认值 | 说明                     |
|------------------------------|--------|--------------------------|
| `http.client.pool_enable`    | true   | 是否复用连接池           |
| `http.client.pool_size`      | 8      | 每个 host 的空闲连接上限 |
| `http.client.max_alive_time` | 30000  | 连接最长存活毫秒数       |
| `http.client.max_request`    | 100    | 单条连接最大复用次数     |
| `http.client.ssl_verify`     | true   | https 是否校验服务端证书 |

- 复用连接时请求发送 `Connection: keep-alive`；服务端返回 `Connection: close` 时该连接不回池
- 池中连接可能已被对端关闭（TCP 半关闭对 `isConnected()` 不可见），遇到 `SEND_CLOSE_BY_PEER` / `SEND_SOCKET_ERROR`
  会自动换新连接重试一次
- https 连接会带 SNI，并用 URL 里的 host 校验证书链与主机名（`http.client.ssl_verify` 可关闭，自签名服务用）
- 想禁用复用可把 `http.client.pool_enable` 设为 false；改配置后调用 `HttpClient::clearPools()` 让下次请求重建连接池（需无在途请求）

## HTTP/2

- 基于 nghttp2 库，`H2Session` 管理连接级别状态
- `H2Stream` 表示单个流，支持 Server Push
- HPACK 头部压缩

### 流式响应（H1 / H2 通用）

`HttpSession` 提供两个虚接口，业务 servlet 不需要区分协议：

```cpp
int32_t handle(chen::http::HttpRequest::ptr req,
               chen::http::HttpResponse::ptr rsp,
               chen::http::HttpSession::ptr session) override
{
    rsp->setStreaming(true);                       // 告诉框架不要再自动发响应
    rsp->setHeader("Content-Type", "text/plain");
    if (session->sendResponseHeader(rsp) != 0) {   // 只发头，不带 END_STREAM / Content-Length
        return -1;
    }
    session->sendData("chunk-1\n", 8, false);
    session->sendData("chunk-2\n", 8, false);
    session->sendData(nullptr, 0, true);           // 最后一段：结束流
    return 0;
}
```

- **HTTP/1.1**：`sendResponseHeader()` 写状态行 + 头，`sendData()` 直接写 socket；框架在处理函数返回后关闭连接，
  因此流式响应不要依赖 keep-alive
- **HTTP/2**：`sendResponseHeader()` 提交不带 `END_STREAM` 的 HEADERS 帧，`sendData()` 投递 DATA 命令，
  由连接协程通过 nghttp2 data provider 发送；`end_stream=true` 打上 `END_STREAM`
- **背压**：每条流有 1MB 待发水位，超过时 `sendData()` 通过 hook 后的 `usleep` 让出 fiber，等连接协程取走数据；
  等待超过 5s 返回 -1
- `H2Stream` 析构时若还没 `end_stream`，会兜底补一个空的结束帧
- SSE 已经建立在同一套接口上：`SSESession` 只是把事件编码成 `data:`/`id:`/`event:` 文本，写入统一走
  `sendResponseHeader()` + `sendData()`，因此 `SSEServlet` 在 H1 与 H2 下行为一致（见下节）

## HTTP 请求指标

`HttpServer` 与 `H2Session` 在 dispatch 前后自动打点（`src/Http/HttpMetrics.hpp`），两个协议共用同一套指标：

| 指标                            | 类型      | 标签                       | 含义                                                        |
|---------------------------------|-----------|----------------------------|-------------------------------------------------------------|
| `http_requests_total`           | counter   | `method`、`path`、`status` | 处理完成的请求数                                            |
| `http_request_duration_seconds` | histogram | `method`、`path`           | 从进入 dispatch 到响应写回（流式响应只统计到 servlet 返回） |
| `http_requests_in_flight`       | gauge     | 无                         | 正在处理的请求数                                            |

- `path` 取自请求行（不含 query），会做归一化：空路径记 `/`、合并重复 `/`、去掉末尾 `/`
- 不同 `(method, path)` 组合超过 `http.metrics.max_paths`（默认 512）后，**新**路径统一记为 `other`，
  避免 404 扫描把指标基数打爆；`http.metrics.enabled=false` 可关掉总量与耗时（在途计数仍维护）
- 指标注册在进程级 Registry，用 `MetricsServlet` 挂 `/metrics` 即可采集（见 `src/Metrics/README.md`）

## WebSocket

- `WSConnection` 处理升级握手和帧读写
- 支持文本/二进制帧，分片消息
- 通过 `WSServlet` 分发消息

## SSE

- `SSEServlet`：业务基类，实现 `onConnect()` / `onClose()`；`FunctionSSEServlet` 用回调快速接入
- `SSESession`：事件编码 + 发送，`sendEvent()` / `sendComment()` / `sendRetry()` / `closeStream()`
- `SSEClient`：客户端侧解析 `text/event-stream`

```cpp
class ChatServlet : public chen::http::SSEServlet {
public:
    ChatServlet() : SSEServlet("chat") {}

    int32_t onConnect(chen::http::HttpRequest::ptr req, chen::http::SSESession::ptr sess) override
    {
        sess->sendEvent("connected", "1", "hello");   // id: 1 / event: hello / data: connected
        sess->sendComment("ping");                     // 心跳：": ping"
        sess->closeStream();                           // H2 发 END_STREAM；H1 由框架关闭连接
        return 0;
    }
    int32_t onClose(chen::http::HttpRequest::ptr, chen::http::SSESession::ptr) override { return 0; }
};
```

- 响应头固定为 `Content-Type: text/event-stream`、`Cache-Control: no-cache`，不带 `Content-Length`：
  H1 靠关闭连接结束响应，H2 靠 `END_STREAM`
- `SSESession(HttpSession::ptr)` 复用当前请求会话（H1 写 socket、H2 走 HEADERS/DATA 帧）；
  旧的 `SSESession(Socket::ptr, bool)` 仍保留，仅适用于 HTTP/1.1
