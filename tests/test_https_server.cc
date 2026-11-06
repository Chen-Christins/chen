/**
 * @file test_https_server.cc
 * @brief HTTPS 服务端集成测试：新配置、旧配置兼容、慢握手不阻塞 accept
 * @author Christins
 * @date 2026-11-06
 */
#include "chen/config/config.h"
#include "chen/http/http_connection.h"
#include "chen/http/http_server.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/metrics/metrics.h"
#include "chen/tcp/tcp_server.h"
#include "chen/util/mutex.h"

#include <cassert>
#include <csignal>
#include <iostream>
#include <string>
#include <vector>

using namespace chen;

#ifndef CHEN_TEST_CERT_DIR
#define CHEN_TEST_CERT_DIR "tests/certs"
#endif

static std::string CertPath(const std::string& name)
{
    return std::string(CHEN_TEST_CERT_DIR) + "/" + name;
}

static uint16_t g_port = 18643;

static http::HttpServer::ptr start_https_server(const std::string& cert, const std::string& key, bool legacy_conf)
{
    auto server = std::make_shared<http::HttpServer>(true);
    server->setName("https_test");
    server->getServletDispatch()->addServlet("/hello", [](http::HttpRequest::ptr req, http::HttpResponse::ptr rsp, http::HttpSession::ptr session) {
        (void)req;
        (void)session;
        rsp->setBody("hello https");
        return 0;
    });

    auto addr = Address::LookupAnyIPAddress("127.0.0.1:" + std::to_string(g_port));
    assert(addr);
    ++g_port;

    if (legacy_conf) {
        // 兼容旧配置：先 bind，再 loadCertificates
        assert(server->bind(addr, true));
        assert(server->loadCertificates(cert, key));
    } else {
        TcpServerConf conf;
        conf.type = "http";
        conf.ssl = 1;
        conf.cert_file = cert;
        conf.key_file = key;
        conf.handshake_timeout_ms = 2000;
        assert(server->setTlsConfig(MakeTlsConfig(conf)));
        assert(server->bind(addr, true));
        server->setConf(conf);
    }

    server->start();
    return server;
}

static void assert_https_get(uint16_t port, const std::string& expected)
{
    std::string url = "https://127.0.0.1:" + std::to_string(port) + "/hello";
    auto result = http::HttpConnection::DoGet(url, 3000);
    assert(result);
    assert(result->result == 0);
    assert(result->response);
    assert(result->response->getStatus() == http::HttpStatus::OK);
    assert(result->response->getBody() == expected);
}

static void test_https_new_conf()
{
    std::cout << "=== test_https_new_conf ===" << std::endl;
    uint16_t port = g_port;
    auto server = start_https_server(CertPath("server.crt"), CertPath("server.key"), false);
    assert(port == g_port - 1);
    assert_https_get(port, "hello https");
    server->stop();
    std::cout << "PASS" << std::endl;
}

static void test_https_legacy_conf()
{
    std::cout << "=== test_https_legacy_conf ===" << std::endl;
    uint16_t port = g_port;
    auto server = start_https_server(CertPath("server.crt"), CertPath("server.key"), true);
    assert(port == g_port - 1);
    assert_https_get(port, "hello https");
    server->stop();
    std::cout << "PASS" << std::endl;
}

static void test_slow_handshake_not_block_accept()
{
    std::cout << "=== test_slow_handshake_not_block_accept ===" << std::endl;

    uint16_t port = g_port;
    auto server = start_https_server(CertPath("server.crt"), CertPath("server.key"), false);
    assert(port == g_port - 1);

    auto addr = Address::LookupAnyIPAddress("127.0.0.1:" + std::to_string(port));
    assert(addr);

    // 建立只完成 TCP、不做 TLS 握手的连接，占住 io worker 的握手协程
    std::vector<Socket::ptr> slow_clients;
    for (int i = 0; i < 20; ++i) {
        auto sock = Socket::CreateTCP(addr);
        assert(sock->connect(addr, 1000));
        slow_clients.push_back(sock);
    }

    // accept worker 不应被慢握手阻塞，正常 HTTPS 请求仍能完成
    assert_https_get(port, "hello https");

    for (auto& sock : slow_clients) {
        sock->close();
    }
    server->stop();
    std::cout << "PASS" << std::endl;
}

static void run_tests()
{
    test_https_new_conf();
    test_https_legacy_conf();
    test_slow_handshake_not_block_accept();

    std::string metrics_text = metrics::RegistryMgr::GetInstance()->RenderPrometheus();
    assert(metrics_text.find("chen_tls_handshakes_total") != std::string::npos);
    assert(metrics_text.find("chen_tls_handshake_duration_seconds") != std::string::npos);
    assert(metrics_text.find("chen_tls_alpn_total") != std::string::npos);
}

int main()
{
    signal(SIGPIPE, SIG_IGN);

    // 测试证书是私有 CA，Step 5 前先用全局开关关闭客户端校验
    auto verify = Config::Lookup<bool>("http.client.ssl_verify", true, "https client verify peer");
    verify->setValue(false);

    IOManager iom(4, false, "test_https");
    Semaphore done(0);
    iom.schedule([&done]() {
        run_tests();
        done.notify();
    });
    iom.start();
    done.wait();
    iom.stop();

    std::cout << "ALL PASS" << std::endl;
    return 0;
}
