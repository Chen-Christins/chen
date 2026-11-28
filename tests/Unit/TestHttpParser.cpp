/**
 * @file TestHttpParser.cpp
 * @brief HTTP/1.1 报文解析器单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @details 解析器只负责报文头，body 由调用方按 Content-Length 从剩余缓冲区读取
 *          （见 HttpConnection），因此这里同时断言 execute() 的返回偏移。
 */
#include "../TestHarness.hpp"

#include "src/Http/HttpParser.hpp"

#include <string>

using chen::http::HttpMethod;
using chen::http::HttpRequestParser;
using chen::http::HttpResponseParser;
using chen::http::HttpStatus;

CHEN_TEST(http_parser, request_headers)
{
    std::string raw = "GET /index.html?a=1&b=2 HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "User-Agent: chen-unit\r\n"
                      "\r\n";

    HttpRequestParser parser;
    const size_t consumed = parser.execute(raw.data(), raw.size());

    CHEN_CHECK_EQ(parser.hasError(), 0);
    CHEN_CHECK(parser.isFinished() != 0);
    CHEN_CHECK_EQ(consumed, raw.size());

    auto req = parser.getData();
    CHEN_CHECK(req->getMethod() == HttpMethod::GET);
    CHEN_CHECK_EQ(req->getPath(), std::string("/index.html"));
    CHEN_CHECK_EQ(req->getQuery(), std::string("a=1&b=2"));
    CHEN_CHECK_EQ(req->getHeader("Host"), std::string("example.com"));
    CHEN_CHECK_EQ(req->getHeader("User-Agent"), std::string("chen-unit"));
    CHEN_CHECK_EQ(static_cast<int>(req->getVersion()), 0x11);
}

CHEN_TEST(http_parser, request_body_leftover)
{
    std::string raw = "POST /submit HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Content-Length: 5\r\n"
                      "\r\n"
                      "hello";

    HttpRequestParser parser;
    const size_t consumed = parser.execute(raw.data(), raw.size());

    CHEN_CHECK_EQ(parser.hasError(), 0);
    CHEN_CHECK_EQ(parser.getContentLength(), 5u);
    // 报文头之后的数据原样留在缓冲区头部，由调用方按 Content-Length 取
    CHEN_CHECK_EQ(std::string(raw.data(), raw.size() - consumed), std::string("hello"));
}

CHEN_TEST(http_parser, request_partial_is_not_finished)
{
    std::string raw = "GET /partial HTT";

    HttpRequestParser parser;
    parser.execute(raw.data(), raw.size());

    CHEN_CHECK_EQ(parser.hasError(), 0);
    CHEN_CHECK_EQ(parser.isFinished(), 0);
}

CHEN_TEST(http_parser, request_invalid_method_sets_error)
{
    std::string raw = "INVALID / HTTP/1.1\r\nHost: example.com\r\n\r\n";

    HttpRequestParser parser;
    parser.execute(raw.data(), raw.size());

    CHEN_CHECK(parser.hasError() != 0);
}

CHEN_TEST(http_parser, response_headers)
{
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/plain\r\n"
                      "Content-Length: 5\r\n"
                      "\r\n"
                      "hello";

    HttpResponseParser parser;
    const size_t consumed = parser.execute(raw.data(), raw.size(), false);

    CHEN_CHECK_EQ(parser.hasError(), 0);
    CHEN_CHECK(parser.isFinished() != 0);

    auto rsp = parser.getData();
    CHEN_CHECK(rsp->getStatus() == HttpStatus::OK);
    CHEN_CHECK_EQ(rsp->getReason(), std::string("OK"));
    CHEN_CHECK_EQ(rsp->getHeader("Content-Type"), std::string("text/plain"));
    CHEN_CHECK_EQ(parser.getContentLength(), 5u);
    CHEN_CHECK_EQ(std::string(raw.data(), raw.size() - consumed), std::string("hello"));
}

CHEN_TEST(http_parser, response_status_404)
{
    std::string raw = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";

    HttpResponseParser parser;
    parser.execute(raw.data(), raw.size(), false);

    CHEN_CHECK_EQ(parser.hasError(), 0);
    auto rsp = parser.getData();
    CHEN_CHECK(static_cast<int>(rsp->getStatus()) == 404);
    CHEN_CHECK_EQ(rsp->getReason(), std::string("Not Found"));
}

CHEN_TEST_MAIN()
