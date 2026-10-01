/**
 * @file http_session.h
 * @brief 封装Server端的请求和响应
 * @author Christins
 * @date 2024-12-15
 */
#pragma once

#include <functional>
#include <string>

#include "../socket/socket_stream.h"
#include "http.h"

namespace chen::http {

class HttpSession : public SocketStream {
public:
    typedef std::shared_ptr<HttpSession> ptr;

    /// 流式 body 回调：返回 true 继续接收，false 终止
    using BodyCallback = std::function<bool(const char* data, size_t len)>;

    HttpSession(Socket::ptr sock, bool owner = true);

    /**
     * @brief 接收一个完整请求（header + body）
     * @return HttpRequest::ptr，失败返回 nullptr
     */
    virtual HttpRequest::ptr recvRequest();

    /**
     * @brief 只接收请求头，body 留给调用方处理
     * @param leftover 若非空，保存 header 之后已经多读到的 body 字节
     * @return HttpRequest::ptr（body 为空），失败返回 nullptr
     */
    virtual HttpRequest::ptr recvRequestHeader(std::string* leftover = nullptr);

    /**
     * @brief 以流式方式读取请求 body
     * @param totalLen 期望读取的总字节数（Content-Length）
     * @param cb 每收到一块数据时回调；返回 false 提前终止
     * @return 0 成功 | -1 失败
     */
    virtual int readBodyStreaming(uint64_t totalLen, BodyCallback cb);

    /**
     * @brief 将请求 body 全量读入 request（普通请求用）
     * @param req 目标请求
     * @param leftover header 之后已多读到的 body 字节
     * @return 0 成功 | -1 失败
     */
    virtual int readBodyInto(HttpRequest::ptr req, const std::string& leftover);

    /**
     * @brief 保存 header 之后已多读到的 body 字节，供流式 servlet 回放
     */
    void setPendingBody(const std::string& v) { m_pendingBody = v; }
    const std::string& getPendingBody() const { return m_pendingBody; }

    /**
     * @brief 设置当前请求剩余的 body 字节数（流式请求用，用于兜底排空）
     */
    void setBodyRemaining(uint64_t v) { m_bodyRemaining = v; }
    uint64_t getBodyRemaining() const { return m_bodyRemaining; }

    /**
     * @brief 丢弃尚未消费的 body 字节（servlet 提前返回时兜底排空，保持 keep-alive 一致性）
     * @return 0 成功 | -1 失败
     */
    virtual int drainBody();

    /**
     * @brief 发送响应（header + body 整体序列化）
     */
    virtual int sendResponse(HttpResponse::ptr rsp);

    /**
     * @brief 只发送响应状态行 + 头（不发送 body，不含自动 content-length）
     * @details 供流式响应 servlet 使用：先发头，再分块写 body。
     */
    virtual int sendResponseHeader(HttpResponse::ptr rsp);

private:
    /// header 之后已多读到的 body 字节
    std::string m_pendingBody;
    /// 流式请求尚未消费的 body 字节数
    uint64_t m_bodyRemaining = 0;
};

} // namespace chen::http
