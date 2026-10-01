/**
 * @file sse_session.h
 * @brief SSE (Server-Sent Events) Session 封装
 * @author Christins
 * @date 2026-05-14
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>

#include "http_session.h"

namespace chen::http {

class SSESession : public HttpSession {
public:
    typedef std::shared_ptr<SSESession> ptr;
    SSESession(Socket::ptr sock, bool owner = true);

    /**
     * @brief 发送 SSE 响应头
     * @return bool 成功 true | 失败 false
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
};

} // namespace chen::http
