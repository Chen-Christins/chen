/**
 * @file servlet.h
 * @brief 仿照java的那种，处理Http的uri
 * @author Christins
 * @date 2025-01-17
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "http.h"
#include "http_session.h"

namespace chen::http {

class Servlet {
public:
    typedef std::shared_ptr<Servlet> ptr;
    Servlet(const std::string& name) : m_name(name) {}
    virtual ~Servlet() {}
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) = 0;

    /**
     * @brief 是否以流式方式接收请求 body
     * @details 返回 true 时，服务端不会在 dispatch 前全量读取 body，
     *          而是由 servlet 内部通过 session->readBodyStreaming() 自行分块读取。
     */
    virtual bool isStreamingBody() const { return false; }

    const std::string& getName() const { return m_name; }

protected:
    std::string m_name;
};

class FunctionServlet : public Servlet {
public:
    typedef std::shared_ptr<FunctionServlet> ptr;
    typedef std::function<int32_t(HttpRequest::ptr request, HttpResponse::ptr response
                        ,HttpSession::ptr session)> callback;

    FunctionServlet(callback cb, bool streamingBody = false);
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

    bool isStreamingBody() const override { return m_streamingBody; }

private:
    callback m_cb;
    bool m_streamingBody;
};

class MethodServlet : public Servlet {
public:
    typedef std::shared_ptr<MethodServlet> ptr;

    MethodServlet();

    void addHandler(HttpMethod method, FunctionServlet::callback cb, bool streamingBody = false);
    void setDefaultHandler(FunctionServlet::callback cb, bool streamingBody = false);

    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

    /// 任一 handler 声明流式 body 即为 true
    bool isStreamingBody() const override { return m_anyStreaming; }

private:
    std::map<HttpMethod, FunctionServlet::callback> m_handlers;
    FunctionServlet::callback m_defaultHandler;
    bool m_anyStreaming = false;
};

class NotFoundServlet : public Servlet {
public:
    typedef std::shared_ptr<NotFoundServlet> ptr;
    NotFoundServlet(const std::string& name);
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

private:
    std::string m_name;
    std::string m_content;
};

class ServletDispatch : public Servlet {
public:
    typedef std::shared_ptr<ServletDispatch> ptr;

    ServletDispatch();
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

    void addServlet(const std::string& uri, Servlet::ptr slt);
    void addServlet(const std::string& uri, FunctionServlet::callback cb);
    void addServlet(const std::string& uri, HttpMethod method, FunctionServlet::callback cb, bool streamingBody = false);
    void addGlobServlet(const std::string& uri, Servlet::ptr slt);
    void addGlobServlet(const std::string& uri, FunctionServlet::callback cb);

    void delServlet(const std::string& uri);
    void delGlobServlet(const std::string& uri);

    /// 清空所有已注册的 servlet（热重载 onUnload 时调用）
    void clear();

    /**
     * @brief 清空所有已注册的 servlet（保留默认 NotFoundServlet）
     * @details 用于热重载的蓝绿部署：先清空 dispatch，再 onServerReady 重新注册。
     *          与 clear() 不同，此方法保留 m_default，避免清空期间请求访问空指针崩溃。
     */
    void clearServlets();

    Servlet::ptr getDefault() const { return m_default; }
    void setDefault(Servlet::ptr v) { m_default = v; }

    Servlet::ptr getServlet(const std::string& uri);
    Servlet::ptr getGlobServlet(const std::string& uri);

    Servlet::ptr getMatchedServlet(const std::string& uri);

    /**
     * @brief 匹配请求对应的 servlet 并抽取 path 参数，但不调用 handle
     * @param request 已解析出 path 的请求对象；匹配到的 RESTful 参数会写入 request->m_params
     * @return 匹配到的 Servlet；无匹配时返回默认 Servlet
     */
    Servlet::ptr matchForRequest(HttpRequest::ptr request);

    struct RestfulRoute {
        std::vector<std::string> segments;  // e.g. ["api", "user", ":id"]
        Servlet::ptr servlet;
    };

private:

    // uri(/chen/xxx) -> servlet
    std::unordered_map<std::string, Servlet::ptr> m_datas;
    // RESTful routes: /api/user/:id
    std::vector<RestfulRoute> m_restful;
    // uri(/chen/*) -> servlet
    std::vector<std::pair<std::string, Servlet::ptr>> m_globs;
    // 默认的 Servlet，所有的路径都没有匹配到的时候使用这个默认的
    Servlet::ptr m_default;
    // 写锁
    std::shared_mutex m_mutex;
};

} // namespace chen::http
