/**
 * @file ws_connection.h
 * @brief WebSocket连接模块
 * @author Christins
 * @date 2025-05-05
 * @copyright GPL-3.0
 */
#pragma once

#include "http_connection.h"
#include "ws_session.h"

namespace chen::http {

class WSConnection : public HttpConnection {
public:
    typedef std::shared_ptr<WSConnection> ptr;

    WSConnection(Socket::ptr sock, bool owner = true);

    static std::pair<HttpResult::ptr, WSConnection::ptr> Create(const std::string& url
            , uint64_t timeout_ms
            , const std::map<std::string, std::string>& headers = {});

    static std::pair<HttpResult::ptr, WSConnection::ptr> Create(Uri::ptr uri
            , uint64_t timeout_ms
            , const std::map<std::string, std::string>& headers = {});
    
    WSFrameMessage::ptr recvMessage();
    int32_t sendMessage(WSFrameMessage::ptr msg, bool fin = true);
    int32_t sendMessage(const std::string& msg, int32_t opcode = WSFrameHead::TEXT_FRAME, bool fin = true);
    int32_t ping();
    int32_t pong();
};

} // namespace chen::http
