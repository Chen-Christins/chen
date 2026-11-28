#include "DiagnosticsServlet.hpp"

#include "../Util/StallDetector.hpp"
#include "HttpServer.hpp"
#include "Servlet.hpp"

namespace chen::http {

bool RegisterDiagnosticsServlets(const std::shared_ptr<HttpServer>& server, const std::string& prefix)
{
    if (!server) {
        return false;
    }
    auto dispatch = server->getServletDispatch();
    if (!dispatch) {
        return false;
    }

    dispatch->addServlet(prefix + "/fibers",
        [](HttpRequest::ptr, HttpResponse::ptr response, HttpSession::ptr) -> int {
            StallDetector* detector = StallDetectorMgr::GetInstance();
            std::string body = "{\"thread_count\":";
            body += std::to_string(detector->threadCount());
            body += ",\"threads\":";
            body += detector->dumpJson();
            body += "}";

            response->setHeader("Content-Type", "application/json; charset=utf-8");
            response->setHeader("Cache-Control", "no-store");
            response->setBody(body);
            return 0;
        });

    return true;
}

} // namespace chen::http
