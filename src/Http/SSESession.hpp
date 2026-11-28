/**
 * @file SSESession.hpp
 * @brief SSE (Server-Sent Events) Session 封装
 * @author Christins
 * @date 2026-05-14
 * @copyright GPL-3.0
 * @details 所有写操作都走 `HttpSession` 的流式接口（`sendResponseHeader()` /
 *          `sendData()`），因此同一份业务代码在 HTTP/1.1 与 HTTP/2 下都能用：
 *          - H1：状态行 + 头直接写 socket，body 逐段写；
 *          - H2：头走 HEADERS 帧，body 走 DATA 帧（由 `H2Stream` 覆写）。
 */
#pragma once

#include <memory>

#include "HttpSession.hpp"

namespace chen::http {

class SSESession : public HttpSession {
public:
    typedef std::shared_ptr<SSESession> ptr;

    /**
     * @brief 基于已有 HTTP 会话构造（推荐，H1/H2 通用）
     * @param[in] session 当前请求的 HttpSession（H1 为 HttpSession，H2 为 H2Stream）
     */
    explicit SSESession(HttpSession::ptr session);

    /**
     * @brief 基于裸 socket 构造（兼容旧用法，仅支持 HTTP/1.1）
     * @param[in] sock 已建立的连接
     * @param[in] owner 是否由本对象接管 socket 生命周期
     */
    SSESession(Socket::ptr sock, bool owner = true);

    /**
     * @brief 发送 SSE 响应头
     * @return bool 成功 true | 失败 false
     * @details 头里不带 Content-Length：SSE 是无限流，靠连接关闭（H1）或
     *          END_STREAM（H2）结束。
     */
    bool sendSSEHeaders();

    /**
     * @brief 发送一条 SSE 事件
     * @param data 事件数据
     * @param id 可选事件 ID
     * @param event 可选事件类型
     * @return int32_t 失败 -1 | 成功 >=0
     */
    int32_t sendEvent(const std::string& data, const std::string& id = "", const std::string& event = "");

    /**
     * @brief 发送 SSE 注释（可用作 keep-alive 心跳）
     * @param comment 注释内容
     * @return int32_t 失败 -1 | 成功 >=0
     */
    int32_t sendComment(const std::string& comment = "");

    /**
     * @brief 发送 retry 指令，告知客户端重连间隔
     * @param milliseconds 重连间隔（毫秒）
     * @return int32_t 失败 -1 | 成功 >=0
     */
    int32_t sendRetry(uint32_t milliseconds);

    /**
     * @brief 结束事件流
     * @return int32_t 失败 -1 | 成功 >=0
     * @details H2 发送 END_STREAM；H1 由框架在 servlet 返回后关闭连接。
     */
    int32_t closeStream();

private:
    /// 底层 HTTP 会话（H1 或 H2）
    HttpSession::ptr session_;
};

} // namespace chen::http
