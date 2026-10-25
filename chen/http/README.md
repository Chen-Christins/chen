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
| SSE       | `SSEConnection`                         | Server-Sent Events 流式推送              |

## HTTP/1.1

- Ragel 状态机解析 HTTP 请求/响应
- `HttpServer` 继承 `TcpServer`，在 `handleClient` 中创建 `HttpConnection`
- Servlet 分发：路径匹配 → Handler 回调
- 支持静态文件服务、Range 请求

## 异步请求（HttpClient）

`HttpClient` 把同步的 `HttpConnection::DoRequest` 提交到调度器执行，配合 hook 实现"写入即异步"：

```cpp
#include "chen/http/http_client.h"

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
- **DNS 解析（`getaddrinfo`）不在 hook 范围内**，域名请求会在调度线程上短暂阻塞，高频场景建议用 IP 或加连接池
- 便捷方法：`get/post/put/patch/del/head/options`，另有通用 `request(method, ...)`

## HTTP/2

- 基于 nghttp2 库，`H2Session` 管理连接级别状态
- `H2Stream` 表示单个流，支持 Server Push
- HPACK 头部压缩

## WebSocket

- `WSConnection` 处理升级握手和帧读写
- 支持文本/二进制帧，分片消息
- 通过 `WSServlet` 分发消息

## SSE

- `SSEConnection` 维护 `text/event-stream` 响应
- 支持自动重连、事件 ID、多频道广播
- `SSEManager` 管理所有 SSE 连接
