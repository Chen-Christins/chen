/**
 * @file TestHttp.cpp
 * @brief HttpRequest / HttpResponse 序列化与解析单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "../TestHarness.hpp"

#include "src/Http/Http.hpp"

#include <string>

using chen::http::HttpMethod;
using chen::http::HttpRequest;
using chen::http::HttpResponse;
using chen::http::HttpStatus;

namespace {

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace

CHEN_TEST(http, request_dump_basic)
{
    HttpRequest req;
    req.setMethod(HttpMethod::GET);
    req.setPath("/index");
    req.setClose(false);

    const std::string text = req.toString();
    CHEN_CHECK(Contains(text, "GET /index HTTP/1.1\r\n"));
    CHEN_CHECK(Contains(text, "connection: keep-alive\r\n"));
    CHEN_CHECK(!Contains(text, "content-length"));
    // 无 body 时以空行结束
    CHEN_CHECK_EQ(text.substr(text.size() - 4), std::string("\r\n\r\n"));
}

CHEN_TEST(http, request_dump_body_and_query)
{
    HttpRequest req;
    req.setMethod(HttpMethod::POST);
    req.setPath("/submit");
    req.setQuery("a=1&b=2");
    req.setHeader("Host", "example.com");
    req.setBody("hello chen");
    req.setClose(true);

    const std::string text = req.toString();
    CHEN_CHECK(Contains(text, "POST /submit?a=1&b=2 HTTP/1.1\r\n"));
    CHEN_CHECK(Contains(text, "connection: close\r\n"));
    CHEN_CHECK(Contains(text, "Host: example.com\r\n"));
    CHEN_CHECK(Contains(text, "content-length: 10\r\n\r\nhello chen"));
}

CHEN_TEST(http, request_dump_skips_user_connection_header)
{
    HttpRequest req;
    req.setClose(true);
    req.setHeader("connection", "keep-alive");

    const std::string text = req.toString();
    // 用户设置的 connection 头不再重复输出，只由 close_ 决定
    const size_t first = text.find("connection:");
    CHEN_CHECK(first != std::string::npos);
    CHEN_CHECK_EQ(text.find("connection:", first + 1), std::string::npos);
    CHEN_CHECK(Contains(text, "connection: close\r\n"));
}

CHEN_TEST(http, request_params_and_cookies)
{
    HttpRequest req;
    req.setQuery("a=1&b=2");
    req.setHeader("content-type", "application/x-www-form-urlencoded");
    req.setBody("x=3&y=4");
    req.setHeader("Cookie", "sid=abc; theme=dark");

    std::string value;
    CHEN_CHECK(req.hasParam("a", &value));
    CHEN_CHECK_EQ(value, std::string("1"));
    CHEN_CHECK(req.hasParam("y", &value));
    CHEN_CHECK_EQ(value, std::string("4"));
    CHEN_CHECK(!req.hasParam("missing"));

    CHEN_CHECK(req.hasCookie("sid", &value));
    CHEN_CHECK_EQ(value, std::string("abc"));
    CHEN_CHECK(req.hasCookie("theme", &value));
    CHEN_CHECK_EQ(value, std::string("dark"));
    CHEN_CHECK(!req.hasCookie("none"));
}

CHEN_TEST(http, response_dump_basic)
{
    HttpResponse rsp;
    rsp.setStatus(HttpStatus::NOT_FOUND);
    rsp.setClose(false);
    rsp.setBody("not here");

    const std::string text = rsp.toString();
    CHEN_CHECK(Contains(text, "HTTP/1.1 404 Not Found\r\n"));
    CHEN_CHECK(Contains(text, "connection: keep-alive\r\n"));
    CHEN_CHECK(Contains(text, "content-length: 8\r\n\r\nnot here"));
}

CHEN_TEST(http, response_cookie_and_reason)
{
    HttpResponse rsp;
    rsp.setStatus(HttpStatus::OK);
    rsp.setReason("Fine");
    rsp.setCookie("k", "v", 0, "/");

    const std::string text = rsp.toString();
    CHEN_CHECK(Contains(text, "HTTP/1.1 200 Fine\r\n"));
    CHEN_CHECK(Contains(text, "Set-Cookie: k=v;path=/\r\n"));
}

CHEN_TEST(http, response_redirect)
{
    HttpResponse rsp;
    rsp.setRedirect("/login");
    CHEN_CHECK(rsp.getStatus() == HttpStatus::FOUND);
    CHEN_CHECK_EQ(rsp.getHeader("Location"), std::string("/login"));
    CHEN_CHECK(Contains(rsp.toString(), "HTTP/1.1 302 "));
}

CHEN_TEST(http, init_reads_connection_header)
{
    HttpRequest req;
    req.setHeader("connection", "close");
    req.init();
    CHEN_CHECK(req.isClose());

    HttpResponse rsp;
    rsp.setHeader("connection", "keep-alive");
    rsp.init();
    CHEN_CHECK(!rsp.isClose());
}

CHEN_TEST_MAIN()
