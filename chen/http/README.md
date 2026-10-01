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

| 协议 | 核心类 | 说明 |
|------|--------|------|
| HTTP/1.1 | `HttpConnection`, `HttpServer` | Ragel 解析器，支持路由、中间件、静态文件 |
| HTTP/2 | `H2Connection`, `H2Session`, `H2Stream` | 基于 nghttp2，多路复用 |
| WebSocket | `WSConnection`, `WSServer` | 协议升级、帧编解码 |
| SSE | `SSEConnection` | Server-Sent Events 流式推送 |

## HTTP/1.1

- Ragel 状态机解析 HTTP 请求/响应
- `HttpServer` 继承 `TcpServer`，在 `handleClient` 中创建 `HttpConnection`
- Servlet 分发：路径匹配 → Handler 回调
- 支持静态文件服务、Range 请求

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
