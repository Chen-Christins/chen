#include "sse_servlet.h"

#include "../log/log.h"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

SSEServlet::SSEServlet(const std::string& name)
    : Servlet(name)
    , m_name(name) {
}

int32_t SSEServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) {
    response->setStreaming(true);

    auto sock = session->getSocket();
    SSESession::ptr sse_sess(new SSESession(sock, false));

    if (!sse_sess->sendSSEHeaders()) {
        WARN(logger) << "sendSSEHeaders fail";
        return -1;
    }

    int32_t rt = onConnect(request, sse_sess);
    onClose(request, sse_sess);
    return rt;
}

FunctionSSEServlet::FunctionSSEServlet(on_connect_cb connect_cb, on_close_cb close_cb)
    : SSEServlet("FunctionSSEServlet")
    , m_onConnect(connect_cb)
    , m_onClose(close_cb) {
}

int32_t FunctionSSEServlet::onConnect(HttpRequest::ptr request, SSESession::ptr session) {
    if (m_onConnect) {
        return m_onConnect(request, session);
    }
    return 0;
}

int32_t FunctionSSEServlet::onClose(HttpRequest::ptr request, SSESession::ptr session) {
    if (m_onClose) {
        return m_onClose(request, session);
    }
    return 0;
}

} // namespace chen::http
