/**
 * @file sse_servlet.h
 * @brief SSE (Server-Sent Events) Servlet 模块
 * @author Christins
 * @date 2026-05-14
 * @copyright GPL-3.0
 */
#pragma once

#include "servlet.h"
#include "sse_session.h"

namespace chen::http {

class SSEServlet : public Servlet {
public:
    typedef std::shared_ptr<SSEServlet> ptr;
    SSEServlet(const std::string& name);
    virtual ~SSEServlet() {}

    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

    virtual int32_t onConnect(HttpRequest::ptr request, SSESession::ptr session) = 0;
    virtual int32_t onClose(HttpRequest::ptr request, SSESession::ptr session) = 0;

    const std::string& getName() const { return m_name; }

protected:
    std::string m_name;
};

class FunctionSSEServlet : public SSEServlet {
public:
    typedef std::shared_ptr<FunctionSSEServlet> ptr;
    typedef std::function<int32_t(HttpRequest::ptr request, SSESession::ptr session)> on_connect_cb;
    typedef std::function<int32_t(HttpRequest::ptr request, SSESession::ptr session)> on_close_cb;

    FunctionSSEServlet(on_connect_cb connect_cb, on_close_cb close_cb = nullptr);

    virtual int32_t onConnect(HttpRequest::ptr request, SSESession::ptr session) override;
    virtual int32_t onClose(HttpRequest::ptr request, SSESession::ptr session) override;

protected:
    on_connect_cb m_onConnect;
    on_close_cb m_onClose;
};

} // namespace chen::http
