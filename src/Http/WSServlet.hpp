/**
 * @file WSServlet.hpp
 * @brief WebSocket Servlet模块
 * @author Christins
 * @date 2025-05-04
 * @copyright GPL-3.0
 */
#pragma once

#include "Servlet.hpp"
#include "WSSession.hpp"

namespace chen::http {

class WSServlet : public Servlet {
public:
    typedef std::shared_ptr<WSServlet> ptr;
    WSServlet(const std::string& name);
    virtual ~WSServlet();

    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;
    virtual int32_t onConnect(HttpRequest::ptr header, WSSession::ptr session) = 0;
    virtual int32_t onClose(HttpRequest::ptr header, WSSession::ptr session) = 0;
    virtual int32_t handle(HttpRequest::ptr header, WSFrameMessage::ptr msg, WSSession::ptr session) = 0;

    const std::string& getName() const { return name_; }

protected:
    std::string name_;
};

class FunctionWSServlet : public WSServlet {
public:
    typedef std::shared_ptr<FunctionWSServlet> ptr;
    typedef std::function<int32_t(HttpRequest::ptr header, WSSession::ptr session)> on_connect_cb;
    typedef std::function<int32_t(HttpRequest::ptr header, WSSession::ptr session)> on_close_cb;
    typedef std::function<int32_t(HttpRequest::ptr header, WSFrameMessage::ptr msg, WSSession::ptr session)> callback;

    FunctionWSServlet(callback cb, on_connect_cb connect_cb = nullptr, on_close_cb close_cb = nullptr);

    virtual int32_t onConnect(HttpRequest::ptr header, WSSession::ptr session) override;
    virtual int32_t onClose(HttpRequest::ptr header, WSSession::ptr session) override;
    virtual int32_t handle(HttpRequest::ptr header, WSFrameMessage::ptr msg, WSSession::ptr session) override;

protected:
    callback callback_;
    on_connect_cb on_connect_;
    on_close_cb on_close_;
};

class WSServletDispatch : public ServletDispatch {
public:
    typedef std::shared_ptr<WSServletDispatch> ptr;

    WSServletDispatch();

    void addServlet(
        const std::string& uri,
        FunctionWSServlet::callback cb,
        FunctionWSServlet::on_connect_cb connect_cb = nullptr,
        FunctionWSServlet::on_close_cb close_cb = nullptr);

    void addGlobServlet(
        const std::string& uri,
        FunctionWSServlet::callback cb,
        FunctionWSServlet::on_connect_cb connect_cb = nullptr,
        FunctionWSServlet::on_close_cb close_cb = nullptr);

    WSServlet::ptr getWSServlet(const std::string& uri);
};

} // namespace chen::http
