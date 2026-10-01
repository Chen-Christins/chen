#include "chen/http/http_parser.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

const char test_request_data[] = "POST / HTTP/1.1\r\n"
                                "Host: www.chen.top\r\n"
                                "Content-Length: 10\r\n\r\n"
                                "1234567890\r\n";

void test_request() {
    chen::http::HttpRequestParser parser;
    std::string tmp = test_request_data;
    size_t s = parser.execute(&tmp[0], tmp.size());
    INFO(logger) << "execute rt=" << s
        << " has_error=" << parser.hasError()
        << " is_finished=" << parser.isFinished()
        << " total=" << tmp.size()
        << " content length=" << parser.getContentLength();
    tmp.resize(tmp.size() - s);

    INFO(logger) << parser.getData()->toString();
}
const char test_response_data[] = "HTTP/1.0 200 OK\r\n"
                                "Accept-Ranges: bytes\r\n"
                                "Cache-Control: no-cache\r\n"
                                "Content-Length: 81\r\n"
                                "Content-Type: text/html\r\n"
                                "Date: Wed, 30 Oct 2024 00:28:38 GMT\r\n\r\n"
                                "<html>\r\n"
                                "<meta http-equiv=\"refresh\"content=\"0;url=http://www.baidu.com/\">\r\n"
                                "</html>\r\n";

void test_response() {
    chen::http::HttpResponseParser parser;
    std::string tmp = test_response_data;
    size_t s = parser.execute(&tmp[0], tmp.size(), false);
    ERROR(logger) << "execute rt=" << s
        << " has_error=" << parser.hasError()
        << " is_finished=" << parser.isFinished()
        << " total=" << tmp.size()
        << " content-length=" << parser.getContentLength();
    tmp.resize(tmp.size() - s);
    INFO(logger) << parser.getData()->toString();
    INFO(logger) << tmp;
}

int main(int argc, char** argv) {
    test_request();
    INFO(logger) << "=============================";
    test_response();
    return 0;
}