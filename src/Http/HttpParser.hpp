/**
 * @file HttpParser.hpp
 * @brief HTTP 解析器接口
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>

#include "Http.hpp"
#include "http11_parser.h"
#include "httpclient_parser.h"

namespace chen::http {

class HttpRequestParser {
public:
    typedef std::shared_ptr<HttpRequestParser> ptr;
    HttpRequestParser();
    // 1: 成功
    //-1: 有错误
    //>0: 已解析多少字节，有效数据量为len - v
    size_t execute(char* data, size_t len);
    int isFinished();
    int hasError();

    HttpRequest::ptr getData() const { return data_; }
    void setError(int v) { error_ = v; }

    uint64_t getContentLength();
    const http_parser& getParser() const { return parser_; }

public:
    static uint64_t GetHttpRequestBufferSize();
    static uint64_t GetHttpRequestMaxBodySize();

private:
    http_parser parser_;
    HttpRequest::ptr data_;
    // 1000: invalid method
    // 1001: invalid version
    // 1002: invalid field
    int error_;
};

class HttpResponseParser {
public:
    typedef std::shared_ptr<HttpResponseParser> ptr;
    HttpResponseParser();

    size_t execute(char* data, size_t len, bool chunck);
    int isFinished();
    int hasError();

    HttpResponse::ptr getData() const { return data_; }
    void setError(int v) { error_ = v; }

    uint64_t getContentLength();
    const httpclient_parser& getParser() const { return parser_; }

public:
    static uint64_t GetHttpResponseBufferSize();
    static uint64_t GetHttpResponseMaxBodySize();

private:
    httpclient_parser parser_;
    HttpResponse::ptr data_;
    int error_;
};

} // namespace chen::http
