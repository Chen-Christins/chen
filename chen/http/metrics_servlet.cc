#include "metrics_servlet.h"

#include "../metrics/metrics.h"
#include "http_server.h"
#include "servlet.h"

namespace chen::http {

bool RegisterMetricsServlet(const std::shared_ptr<HttpServer>& server, const std::string& path, metrics::RenderFormat format)
{
    if (!server) {
        return false;
    }
    auto dispatch = server->getServletDispatch();
    if (!dispatch) {
        return false;
    }

    const char* content_type = "text/plain; version=0.0.4; charset=utf-8";
    switch (format) {
    case metrics::RenderFormat::OPENMETRICS:
        content_type = "application/openmetrics-text; version=1.0.0; charset=utf-8";
        break;
    case metrics::RenderFormat::JSON:
        content_type = "application/json; charset=utf-8";
        break;
    case metrics::RenderFormat::PROMETHEUS:
    default:
        break;
    }

    dispatch->addServlet(path, [format, content_type](HttpRequest::ptr, HttpResponse::ptr response, HttpSession::ptr) -> int {
        response->setHeader("Content-Type", content_type);
        response->setBody(metrics::RegistryMgr::GetInstance()->Render(format));
        return 0;
    });

    return true;
}

} // namespace chen::http
