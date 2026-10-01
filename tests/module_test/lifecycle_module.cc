#include "lifecycle_module.h"
#include "chen/log/log.h"
#include "chen/application.h"
#include "chen/http/http_server.h"
#include "chen/http/servlet.h"

#include <atomic>
#include <sstream>

namespace module_test {

static chen::Logger::ptr logger = LOG_NAME("system");
static std::atomic<int> g_instanceCount{0};

// ─── 一个极简的 HTTP Servlet，用于验证 handler 注册/切换 ───────────────
class LifecycleServlet : public chen::http::Servlet {
public:
    typedef std::shared_ptr<LifecycleServlet> ptr;

    LifecycleServlet(int module_id)
        : chen::http::Servlet("lifecycle_servlet")
        , m_moduleId(module_id) {}

    int32_t handle(chen::http::HttpRequest::ptr request,
                   chen::http::HttpResponse::ptr response,
                   chen::http::HttpSession::ptr session) override {
        std::stringstream ss;
        ss << "{\"module_id\":" << m_moduleId
           << ",\"path\":\"" << request->getPath()
           << "\",\"method\":\"" << chen::http::HttpMethodToString(request->getMethod())
           << "\"}";
        response->setBody(ss.str());
        response->setHeader("Content-Type", "application/json");
        return 0;
    }

private:
    int m_moduleId;
};

// ─── LifecycleModule 实现 ─────────────────────────────────────────────

LifecycleModule::LifecycleModule()
    : chen::Module("lifecycle_test", "2.0", "lifecycle_module.so")
    , m_id(++g_instanceCount) {
    INFO(logger) << "LifecycleModule() id=" << m_id
                 << " this=" << (void*)this;
}

LifecycleModule::~LifecycleModule() {
    INFO(logger) << "~LifecycleModule() id=" << m_id
                 << " this=" << (void*)this
                 << " ← THIS MEANS DLCLOSE HAPPENED!";
}

bool LifecycleModule::onLoad() {
    INFO(logger) << ">>> [id=" << m_id << "] onLoad";
    return true;
}

bool LifecycleModule::onUnload() {
    INFO(logger) << ">>> [id=" << m_id << "] onUnload";
    return true;
}

bool LifecycleModule::onActivate() {
    INFO(logger) << ">>> [id=" << m_id << "] onActivate (calling onServerReady)";
    return Module::onActivate();
}

bool LifecycleModule::onDeactivate() {
    INFO(logger) << ">>> [id=" << m_id << "] onDeactivate";
    return true;
}

bool LifecycleModule::onServerReady() {
    INFO(logger) << ">>> [id=" << m_id << "] onServerReady — registering servlet";

    // 注册一个 HTTP servlet，路径 /lifecycle_test
    std::vector<chen::http::HttpServer::ptr> http_servers;
    chen::Module::getAllHttpServer(http_servers);
    for (auto& server : http_servers) {
        auto dispatch = server->getServletDispatch();
        if (dispatch) {
            dispatch->addGlobServlet("/lifecycle_test",
                std::make_shared<LifecycleServlet>(m_id));
            INFO(logger) << ">>> [id=" << m_id << "] registered /lifecycle_test on server="
                         << server->getName();
        }
    }
    return true;
}

bool LifecycleModule::onServerUp() {
    INFO(logger) << ">>> [id=" << m_id << "] onServerUp";
    return true;
}

void LifecycleModule::onTick() {
    INFO(logger) << ">>> [id=" << m_id << "] onTick";
}

uint64_t LifecycleModule::getTickIntervalMs() {
    return 5000; // 5 秒一次
}

} // namespace module_test

// ─── .so 导出函数 ────────────────────────────────────────────────────

extern "C" {

chen::Module* CreateModule() {
    INFO(module_test::logger) << "CreateModule";
    return new module_test::LifecycleModule;
}

void DestroyModule(chen::Module* module) {
    INFO(module_test::logger) << "DestroyModule id="
        << static_cast<module_test::LifecycleModule*>(module);
    delete module;
}

}
