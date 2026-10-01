#include "chen/http/http.h"
#include "chen/log/log.h"
#include <iostream>

chen::Logger::ptr logger = LOG_ROOT();

void test_reqest() {
    chen::http::HttpRequest::ptr req(new chen::http::HttpRequest);
    req->setHeader("host", "www.baidu.com");
    req->setBody("hello baidu");

    req->dump(std::cout) << std::endl;
}

void test_response() {
    chen::http::HttpResponse::ptr rsp(new chen::http::HttpResponse);
    rsp->setHeader("X-X", "chen");
    rsp->setBody("hello chen");
    rsp->setStatus((chen::http::HttpStatus)404);
    rsp->setClose(false);

    rsp->dump(std::cout) << std::endl;
}

int main(int argc, char** argv) {
    test_reqest();
    test_response();
    return 0;
}
