/**
 * @file WSServer.hpp
 * @brief WebSocket Server程序
 * @author Christins
 * @date 2025-05-04
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <set>

#include "../Tcp/TcpServer.hpp"
#include "WSServlet.hpp"

namespace chen::http {

class WSServer : public TcpServer {
public:
    typedef std::shared_ptr<WSServer> ptr;

    WSServer(IOManager* worker = IOManager::GetThis(), IOManager* io_worker = IOManager::GetThis(), IOManager* accept_worker = IOManager::GetThis());

    WSServletDispatch::ptr getWSServletDispatch() const { return dispatch_; }
    void setWSServletDispatch(WSServletDispatch::ptr v) { dispatch_ = v; }

    /**
     * @brief WebSocket 以「一条消息的处理」作为请求边界，支持协作式 drain
     */
    virtual bool supportsGracefulDrain() const override { return true; }

protected:
    virtual void handleClient(Socket::ptr client) override;

    /**
     * @brief 进入 draining：给空闲会话回 CLOSE 帧，在途会话由 handleClient 收尾
     * @details 只处理没有在途消息的会话，避免和业务 fiber 并发写同一个 socket 导致帧交错；
     *          发完帧不直接关连接（等对端回 CLOSE，或由 closeIdleClients() 关闭）。
     */
    virtual void onDrain() override;

    WSServletDispatch::ptr dispatch_;

private:
    void trackSession(const WSSession::ptr& session);
    void untrackSession(const WSSession::ptr& session);

    std::mutex sessions_mutex_;
    std::set<std::weak_ptr<WSSession>, std::owner_less<std::weak_ptr<WSSession>>> sessions_;
};

} // namespace chen::http
