#include "Servlet.hpp"

#include <fnmatch.h>
#include <mutex>
#include <sstream>

namespace chen::http {

Servlet::Servlet(const std::string& name)
    : name_(name)
{
}

Servlet::~Servlet()
{
}

bool Servlet::isStreamingBody() const
{
    return false;
}

FunctionServlet::FunctionServlet(callback cb, bool streaming_body)
    : Servlet("FunctionServlet")
    , cb_(cb)
    , streaming_body_(streaming_body)
{
}

int32_t FunctionServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
{
    return cb_(request, response, session);
}

MethodServlet::MethodServlet()
    : Servlet("MethodServlet")
{
}

void MethodServlet::addHandler(HttpMethod method, FunctionServlet::callback cb, bool streaming_body)
{
    handlers_[method] = cb;
    if (streaming_body) {
        any_streaming_ = true;
    }
}

void MethodServlet::setDefaultHandler(FunctionServlet::callback cb, bool streaming_body)
{
    default_handler_ = cb;
    if (streaming_body) {
        any_streaming_ = true;
    }
}

int32_t MethodServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
{
    auto it = handlers_.find(request->getMethod());
    if (it != handlers_.end()) {
        return it->second(request, response, session);
    }
    if (default_handler_) {
        return default_handler_(request, response, session);
    }
    response->setStatus(HttpStatus::METHOD_NOT_ALLOWED);
    std::stringstream allow;
    bool first = true;
    for (auto& pair : handlers_) {
        if (!first)
            allow << ", ";
        allow << HttpMethodToString(pair.first);
        first = false;
    }
    response->setHeader("Allow", allow.str());
    return 0;
}

NotFoundServlet::NotFoundServlet(const std::string& name)
    : Servlet("NotFoundServlet")
    , name_(name)
{
    content_ =
        "<html><head><title>404 Not Found</title></head><body><center><h1>404 Not Found</h1></center><hr><center>" + name + "</center></body></html>";
}

int32_t NotFoundServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
{
    response->setStatus(HttpStatus::NOT_FOUND);
    response->setHeader("Server", name_);
    response->setHeader("Content-Type", "text/html");
    response->setBody(content_);

    return 0;
}

// --- ServletDispatch helpers ---

static std::vector<std::string> splitPath(const std::string& path)
{
    std::vector<std::string> result;
    size_t start = 0;
    if (!path.empty() && path[0] == '/') {
        start = 1;
    }
    for (;;) {
        size_t pos = path.find('/', start);
        if (pos == std::string::npos) {
            break;
        }
        if (pos > start) {
            result.push_back(path.substr(start, pos - start));
        }
        start = pos + 1;
    }
    if (start < path.size()) {
        result.push_back(path.substr(start));
    }
    return result;
}

static bool isRestfulPattern(const std::vector<std::string>& segments)
{
    for (auto& seg : segments) {
        if (!seg.empty() && seg[0] == ':') {
            return true;
        }
    }
    return false;
}

// Match a RESTful route against a request path. If request != nullptr,
// extracted path params are stored in request->params_.
static Servlet::ptr matchRestfulRoute(const ServletDispatch::RestfulRoute& route, const std::string& path, HttpRequest::ptr request = nullptr)
{
    auto path_segs = splitPath(path);
    if (path_segs.size() != route.segments.size()) {
        return nullptr;
    }
    for (size_t i = 0; i < route.segments.size(); ++i) {
        const std::string& seg = route.segments[i];
        if (seg.empty()) {
            continue;
        }
        if (seg[0] == ':') {
            if (request) {
                request->setParam(seg.substr(1), path_segs[i]);
            }
        } else if (seg != path_segs[i]) {
            return nullptr;
        }
    }
    return route.servlet;
}

// --- ServletDispatch ---

ServletDispatch::ServletDispatch()
    : Servlet("ServletDispatch")
{
    default_.reset(new NotFoundServlet("chen/1.0"));
}

int32_t ServletDispatch::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
{
    std::shared_lock lock(mutex_);
    const std::string& path = request->getPath();

    // 1. Exact match
    auto mit = datas_.find(path);
    if (mit != datas_.end()) {
        mit->second->handle(request, response, session);
        return 0;
    }

    // 2. RESTful match (with path-param extraction)
    for (auto& route : restful_) {
        auto slt = matchRestfulRoute(route, path, request);
        if (slt) {
            slt->handle(request, response, session);
            return 0;
        }
    }

    // 3. Glob match
    for (auto& [pattern, slt] : globs_) {
        if (!fnmatch(pattern.c_str(), path.c_str(), 0)) {
            slt->handle(request, response, session);
            return 0;
        }
    }

    // 4. Default
    default_->handle(request, response, session);
    return 0;
}

void ServletDispatch::addServlet(const std::string& uri, Servlet::ptr slt)
{
    std::unique_lock lock(mutex_);
    auto segs = splitPath(uri);
    if (isRestfulPattern(segs)) {
        // Replace existing route with same segments
        for (auto it = restful_.begin(); it != restful_.end(); ++it) {
            if (it->segments == segs) {
                restful_.erase(it);
                break;
            }
        }
        restful_.push_back({std::move(segs), slt});
    } else {
        datas_[uri] = slt;
    }
}

void ServletDispatch::addServlet(const std::string& uri, FunctionServlet::callback cb)
{
    addServlet(uri, Servlet::ptr(new FunctionServlet(cb)));
}

void ServletDispatch::addServlet(const std::string& uri,
    HttpMethod method,
    FunctionServlet::callback cb,
    bool streaming_body)
{
    std::unique_lock lock(mutex_);
    auto segs = splitPath(uri);
    if (isRestfulPattern(segs)) {
        // Find or create MethodServlet for this RESTful pattern
        MethodServlet::ptr ms;
        for (auto& route : restful_) {
            if (route.segments == segs) {
                ms = std::dynamic_pointer_cast<MethodServlet>(route.servlet);
                break;
            }
        }
        if (!ms) {
            ms.reset(new MethodServlet());
            restful_.push_back({segs, ms});
        }
        ms->addHandler(method, cb, streaming_body);
    } else {
        auto it = datas_.find(uri);
        MethodServlet::ptr ms;
        if (it != datas_.end()) {
            ms = std::dynamic_pointer_cast<MethodServlet>(it->second);
        }
        if (!ms) {
            ms.reset(new MethodServlet());
            datas_[uri] = ms;
        }
        ms->addHandler(method, cb, streaming_body);
    }
}

void ServletDispatch::addGlobServlet(const std::string& uri, Servlet::ptr slt)
{
    std::unique_lock lock(mutex_);
    for (auto it = globs_.begin(); it != globs_.end(); ++it) {
        if (it->first == uri) {
            globs_.erase(it);
            break;
        }
    }
    globs_.push_back(std::pair(uri, slt));
}

void ServletDispatch::addGlobServlet(const std::string& uri, FunctionServlet::callback cb)
{
    return addGlobServlet(uri, FunctionServlet::ptr(new FunctionServlet(cb)));
}

void ServletDispatch::delServlet(const std::string& uri)
{
    std::unique_lock lock(mutex_);
    datas_.erase(uri);
    // Also try to remove RESTful route
    auto segs = splitPath(uri);
    for (auto it = restful_.begin(); it != restful_.end(); ++it) {
        if (it->segments == segs) {
            restful_.erase(it);
            break;
        }
    }
}

void ServletDispatch::delGlobServlet(const std::string& uri)
{
    std::unique_lock lock(mutex_);
    for (auto it = globs_.begin(); it != globs_.end(); ++it) {
        if (it->first == uri) {
            globs_.erase(it);
            break;
        }
    }
}

void ServletDispatch::clear()
{
    std::unique_lock lock(mutex_);
    datas_.clear();
    restful_.clear();
    globs_.clear();
    default_.reset();
}

void ServletDispatch::clearServlets()
{
    std::unique_lock lock(mutex_);
    datas_.clear();
    restful_.clear();
    globs_.clear();
    // 保留 default_（NotFoundServlet），不清空
}

Servlet::ptr ServletDispatch::getServlet(const std::string& uri)
{
    std::shared_lock lock(mutex_);
    auto it = datas_.find(uri);
    if (it != datas_.end()) {
        return it->second;
    }
    // Check RESTful patterns
    auto segs = splitPath(uri);
    for (auto& route : restful_) {
        if (route.segments == segs) {
            return route.servlet;
        }
    }
    return nullptr;
}

Servlet::ptr ServletDispatch::getGlobServlet(const std::string& uri)
{
    std::shared_lock lock(mutex_);
    for (auto it = globs_.begin(); it != globs_.end(); ++it) {
        if (it->first == uri) {
            return it->second;
        }
    }
    return nullptr;
}

Servlet::ptr ServletDispatch::getMatchedServlet(const std::string& uri)
{
    std::shared_lock lock(mutex_);
    // 1. Exact match
    auto mit = datas_.find(uri);
    if (mit != datas_.end()) {
        return mit->second;
    }
    // 2. RESTful match (without param extraction)
    for (auto& route : restful_) {
        auto slt = matchRestfulRoute(route, uri);
        if (slt) {
            return slt;
        }
    }
    // 3. Glob match
    for (auto& [pattern, slt] : globs_) {
        if (!fnmatch(pattern.c_str(), uri.c_str(), 0)) {
            return slt;
        }
    }
    return default_;
}

Servlet::ptr ServletDispatch::matchForRequest(HttpRequest::ptr request)
{
    std::shared_lock lock(mutex_);
    const std::string& path = request->getPath();

    // 1. Exact match
    auto mit = datas_.find(path);
    if (mit != datas_.end()) {
        return mit->second;
    }
    // 2. RESTful match (with path-param extraction)
    for (auto& route : restful_) {
        auto slt = matchRestfulRoute(route, path, request);
        if (slt) {
            return slt;
        }
    }
    // 3. Glob match
    for (auto& [pattern, slt] : globs_) {
        if (!fnmatch(pattern.c_str(), path.c_str(), 0)) {
            return slt;
        }
    }
    // 4. Default
    return default_;
}

} // namespace chen::http
