/**
 * @file HttpServer.hpp
 * @brief HttpServer模块
 * @author Christins
 * @date 2024-12-15
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <set>

#include "../Tcp/TcpServer.hpp"
#include "Servlet.hpp"

namespace chen::http {

class H2Session;

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

    /**
     * @brief HTTP/1.1 与 HTTP/2 支持协作式 drain
     * @details HTTP/1.1 在 handleClient 内用 RequestGuard 标记在途请求；HTTP/2 会话整体
     *          计入在途，drain 时由 onDrain() 逐个发送 GOAWAY，等 stream 结束后退出。
     */
    virtual bool supportsGracefulDrain() const override { return true; }

protected:
    virtual void handleClient(Socket::ptr client) override;

    /**
     * @brief draining 时向所有活跃 HTTP/2 会话发送 GOAWAY
     */
    virtual void onDrain() override;

private:
    bool is_keep_alive_;
    bool negotiate_h2_ = false;
    /// 当前活跃的 dispatch（所有当前请求走这个）
    ServletDispatch::ptr dispatch_;
    /// 活跃的 HTTP/2 会话（drain 时逐个发 GOAWAY；weak_ptr 避免延长生命周期）
    std::mutex h2_mutex_;
    std::set<std::weak_ptr<H2Session>, std::owner_less<std::weak_ptr<H2Session>>> h2_sessions_;
};

} // namespace chen::http
