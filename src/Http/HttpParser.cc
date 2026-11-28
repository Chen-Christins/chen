#include "HttpParser.hpp"

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<uint64_t>::ptr g_http_request_buffer_size =
    Config::Lookup("http.request.buffer_size", uint64_t(4 * 1024), "http request buffer size");

static ConfigVar<uint64_t>::ptr g_http_request_max_body_size =
    Config::Lookup("http.request.max_body_size", uint64_t(64 * 1024 * 1024), "http request max body size");

static ConfigVar<uint64_t>::ptr g_http_response_buffer_size =
    Config::Lookup("http.response.buffer_size", uint64_t(4 * 1024), "http response buffer size");

static ConfigVar<uint64_t>::ptr g_http_response_max_body_size =
    Config::Lookup("http.response.max_body_size", uint64_t(64 * 1024 * 1024), "http response max body size");

static uint64_t s_http_request_buffer_size = 0;
static uint64_t s_http_request_max_body_size = 0;
static uint64_t s_http_response_buffer_size = 0;
static uint64_t s_http_response_max_body_size = 0;

namespace {

struct _RequestSizeIniter {
    _RequestSizeIniter()
    {
        s_http_request_buffer_size = g_http_request_buffer_size->getValue();
        s_http_request_max_body_size = g_http_request_max_body_size->getValue();
        s_http_response_buffer_size = g_http_response_buffer_size->getValue();
        s_http_response_max_body_size = g_http_response_max_body_size->getValue();

        g_http_request_buffer_size->addListener([](const uint64_t& ov, const uint64_t& nv) {
            s_http_request_buffer_size = nv;
        });

        g_http_request_max_body_size->addListener([](const uint64_t& ov, const uint64_t& nv) {
            s_http_request_max_body_size = nv;
        });

        g_http_response_buffer_size->addListener([](const uint64_t& ov, const uint64_t& nv) {
            s_http_response_buffer_size = nv;
        });

        g_http_response_max_body_size->addListener([](const uint64_t& ov, const uint64_t& nv) {
            s_http_response_max_body_size = nv;
        });
    }
};

static _RequestSizeIniter _init;

} // namespace

void on_request_method(void* data, const char* at, size_t length)
{
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    HttpMethod m = CharsToHttpMethod(at);

    if (m == HttpMethod::INVALID_METHOD) {
        // network attack
        WARN(logger) << "invalid http request method " << std::string(at, length);
        parser->setError(1000);
        return;
    }
    parser->getData()->setMethod(m);
}

void on_request_uri(void* data, const char* at, size_t length)
{
}

void on_request_fragment(void* data, const char* at, size_t length)
{
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    parser->getData()->setFragment(std::string(at, length));
}

void on_request_path(void* data, const char* at, size_t length)
{
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    parser->getData()->setPath(std::string(at, length));
}

void on_request_query(void* data, const char* at, size_t length)
{
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    parser->getData()->setQuery(std::string(at, length));
}

void on_request_version(void* data, const char* at, size_t length)
{
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    uint8_t v = 0;
    if (strncmp(at, "HTTP/1.1", length) == 0) {
        v = 0x11;
    } else if (strncmp(at, "HTTP/1.0", length) == 0) {
        v = 0x10;
    } else {
        WARN(logger) << "invalid http request version: " << std::string(at, length);
        parser->setError(1001);
        return;
    }
    parser->getData()->setVersion(v);
}

void on_request_header_done(void* data, const char* at, size_t length)
{
}

void on_request_http_field(void* data, const char* field, size_t flen, const char* value, size_t vlen)
{
    HttpRequestParser* parser = static_cast<HttpRequestParser*>(data);
    if (flen == 0) {
        WARN(logger) << "invalid http request field length == 0";
        return;
    }
    parser->getData()->setHeader(std::string(field, flen), std::string(value, vlen));
}

