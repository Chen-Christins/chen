#include "SSEServlet.hpp"

#include "../Log/Log.hpp"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

SSEServlet::SSEServlet(const std::string& name)
    : Servlet(name)
    , name_(name)
{
}

SSEServlet::~SSEServlet()
{
}

int32_t SSEServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
{
    response->setStreaming(true);

    // 复用当前请求的会话：H1 写 socket，H2 走 HEADERS/DATA 帧
    SSESession::ptr sse_sess(new SSESession(session));

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
    , on_connect_(connect_cb)
    , on_close_(close_cb)
{
}

int32_t FunctionSSEServlet::onConnect(HttpRequest::ptr request, SSESession::ptr session)
{
    if (on_connect_) {
        return on_connect_(request, session);
    }
    return 0;
}

int32_t FunctionSSEServlet::onClose(HttpRequest::ptr request, SSESession::ptr session)
{
    if (on_close_) {
        return on_close_(request, session);
    }
    return 0;
}

} // namespace chen::http
