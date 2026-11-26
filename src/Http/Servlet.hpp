/**
 * @file Servlet.hpp
 * @brief 仿照java的那种，处理Http的uri
 * @author Christins
 * @date 2025-01-17
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Http.hpp"
#include "HttpSession.hpp"

namespace chen::http {

class Servlet {
public:
    typedef std::shared_ptr<Servlet> ptr;
    Servlet(const std::string& name);
    virtual ~Servlet();
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) = 0;

    /**
     * @brief 是否以流式方式接收请求 body
     * @details 返回 true 时，服务端不会在 dispatch 前全量读取 body，
     *          而是由 servlet 内部通过 session->readBodyStreaming() 自行分块读取。
     */
    virtual bool isStreamingBody() const;

    const std::string& getName() const { return name_; }

protected:
    std::string name_;
};

class FunctionServlet : public Servlet {
public:
    typedef std::shared_ptr<FunctionServlet> ptr;
    typedef std::function<int32_t(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)> callback;

    FunctionServlet(callback cb, bool streaming_body = false);
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

    bool isStreamingBody() const override { return streaming_body_; }

private:
    callback cb_;
    bool streaming_body_;
};

class MethodServlet : public Servlet {
public:
    typedef std::shared_ptr<MethodServlet> ptr;

    MethodServlet();

    void addHandler(HttpMethod method, FunctionServlet::callback cb, bool streaming_body = false);
    void setDefaultHandler(FunctionServlet::callback cb, bool streaming_body = false);

    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

    /// 任一 handler 声明流式 body 即为 true
    bool isStreamingBody() const override { return any_streaming_; }

private:
    std::map<HttpMethod, FunctionServlet::callback> handlers_;
    FunctionServlet::callback default_handler_;
    bool any_streaming_ = false;
};

class NotFoundServlet : public Servlet {
public:
    typedef std::shared_ptr<NotFoundServlet> ptr;
    NotFoundServlet(const std::string& name);
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

private:
    std::string name_;
    std::string content_;
};

class ServletDispatch : public Servlet {
public:
    typedef std::shared_ptr<ServletDispatch> ptr;

    ServletDispatch();
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

    void addServlet(const std::string& uri, Servlet::ptr slt);
    void addServlet(const std::string& uri, FunctionServlet::callback cb);
    void addServlet(const std::string& uri,
        HttpMethod method,
        FunctionServlet::callback cb,
        bool streaming_body = false);
    void addServlet(const std::string& uri, HttpMethod method, Servlet::ptr slt, bool streaming_body = false);
    void addGlobServlet(const std::string& uri, Servlet::ptr slt);
    void addGlobServlet(const std::string& uri, FunctionServlet::callback cb);

    void delServlet(const std::string& uri);
    void delGlobServlet(const std::string& uri);

    void clear();

    void clearServlets();

    Servlet::ptr getDefault() const { return default_; }
    void setDefault(Servlet::ptr v) { default_ = v; }

    Servlet::ptr getServlet(const std::string& uri);
    Servlet::ptr getGlobServlet(const std::string& uri);

    Servlet::ptr getMatchedServlet(const std::string& uri);

    Servlet::ptr matchForRequest(HttpRequest::ptr request);

    struct RestfulRoute {
        std::vector<std::string> segments; // e.g. ["api", "user", ":id"]
        Servlet::ptr servlet;
    };

private:
    // uri(/chen/xxx) -> servlet
    std::unordered_map<std::string, Servlet::ptr> datas_;
    // RESTful routes: /api/user/:id
    std::vector<RestfulRoute> restful_;
    // uri(/chen/*) -> servlet
    std::vector<std::pair<std::string, Servlet::ptr>> globs_;
    // 默认的 Servlet，所有的路径都没有匹配到的时候使用这个默认的
    Servlet::ptr default_;
    // 写锁
    std::shared_mutex mutex_;
};

} // namespace chen::http
