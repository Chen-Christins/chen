/**
 * @file ws_server.h
 * @brief WebSocket Server程序
 * @author Christins
 * @date 2025-05-04
 * @copyright GPL-3.0
 */
#pragma once

#include "../tcp/tcp_server.h"
#include "ws_servlet.h"

namespace chen::http {

class WSServer : public TcpServer {
public:
    typedef std::shared_ptr<WSServer> ptr;

    WSServer(IOManager* worker = IOManager::GetThis()
            , IOManager* io_worker = IOManager::GetThis()
            , IOManager* accept_worker = IOManager::GetThis());

    WSServletDispatch::ptr getWSServletDispatch() const { return m_dispatch; }
    void setWSServletDispatch(WSServletDispatch::ptr v) { m_dispatch = v; }

protected:
    virtual void handleClient(Socket::ptr client) override;

protected:
    WSServletDispatch::ptr m_dispatch;
};

} // namespace chen::http
