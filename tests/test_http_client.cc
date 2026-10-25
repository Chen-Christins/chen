/**
 * @file test_http_client.cc
 * @brief HttpClient 异步请求测试：Future/回调两种形态、并发、失败路径、无调度器降级
 * @author chen
 * @date 2026-10-25
 * @copyright GPL-3.0
 */
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#include "chen/http/http_client.h"
#include "chen/http/http_server.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/util/time_util.h"

static chen::Logger::ptr logger = LOG_ROOT();
static std::atomic<int> g_failed{0};
static chen::http::HttpServer::ptr g_server;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            ++g_failed;                                                            \
            ERROR(logger) << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond; \
        }                                                                          \
    } while (0)

static std::string base_url(uint16_t port)
{
    return "http://127.0.0.1:" + std::to_string(port);
}

/**
 * @brief 在 18087~18100 中找一个可用端口启动测试服务
 */
static chen::http::HttpServer::ptr start_server(uint16_t& port)
{
    for (uint16_t p = 18087; p < 18100; ++p) {
        chen::Address::ptr addr = chen::Address::LookupAnyIPAddress("127.0.0.1:" + std::to_string(p));
        if (!addr) {
            continue;
        }
        chen::http::HttpServer::ptr server(new chen::http::HttpServer);
        if (!server->bind(addr)) {
            WARN(logger) << "bind 127.0.0.1:" << p << " failed, try next port";
            continue;
        }

        chen::http::ServletDispatch::ptr dispatch = server->getServletDispatch();
        // 回显请求体
        dispatch->addServlet("/echo", [](chen::http::HttpRequest::ptr req, chen::http::HttpResponse::ptr rsp, chen::http::HttpSession::ptr) {
            rsp->setBody(req->getBody());
            return 0;
        });
        // 慢接口：sleep 被 hook，占住协程不占线程
        dispatch->addServlet("/slow", [](chen::http::HttpRequest::ptr, chen::http::HttpResponse::ptr rsp, chen::http::HttpSession::ptr) {
            usleep(200 * 1000);
            rsp->setBody("slow");
            return 0;
        });

        server->start();
        port = p;
        return server;
    }
    return nullptr;
}

// ========== 无调度器降级 ==========

void test_no_scheduler()
{
    INFO(logger) << "=== test_no_scheduler ===";
    const bool submitted = chen::http::HttpClient::submit(chen::http::HttpMethod::GET, "http://127.0.0.1:1/echo", 1000);
    CHECK(!submitted);

    chen::Future<chen::http::HttpResult::ptr> future = chen::http::HttpClient::get("http://127.0.0.1:1/echo", 1000);
    CHECK(future.valid());
    CHECK(future.ready());
    CHECK(future.get() == nullptr);
}

// ========== fiber 内异步请求 ==========

void test_post_echo(uint16_t port)
{
    INFO(logger) << "=== test_post_echo ===";
    chen::Future<chen::http::HttpResult::ptr> future =
        chen::http::HttpClient::post(base_url(port) + "/echo", 3000, {{"Content-Type", "application/json"}}, "{\"a\":1}");
    CHECK(future.valid());

    chen::http::HttpResult::ptr result = future.get();
    CHECK(result != nullptr);
    if (!result) {
        return;
    }
    CHECK(result->result == 0);
    CHECK(result->response != nullptr);
    if (result->response) {
        CHECK(result->response->getBody() == "{\"a\":1}");
    }
}

void test_callback(uint16_t port)
{
    INFO(logger) << "=== test_callback ===";
    chen::Promise<bool> done;
    chen::Future<bool> done_future = done.getFuture();

    const bool submitted =
        chen::http::HttpClient::submit(chen::http::HttpMethod::GET, base_url(port) + "/echo", 3000, [&done](chen::http::HttpResult::ptr result) {
            done.setValue(result && result->result == 0);
        });
    CHECK(submitted);
    CHECK(done_future.get());
}

void test_connection_fail()
{
    INFO(logger) << "=== test_connection_fail ===";
    chen::http::HttpResult::ptr result = chen::http::HttpClient::get("http://127.0.0.1:1/echo", 1000).get();
    CHECK(result != nullptr);
    if (result) {
        CHECK(result->result != 0);
    }
}

void test_concurrent(uint16_t port)
{
    INFO(logger) << "=== test_concurrent ===";
    const int n = 8;
    const uint64_t start = chen::TimeUtil::GetCurrentMs();

    std::vector<chen::Future<chen::http::HttpResult::ptr>> futures;
    futures.reserve(n);
    for (int i = 0; i < n; ++i) {
        futures.push_back(chen::http::HttpClient::get(base_url(port) + "/slow", 10000));
    }

    int ok = 0;
    for (auto& f : futures) {
        chen::http::HttpResult::ptr result = f.get();
        if (result && result->result == 0) {
            ++ok;
        }
    }

    const uint64_t elapsed = chen::TimeUtil::GetCurrentMs() - start;
    INFO(logger) << "concurrent n=" << n << " ok=" << ok << " elapsed=" << elapsed << "ms";
    CHECK(ok == n);
    // 串行需要 n * 200ms，fiber 并发应远小于该值
    CHECK(elapsed < 1000);
}

void run_tests(uint16_t port)
{
    test_post_echo(port);
    test_callback(port);
    test_connection_fail();
    test_concurrent(port);
}

int main(int argc, char** argv)
{
    logger->setLevel(chen::LogLevel::TRACE);

    // 必须在创建调度器之前：当前线程没有调度器
    test_no_scheduler();

    chen::IOManager iom(4, true, "http_client_test");
    iom.schedule([]() {
        uint16_t port = 0;
        g_server = start_server(port);
        if (!g_server) {
            ++g_failed;
            ERROR(logger) << "start http server failed";
            return;
        }
        INFO(logger) << "http server listening on 127.0.0.1:" << port;

        run_tests(port);

        g_server->stop();
        g_server.reset();
    });
    iom.stop();

    if (g_failed.load() == 0) {
        std::cout << "test_http_client PASS" << std::endl;
        return 0;
    }
    std::cout << "test_http_client FAILED count=" << g_failed.load() << std::endl;
    return 1;
}
