#include "WSServlet.hpp"

namespace chen::http {

WSServlet::WSServlet(const std::string& name)
    : Servlet(name)
    , name_(name)
{
}

WSServlet::~WSServlet()
{
}

int32_t WSServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
{
    return 0;
}

FunctionWSServlet::FunctionWSServlet(callback cb, on_connect_cb connect_cb, on_close_cb close_cb)
    : WSServlet("FunctionWSServlet")
    , callback_(cb)
    , on_connect_(connect_cb)
    , on_close_(close_cb)
{
}

int32_t FunctionWSServlet::onConnect(HttpRequest::ptr header, WSSession::ptr session)
{
    if (on_connect_) {
        return on_connect_(header, session);
    }
    return 0;
}

int32_t FunctionWSServlet::onClose(HttpRequest::ptr header, WSSession::ptr session)
{
    if (on_close_) {
        return on_close_(header, session);
    }
    return 0;
}

int32_t FunctionWSServlet::handle(HttpRequest::ptr header, WSFrameMessage::ptr msg, WSSession::ptr session)
{
    if (callback_) {
        return callback_(header, msg, session);
    }
    return 0;
}

WSServletDispatch::WSServletDispatch()
{
    name_ = "WSServletDispatch";
}

void WSServletDispatch::addServlet(
    const std::string& uri,
    FunctionWSServlet::callback cb,
    FunctionWSServlet::on_connect_cb connect_cb,
    FunctionWSServlet::on_close_cb close_cb)
{
    ServletDispatch::addServlet(uri, std::make_shared<FunctionWSServlet>(cb, connect_cb, close_cb));
}

void WSServletDispatch::addGlobServlet(
    const std::string& uri,
    FunctionWSServlet::callback cb,
    FunctionWSServlet::on_connect_cb connect_cb,
    FunctionWSServlet::on_close_cb close_cb)
{
    ServletDispatch::addGlobServlet(uri, std::make_shared<FunctionWSServlet>(cb, connect_cb, close_cb));
}

WSServlet::ptr WSServletDispatch::getWSServlet(const std::string& uri)
{
    auto slt = getMatchedServlet(uri);
    return std::dynamic_pointer_cast<WSServlet>(slt);
}

} // namespace chen::http
