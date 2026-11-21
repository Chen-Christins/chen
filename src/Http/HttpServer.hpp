/**
 * @file HttpServer.hpp
 * @brief HttpServer模块
 * @author Christins
 * @date 2024-12-15
 * @copyright GPL-3.0
 */
#pragma once

#include "../Tcp/TcpServer.hpp"
#include "Servlet.hpp"

namespace chen::http {

class HttpServer : public TcpServer {
public:
    typedef std::shared_ptr<HttpServer> ptr;
    HttpServer(
        bool keepalive = false,
        IOManager* worker = IOManager::GetThis(),
        IOManager* io_worker = IOManager::GetThis(),
        IOManager* accept_worker = IOManager::GetThis());
    virtual void setName(const std::string& v) override;
    ServletDispatch::ptr getServletDispatch() const { return dispatch_; }
    void setDispatch(ServletDispatch::ptr v) { dispatch_ = v; }

    void setNegotiateH2(bool v) { negotiate_h2_ = v; }
    bool isNegotiateH2() const { return negotiate_h2_; }

protected:
    virtual void handleClient(Socket::ptr client) override;

private:
    bool is_keep_alive_;
    bool negotiate_h2_ = false;
    /// 当前活跃的 dispatch（所有当前请求走这个）
    ServletDispatch::ptr dispatch_;
};

} // namespace chen::http
