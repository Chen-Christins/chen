/**
 * @file test_h2_server.cc
 * @brief HTTP/2 server and client round-trip test
 */
#include <iostream>
#include "chen/http/http_server.h"
#include "chen/http/h2_connection.h"
#include "chen/http/h2_session.h"
#include "chen/log/log.h"
#include "chen/iomanager/iomanager.h"

static chen::Logger::ptr logger = LOG_ROOT();

static const int TEST_PORT = 8128;
static const std::string TEST_PATH = "/h2test";

void run_server() {
    auto server = std::make_shared<chen::http::HttpServer>(false);
    server->setType("http2");

    chen::Address::ptr addr = chen::Address::LookupAnyIPAddress("127.0.0.1:8128");
    while (!server->bind(addr)) {
        sleep(1);
    }

    auto sd = server->getServletDispatch();
    sd->addServlet(TEST_PATH, [](chen::http::HttpRequest::ptr req,
                                  chen::http::HttpResponse::ptr rsp,
                                  chen::http::HttpSession::ptr session) {
        rsp->setStatus(chen::http::HttpStatus::OK);
        rsp->setHeader("content-type", "text/plain");
        rsp->setBody("Hello HTTP/2: " + req->getPath());
        return 0;
    });

    server->start();
    std::cout << "[SERVER] HTTP/2 test server listening on port " << TEST_PORT << std::endl;
}

void run_client() {
    // Wait for server to start
    sleep(1);

    std::cout << "[CLIENT] Connecting to HTTP/2 server..." << std::endl;
    auto result = chen::http::H2Connection::DoGet(
        "http://127.0.0.1:" + std::to_string(TEST_PORT) + TEST_PATH,
        5000);

    if (result->result != 0) {
        std::cerr << "[CLIENT] FAIL: " << result->error << std::endl;
        return;
    }

    auto& rsp = result->response;
    std::cout << "[CLIENT] Response status: " << (int)rsp->getStatus() << std::endl;
    std::cout << "[CLIENT] Response body: " << rsp->getBody() << std::endl;
    std::cout << "[CLIENT] Response version: " << (rsp->isHttp2() ? "HTTP/2" : "HTTP/1.x") << std::endl;

    if (rsp->getStatus() == chen::http::HttpStatus::OK &&
        rsp->getBody() == "Hello HTTP/2: " + TEST_PATH &&
        rsp->isHttp2()) {
        std::cout << "[CLIENT] PASS: HTTP/2 round-trip successful!" << std::endl;
    } else {
        std::cerr << "[CLIENT] FAIL: Unexpected response" << std::endl;
    }
}

int main(int argc, char** argv) {
    logger->setLevel(chen::LogLevel::DEBUG);

    chen::IOManager iom(4, false, "test_h2");
    iom.schedule(run_server);
    iom.schedule(run_client);
    iom.start();

    // Wait for test to complete
    sleep(3);
    iom.stop();

    std::cout << "[MAIN] Test completed." << std::endl;
    return 0;
}
