#include "ws_server.h"

#include "../log/log.h"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

WSServer::WSServer(IOManager* worker, IOManager* io_worker, IOManager* accept_worker) 
        : TcpServer(worker, io_worker, accept_worker) {
    m_dispatch.reset(new WSServletDispatch);
    m_type = "websocket_server";
}

void WSServer::prepareDispatch() {
    m_pendingDispatch.reset(new WSServletDispatch);
}

void WSServer::commitDispatch() {
    if (m_pendingDispatch) {
        m_dispatch = m_pendingDispatch;
        m_pendingDispatch.reset();
    }
}

void WSServer::handleClient(Socket::ptr client) {
    DEBUG(logger) << "handleClient " << *client;
    WSSession::ptr session(std::make_shared<WSSession>(client));
    do {
        HttpRequest::ptr header = session->handleShake();
        if (!header) {
            DEBUG(logger) << "handleShake error";
            break;
        }
        WSServlet::ptr servlet = m_dispatch->getWSServlet(header->getPath());
        if (!servlet) {
            DEBUG(logger) << "no match WSServlet";
            break;
        }
        int rt = servlet->onConnect(header, session);
        if (rt) {
            DEBUG(logger) << "onConnect return " << rt;
            break;
        }
        try {
            while (true) {
                auto msg = session->recvMessage();
                if (!msg) {
                    break;
                }
                rt = servlet->handle(header, msg, session);
                if (rt) {
                    DEBUG(logger) << "handle return " << rt;
                    break;
                }
            }
        } catch (std::exception& e) {
            WARN(logger) << "handle exception: " << e.what();
        } catch (...) {
            WARN(logger) << "handle unknown exception";
        }
        auto slt = m_dispatch->getWSServlet(header->getPath());
        if (slt) {
            slt->onClose(header, session);
        }
        servlet = slt;
    } while (0);
    session->close();
}

} // namespace chen::http
