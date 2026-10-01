#include "chen/http/http_server.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

void run() {
    chen::http::HttpServer::ptr server(new chen::http::HttpServer);
    chen::Address::ptr addr = chen::Address::LookupAnyIPAddress("0.0.0.0:8020");
    while (!server->bind(addr)) {
        sleep(2);
    }
    auto sd = server->getServletDispatch();
    sd->addServlet("/chen/xx", [](chen::http::HttpRequest::ptr req
                ,chen::http::HttpResponse::ptr rsp
                ,chen::http::HttpSession::ptr session) {
        rsp->setBody(req->toString());
        return 0;
    });
    sd->addGlobServlet("/chen/*", [](chen::http::HttpRequest::ptr req 
                        ,chen::http::HttpResponse::ptr rsp
                        ,chen::http::HttpSession::ptr session) {
        rsp->setBody("Glob:\r\n" + req->toString());
        return 0;
    });
    server->start();
}

int main(int argc, char** argv) {
    logger->setLevel(chen::LogLevel::ERROR);
    chen::IOManager iom(7);
    iom.schedule(run);
    return 0;
}