HttpRequestParser::HttpRequestParser()
    : error_(0)
{
    data_.reset(new HttpRequest);
    http_parser_init(&parser_);
    parser_.request_method = on_request_method;
    parser_.request_uri = on_request_uri;
    parser_.fragment = on_request_fragment;
    parser_.request_path = on_request_path;
    parser_.query_string = on_request_query;
    parser_.http_version = on_request_version;
    parser_.header_done = on_request_header_done;
    parser_.http_field = on_request_http_field;
    parser_.data = this;
}

size_t HttpRequestParser::execute(char* data, size_t len)
{
    size_t offset = http_parser_execute(&parser_, data, len, 0);
    // move (data + offset, data + len - offset) to data
    // 把解析过的数据移走
    memmove(data, data + offset, (len - offset));
    return offset;
}

int HttpRequestParser::isFinished()
{
    return http_parser_is_finished(&parser_);
}

int HttpRequestParser::hasError()
{
    return error_ || http_parser_has_error(&parser_);
}

uint64_t HttpRequestParser::getContentLength()
{
    return data_->getHeaderAs<uint64_t>("content-length", 0);
}

uint64_t HttpRequestParser::GetHttpRequestBufferSize()
{
    return s_http_request_buffer_size;
}

uint64_t HttpRequestParser::GetHttpRequestMaxBodySize()
{
    return s_http_request_max_body_size;
}

void on_response_reason(void* data, const char* at, size_t length)
{
    HttpResponseParser* parser = static_cast<HttpResponseParser*>(data);
    parser->getData()->setReason(std::string(at, length));
}

void on_response_status(void* data, const char* at, size_t length)
{
    HttpResponseParser* parser = static_cast<HttpResponseParser*>(data);
    HttpStatus status = (HttpStatus)(atoi(at));
    parser->getData()->setStatus(status);
}

void on_response_chunck(void* data, const char* at, size_t length)
{
}

void on_response_version(void* data, const char* at, size_t length)
{
    HttpResponseParser* parser = static_cast<HttpResponseParser*>(data);
    uint8_t v = 0;
    if (strncmp(at, "HTTP/1.1", length) == 0) {
        v = 0x11;
    } else if (strncmp(at, "HTTP/1.0", length) == 0) {
        v = 0x10;
    } else {
        WARN(logger) << "invalid http response version: " << std::string(at, length);
        parser->setError(1001);
        return;
    }
    parser->getData()->setVersion(v);
}

void on_response_header_done(void* data, const char* at, size_t length)
{
}

void on_response_last_chunck(void* data, const char* at, size_t length)
{
}

void on_response_http_field(void* data, const char* field, size_t flen, const char* value, size_t vlen)
{
    HttpResponseParser* parser = static_cast<HttpResponseParser*>(data);
    if (flen == 0) {
        return;
    }
    parser->getData()->setHeader(std::string(field, flen), std::string(value, vlen));
}

HttpResponseParser::HttpResponseParser()
    : error_(0)
{
    data_.reset(new HttpResponse);
    httpclient_parser_init(&parser_);
    parser_.reason_phrase = on_response_reason;
    parser_.status_code = on_response_status;
    parser_.chunk_size = on_response_chunck;
    parser_.http_version = on_response_version;
    parser_.header_done = on_response_header_done;
    parser_.last_chunk = on_response_last_chunck;
    parser_.http_field = on_response_http_field;
    parser_.data = this;
}

size_t HttpResponseParser::execute(char* data, size_t len, bool chunck)
{
    if (chunck) {
        httpclient_parser_init(&parser_);
    }
    size_t offset = httpclient_parser_execute(&parser_, data, len, 0);
    memmove(data, data + offset, len - offset);
    return offset;
}

int HttpResponseParser::isFinished()
{
    return httpclient_parser_finish(&parser_);
}

int HttpResponseParser::hasError()
{
    return error_ || httpclient_parser_has_error(&parser_);
}

uint64_t HttpResponseParser::getContentLength()
{
    return data_->getHeaderAs<uint64_t>("content-length", 0);
}

uint64_t HttpResponseParser::GetHttpResponseBufferSize()
{
    return s_http_response_buffer_size;
}

uint64_t HttpResponseParser::GetHttpResponseMaxBodySize()
{
    return s_http_response_max_body_size;
}

} // namespace chen::http
