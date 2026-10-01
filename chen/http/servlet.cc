#include "servlet.h"

#include <fnmatch.h>
#include <mutex>
#include <sstream>

namespace chen::http {

FunctionServlet::FunctionServlet(callback cb, bool streamingBody)
    : Servlet("FunctionServlet")
    , m_cb(cb)
    , m_streamingBody(streamingBody) {
}

int32_t FunctionServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response
        , HttpSession::ptr session) {
    return m_cb(request, response, session);
}

MethodServlet::MethodServlet()
    : Servlet("MethodServlet") {
}

void MethodServlet::addHandler(HttpMethod method, FunctionServlet::callback cb, bool streamingBody) {
    m_handlers[method] = cb;
    if (streamingBody) {
        m_anyStreaming = true;
    }
}

void MethodServlet::setDefaultHandler(FunctionServlet::callback cb, bool streamingBody) {
    m_defaultHandler = cb;
    if (streamingBody) {
        m_anyStreaming = true;
    }
}

int32_t MethodServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response
        , HttpSession::ptr session) {
    auto it = m_handlers.find(request->getMethod());
    if (it != m_handlers.end()) {
        return it->second(request, response, session);
    }
    if (m_defaultHandler) {
        return m_defaultHandler(request, response, session);
    }
    response->setStatus(HttpStatus::METHOD_NOT_ALLOWED);
    std::stringstream allow;
    bool first = true;
    for (auto& pair : m_handlers) {
        if (!first) allow << ", ";
        allow << HttpMethodToString(pair.first);
        first = false;
    }
    response->setHeader("Allow", allow.str());
    return 0;
}

NotFoundServlet::NotFoundServlet(const std::string& name)
    : Servlet("NotFoundServlet")
    , m_name(name) {
    m_content = "<html><head><title>404 Not Found"
        "</title></head><body><center><h1>404 Not Found</h1></center>"
        "<hr><center>" + name + "</center></body></html>";
}

int32_t NotFoundServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response
        , HttpSession::ptr session) {
    response->setStatus(HttpStatus::NOT_FOUND);
    response->setHeader("Server", m_name);
    response->setHeader("Content-Type", "text/html");
    response->setBody(m_content);

    return 0;
}

// --- ServletDispatch helpers ---

static std::vector<std::string> splitPath(const std::string& path) {
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

static bool isRestfulPattern(const std::vector<std::string>& segments) {
    for (auto& seg : segments) {
        if (!seg.empty() && seg[0] == ':') {
            return true;
        }
    }
    return false;
}

// Match a RESTful route against a request path. If request != nullptr,
// extracted path params are stored in request->m_params.
static Servlet::ptr matchRestfulRoute(const ServletDispatch::RestfulRoute& route, const std::string& path, HttpRequest::ptr request = nullptr) {
    auto pathSegs = splitPath(path);
    if (pathSegs.size() != route.segments.size()) {
        return nullptr;
    }
    for (size_t i = 0; i < route.segments.size(); ++i) {
        const std::string& seg = route.segments[i];
        if (seg.empty()) {
            continue;
        }
        if (seg[0] == ':') {
            if (request) {
                request->setParam(seg.substr(1), pathSegs[i]);
            }
        } else if (seg != pathSegs[i]) {
            return nullptr;
        }
    }
    return route.servlet;
}

// --- ServletDispatch ---

ServletDispatch::ServletDispatch()
        : Servlet("ServletDispatch") {
    m_default.reset(new NotFoundServlet("chen/1.0"));
}

int32_t ServletDispatch::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) {
    std::shared_lock lock(m_mutex);
    const std::string& path = request->getPath();

    // 1. Exact match
    auto mit = m_datas.find(path);
    if (mit != m_datas.end()) {
        mit->second->handle(request, response, session);
        return 0;
    }

    // 2. RESTful match (with path-param extraction)
    for (auto& route : m_restful) {
        auto slt = matchRestfulRoute(route, path, request);
        if (slt) {
            slt->handle(request, response, session);
            return 0;
        }
    }

    // 3. Glob match
    for (auto& [pattern, slt] : m_globs) {
        if (!fnmatch(pattern.c_str(), path.c_str(), 0)) {
            slt->handle(request, response, session);
            return 0;
        }
    }

    // 4. Default
    m_default->handle(request, response, session);
    return 0;
}

void ServletDispatch::addServlet(const std::string& uri, Servlet::ptr slt) {
    std::unique_lock lock(m_mutex);
    auto segs = splitPath(uri);
    if (isRestfulPattern(segs)) {
        // Replace existing route with same segments
        for (auto it = m_restful.begin(); it != m_restful.end(); ++it) {
            if (it->segments == segs) {
                m_restful.erase(it);
                break;
            }
        }
        m_restful.push_back({std::move(segs), slt});
    } else {
        m_datas[uri] = slt;
    }
}

void ServletDispatch::addServlet(const std::string& uri, FunctionServlet::callback cb) {
    addServlet(uri, Servlet::ptr(new FunctionServlet(cb)));
}

