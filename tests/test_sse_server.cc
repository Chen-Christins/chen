#include "chen/http/http_server.h"
#include "chen/http/sse_servlet.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

void run() {
    chen::http::HttpServer::ptr server(new chen::http::HttpServer);
    chen::Address::ptr addr = chen::Address::LookupAnyIPAddress("0.0.0.0:8089");
    if (!addr) {
        ERROR(logger) << "get address error";
        return;
    }

    auto connect_cb = [](chen::http::HttpRequest::ptr request, chen::http::SSESession::ptr session) -> int32_t {
        INFO(logger) << "SSE client connected, path=" << request->getPath();
        for (int i = 0; i < 10; ++i) {
            std::string data = "{\"time\": " + std::to_string(time(nullptr))
                             + ", \"seq\": " + std::to_string(i) + "}";
            if (session->sendEvent(data, std::to_string(i), "message") <= 0) {
                INFO(logger) << "client disconnected, stop sending";
                break;
            }
            sleep(1);
        }
        return 0;
    };

    auto close_cb = [](chen::http::HttpRequest::ptr request, chen::http::SSESession::ptr session) -> int32_t {
        INFO(logger) << "SSE client disconnected";
        return 0;
    };

    server->getServletDispatch()->addServlet("/chen/sse",
        std::make_shared<chen::http::FunctionSSEServlet>(connect_cb, close_cb));

    while (!server->bind(addr)) {
        ERROR(logger) << "bind " << *addr << " fail";
        sleep(1);
    }
    INFO(logger) << "SSE server started on " << *addr;
    server->start();
}

int main(int argc, char** argv) {
    chen::IOManager::ptr iom(new chen::IOManager(4, true, "sse_test"));
    iom->schedule(run);
    iom->addTimer(3000, []() {}, true);
    iom->stop();
    return 0;
}
