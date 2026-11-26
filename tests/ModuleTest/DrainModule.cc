/**
 * @file DrainModule.cc
 * @brief drain/热重载集成测试模块实现
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "DrainModule.hpp"

#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "src/Application.hpp"
#include "src/Http/HttpServer.hpp"
#include "src/Http/Servlet.hpp"
#include "src/Log/Log.hpp"

namespace module_test {

static chen::Logger::ptr logger = LOG_NAME("drain_test");

namespace {

/**
 * @brief 睡指定毫秒后返回 slow-ok
 * @details usleep 已被框架 hook，睡眠期间让出 io 线程，不会阻塞事件循环
 */
class SlowServlet : public chen::http::Servlet {
public:
    SlowServlet()
        : chen::http::Servlet("drain_slow")
    {
    }

    int32_t handle(chen::http::HttpRequest::ptr request,
                   chen::http::HttpResponse::ptr response,
                   chen::http::HttpSession::ptr session) override
    {
        (void)session;

        int delay_ms = 800;
        const std::string ms = request->getParam("ms");
        if (!ms.empty()) {
            char* end = nullptr;
            const long v = std::strtol(ms.c_str(), &end, 10);
            if (end != ms.c_str() && v >= 0 && v <= 30000) {
                delay_ms = static_cast<int>(v);
            }
        }

        if (delay_ms > 0) {
            usleep(static_cast<useconds_t>(delay_ms) * 1000);
        }

        response->setHeader("Content-Type", "text/plain");
        response->setBody("slow-ok");
        return 0;
    }
};

/**
 * @brief 立即返回 fast-ok，用于探活
 */
class FastServlet : public chen::http::Servlet {
public:
    FastServlet()
        : chen::http::Servlet("drain_fast")
    {
    }

    int32_t handle(chen::http::HttpRequest::ptr request,
                   chen::http::HttpResponse::ptr response,
                   chen::http::HttpSession::ptr session) override
    {
        (void)request;
        (void)session;
        response->setHeader("Content-Type", "text/plain");
        response->setBody("fast-ok");
        return 0;
    }
};

} // namespace

DrainModule::DrainModule()
    : chen::Module("drain_test", "1.0.0", "drain_test_module")
{
}

bool DrainModule::onLoad()
{
    INFO(logger) << "DrainModule onLoad";
    return true;
}

bool DrainModule::onServerReady()
{
    // 不能按配置里的 type 字符串筛选（旧的 getAllHttpServer() 就栽在这）：
    // type=http2 的服务器同样是 HttpServer。集成测试要覆盖 H1 与 H2，这里遍历全部 server 再按类型转换。
    std::vector<chen::TcpServer::ptr> all_servers;
    if (auto app = chen::Application::GetInstance()) {
        app->getAllServers(all_servers);
    }

    int registered = 0;
    for (auto& server : all_servers) {
        auto http_server = std::dynamic_pointer_cast<chen::http::HttpServer>(server);
        if (!http_server) {
            continue;
        }
        auto dispatch = http_server->getServletDispatch();
        if (!dispatch) {
            continue;
        }
        dispatch->addServlet("/drain/slow", std::make_shared<SlowServlet>());
        dispatch->addServlet("/drain/fast", std::make_shared<FastServlet>());
        ++registered;
        INFO(logger) << "DrainModule registered /drain/{slow,fast} on server=" << http_server->getName()
                     << " type=" << http_server->getType();
    }

    if (registered == 0) {
        WARN(logger) << "DrainModule: no HttpServer found, /drain/* not registered";
    }
    return true;
}

std::string DrainModule::statusString()
{
    return "drain_test";
}

} // namespace module_test

extern "C" {

chen::Module* CreateModule()
{
    return new module_test::DrainModule();
}

void DestroyModule(chen::Module* m)
{
    delete m;
}
}