void ServletDispatch::addServlet(const std::string& uri, HttpMethod method, FunctionServlet::callback cb, bool streamingBody) {
    std::unique_lock lock(m_mutex);
    auto segs = splitPath(uri);
    if (isRestfulPattern(segs)) {
        // Find or create MethodServlet for this RESTful pattern
        MethodServlet::ptr ms;
        for (auto& route : m_restful) {
            if (route.segments == segs) {
                ms = std::dynamic_pointer_cast<MethodServlet>(route.servlet);
                break;
            }
        }
        if (!ms) {
            ms.reset(new MethodServlet());
            m_restful.push_back({segs, ms});
        }
        ms->addHandler(method, cb, streamingBody);
    } else {
        auto it = m_datas.find(uri);
        MethodServlet::ptr ms;
        if (it != m_datas.end()) {
            ms = std::dynamic_pointer_cast<MethodServlet>(it->second);
        }
        if (!ms) {
            ms.reset(new MethodServlet());
            m_datas[uri] = ms;
        }
        ms->addHandler(method, cb, streamingBody);
    }
}

void ServletDispatch::addGlobServlet(const std::string& uri, Servlet::ptr slt) {
    std::unique_lock lock(m_mutex);
    for (auto it = m_globs.begin(); it != m_globs.end(); ++it) {
        if (it->first == uri) {
            m_globs.erase(it);
            break;
        }
    }
    m_globs.push_back(std::pair(uri, slt));
}

void ServletDispatch::addGlobServlet(const std::string& uri, FunctionServlet::callback cb) {
    return addGlobServlet(uri, FunctionServlet::ptr(new FunctionServlet(cb)));
}

void ServletDispatch::delServlet(const std::string& uri) {
    std::unique_lock lock(m_mutex);
    m_datas.erase(uri);
    // Also try to remove RESTful route
    auto segs = splitPath(uri);
    for (auto it = m_restful.begin(); it != m_restful.end(); ++it) {
        if (it->segments == segs) {
            m_restful.erase(it);
            break;
        }
    }
}

void ServletDispatch::delGlobServlet(const std::string& uri) {
    std::unique_lock lock(m_mutex);
    for (auto it = m_globs.begin(); it != m_globs.end(); ++it) {
        if (it->first == uri) {
            m_globs.erase(it);
            break;
        }
    }
}

void ServletDispatch::clear() {
    std::unique_lock lock(m_mutex);
    m_datas.clear();
    m_restful.clear();
    m_globs.clear();
    m_default.reset();
}

void ServletDispatch::clearServlets() {
    std::unique_lock lock(m_mutex);
    m_datas.clear();
    m_restful.clear();
    m_globs.clear();
    // 保留 m_default（NotFoundServlet），不清空
}

Servlet::ptr ServletDispatch::getServlet(const std::string& uri) {
    std::shared_lock lock(m_mutex);
    auto it = m_datas.find(uri);
    if (it != m_datas.end()) {
        return it->second;
    }
    // Check RESTful patterns
    auto segs = splitPath(uri);
    for (auto& route : m_restful) {
        if (route.segments == segs) {
            return route.servlet;
        }
    }
    return nullptr;
}

Servlet::ptr ServletDispatch::getGlobServlet(const std::string& uri) {
    std::shared_lock lock(m_mutex);
    for (auto it = m_globs.begin(); it != m_globs.end(); ++it) {
        if (it->first == uri) {
            return it->second;
        }
    }
    return nullptr;
}

Servlet::ptr ServletDispatch::getMatchedServlet(const std::string& uri) {
    std::shared_lock lock(m_mutex);
    // 1. Exact match
    auto mit = m_datas.find(uri);
    if (mit != m_datas.end()) {
        return mit->second;
    }
    // 2. RESTful match (without param extraction)
    for (auto& route : m_restful) {
        auto slt = matchRestfulRoute(route, uri);
        if (slt) {
            return slt;
        }
    }
    // 3. Glob match
    for (auto& [pattern, slt] : m_globs) {
        if (!fnmatch(pattern.c_str(), uri.c_str(), 0)) {
            return slt;
        }
    }
    return m_default;
}

Servlet::ptr ServletDispatch::matchForRequest(HttpRequest::ptr request) {
    std::shared_lock lock(m_mutex);
    const std::string& path = request->getPath();

    // 1. Exact match
    auto mit = m_datas.find(path);
    if (mit != m_datas.end()) {
        return mit->second;
    }
    // 2. RESTful match (with path-param extraction)
    for (auto& route : m_restful) {
        auto slt = matchRestfulRoute(route, path, request);
        if (slt) {
            return slt;
        }
    }
    // 3. Glob match
    for (auto& [pattern, slt] : m_globs) {
        if (!fnmatch(pattern.c_str(), path.c_str(), 0)) {
            return slt;
        }
    }
    // 4. Default
    return m_default;
}

} // namespace chen::http
