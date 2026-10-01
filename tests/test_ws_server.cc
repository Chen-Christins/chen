#include "chen/http/ws_server.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

void run() {
    chen::http::WSServer::ptr server(new chen::http::WSServer);
    chen::Address::ptr addr = chen::Address::LookupAnyIPAddress("0.0.0.0:8020");
    if (!addr) {
        ERROR(logger) << "get address error";
        return ;
    }
    auto fun = [](chen::http::HttpRequest::ptr header
                 ,chen::http::WSFrameMessage::ptr msg
                 ,chen::http::WSSession::ptr session) {
        session->sendMessage(msg);
        return 0;
    };
    server->getWSServletDispatch()->addServlet("/chen", fun);
    while (!server->bind(addr)) {
        ERROR(logger) << "bind " << *addr << " fail";
        sleep(1);
    }
    server->start();
}

int main(int argc, char** argv) {
    chen::IOManager iom(2);
    iom.schedule(run);
    return 0;
}