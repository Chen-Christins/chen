/**
 * @file WSServer.hpp
 * @brief WebSocket Server程序
 * @author Christins
 * @date 2025-05-04
 * @copyright GPL-3.0
 */
#pragma once

#include "../Tcp/TcpServer.hpp"
#include "WSServlet.hpp"

namespace chen::http {

class WSServer : public TcpServer {
public:
    typedef std::shared_ptr<WSServer> ptr;

    WSServer(IOManager* worker = IOManager::GetThis(), IOManager* io_worker = IOManager::GetThis(), IOManager* accept_worker = IOManager::GetThis());

    WSServletDispatch::ptr getWSServletDispatch() const { return dispatch_; }
    void setWSServletDispatch(WSServletDispatch::ptr v) { dispatch_ = v; }

protected:
    virtual void handleClient(Socket::ptr client) override;

protected:
    WSServletDispatch::ptr dispatch_;
};

} // namespace chen::http
