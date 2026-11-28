#include "WSServer.hpp"

#include "../Log/Log.hpp"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

WSServer::WSServer(IOManager* worker, IOManager* io_worker, IOManager* accept_worker)
    : TcpServer(worker, io_worker, accept_worker)
{
    dispatch_.reset(new WSServletDispatch);
    type_ = "websocket_server";
}

void WSServer::trackSession(const WSSession::ptr& session)
{
    std::lock_guard lock(sessions_mutex_);
    sessions_.insert(session);
}

void WSServer::untrackSession(const WSSession::ptr& session)
{
    std::lock_guard lock(sessions_mutex_);
    sessions_.erase(session);
}

void WSServer::onDrain()
{
    std::vector<WSSession::ptr> sessions;
    {
        std::lock_guard lock(sessions_mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            auto session = it->lock();
            if (!session) {
                it = sessions_.erase(it);
                continue;
            }
            sessions.push_back(session);
            ++it;
        }
    }

    size_t notified = 0;
    for (auto& session : sessions) {
        // 有在途消息的会话由 handleClient 在处理完之后自己发 CLOSE 帧，这里不并发写 socket
        if (!isClientIdle(session->getSocket())) {
            continue;
        }
        // 只发 CLOSE 帧：对端回 CLOSE 时 recvMessage 会完成关闭；对端不回则由 drain 轮询的
        // closeIdleClients() 关掉连接
        session->sendMessage(WSFrameMessage::ptr(new WSFrameMessage(WSFrameHead::CLOSE, "")), true);
        ++notified;
    }
    INFO(logger) << "websocket server draining, notified " << notified << " idle session(s), in-flight "
                 << getInFlightClientCount();
}

void WSServer::handleClient(Socket::ptr client)
{
    DEBUG(logger) << "handleClient " << *client;
    WSSession::ptr session(std::make_shared<WSSession>(client));
    trackSession(session);
    do {
        HttpRequest::ptr header;
        {
            RequestGuard guard(this, client);
            header = session->handleShake();
        }
        if (!header) {
            DEBUG(logger) << "handleShake error";
            break;
        }
        WSServlet::ptr servlet = dispatch_->getWSServlet(header->getPath());
        if (!servlet) {
            DEBUG(logger) << "no match WSServlet";
            break;
        }
        int rt = servlet->onConnect(header, session);
        if (rt) {
            DEBUG(logger) << "onConnect return " << rt;
            break;
        }
        bool drained = false;
        try {
            while (true) {
                auto msg = session->recvMessage();
                if (!msg) {
                    break;
                }
                {
                    // 一条消息的处理就是这里的「请求」：draining 时先回 CLOSE 帧，
                    // 再由 guard 析构把在途计数归零并关连接
                    RequestGuard guard(this, client);
                    rt = servlet->handle(header, msg, session);
                    if (isDraining()) {
                        session->sendMessage(WSFrameMessage::ptr(new WSFrameMessage(WSFrameHead::CLOSE, "")), true);
                        drained = true;
                    }
                }
                if (rt) {
                    DEBUG(logger) << "handle return " << rt;
                    break;
                }
                if (drained) {
                    DEBUG(logger) << "websocket session drained, closing client:" << *client;
                    break;
                }
            }
        } catch (std::exception& e) {
            WARN(logger) << "handle exception: " << e.what();
        } catch (...) {
            WARN(logger) << "handle unknown exception";
        }
        auto slt = dispatch_->getWSServlet(header->getPath());
        if (slt) {
            slt->onClose(header, session);
        }
        servlet = slt;
    } while (0);
    session->close();
    untrackSession(session);
}

} // namespace chen::http
