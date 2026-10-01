/**
 * @file http_server.h
 * @brief HttpServer模块
 * @author Christins
 * @date 2024-12-15
 */
#pragma once

#include "../tcp/tcp_server.h"
#include "servlet.h"

namespace chen::http {

class HttpServer : public TcpServer {
public:
    typedef std::shared_ptr<HttpServer> ptr;
    HttpServer(bool keepalive = false
              ,IOManager* worker = IOManager::GetThis()
              ,IOManager* io_worker = IOManager::GetThis()
              ,IOManager* accept_worker = IOManager::GetThis());
    virtual void setName(const std::string& v) override;
    ServletDispatch::ptr getServletDispatch() const { return m_pendingDispatch ? m_pendingDispatch : m_dispatch; }
    void setDispatch(ServletDispatch::ptr v) { m_dispatch = v; }

    void setNegotiateH2(bool v) { m_negotiateH2 = v; }
    bool isNegotiateH2() const { return m_negotiateH2; }

    /**
     * @brief 设置待提交的 dispatch（热重载双缓冲）
     * @details 设置后 getServletDispatch() 返回新的 pending dispatch，
     *          模块在 onActivate() 中注册到此 dispatch。
     *          commitDispatch() 后此指针被清空。
     */
    void setPendingDispatch(ServletDispatch::ptr v) { m_pendingDispatch = v; }

    void prepareDispatch() override;
    void commitDispatch() override;

protected:
    virtual void handleClient(Socket::ptr client) override;

private:
    bool m_isKeepAlive;
    bool m_negotiateH2 = false;
    /// 当前活跃的 dispatch（所有当前请求走这个）
    ServletDispatch::ptr m_dispatch;
    /// 待提交的 dispatch（热重载时新模块注册到这里）
    ServletDispatch::ptr m_pendingDispatch;
};

} // namespace chen::http
