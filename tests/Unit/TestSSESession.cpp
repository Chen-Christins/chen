/**
 * @file TestSSESession.cpp
 * @brief SSE 事件编码与响应头单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @details SSE 的所有写操作都通过 `HttpSession::sendResponseHeader()` / `sendData()`，
 *          这里用一个只记录调用的假会话替换真实 socket，验证：
 *          1. 响应头（Content-Type / Cache-Control / 无 Content-Length / close）；
 *          2. `data:` / `id:` / `event:` 的编码规则与多行拆分；
 *          3. `close()` 通过 end_stream 结束流。
 */
#include "../TestHarness.hpp"

#include "src/Http/HttpSession.hpp"
#include "src/Http/SSESession.hpp"

#include <string>

namespace {

using namespace chen::http;

/**
 * @brief 只记录发送内容的假 HttpSession（不碰 socket）
 */
class CapturingSession : public HttpSession {
public:
    CapturingSession()
        : HttpSession(nullptr, false)
    {
    }

    int sendResponseHeader(HttpResponse::ptr rsp) override
    {
        header_rsp_ = rsp;
        return 0;
    }

    int sendData(const void* buffer, size_t length, bool end_stream) override
    {
        if (buffer && length > 0) {
            body_.append(static_cast<const char*>(buffer), length);
        }
        end_stream_ = end_stream;
        ++data_calls_;
        return static_cast<int>(length);
    }

    HttpResponse::ptr header_rsp() const { return header_rsp_; }
    std::string& body() { return body_; }
    const std::string& body() const { return body_; }
    bool end_stream() const { return end_stream_; }
    int data_calls() const { return data_calls_; }

private:
    HttpResponse::ptr header_rsp_;
    std::string body_;
    bool end_stream_ = false;
    int data_calls_ = 0;
};

} // namespace

CHEN_TEST(sse, headers)
{
    auto cap = std::make_shared<CapturingSession>();
    SSESession sse(cap);

    CHEN_CHECK(sse.sendSSEHeaders());

    auto rsp = cap->header_rsp();
    CHEN_CHECK(rsp != nullptr);
    CHEN_CHECK_EQ(static_cast<int>(rsp->getStatus()), 200);
    CHEN_CHECK_EQ(rsp->getHeader("Content-Type"), std::string("text/event-stream"));
    CHEN_CHECK_EQ(rsp->getHeader("Cache-Control"), std::string("no-cache"));
    CHEN_CHECK_EQ(rsp->getHeader("Access-Control-Allow-Origin"), std::string("*"));
    // SSE 没有 Content-Length，H1 只能靠关闭连接结束响应
    CHEN_CHECK(rsp->getHeader("Content-Length").empty());
    CHEN_CHECK(rsp->isClose());
}

CHEN_TEST(sse, event_with_id_and_type)
{
    auto cap = std::make_shared<CapturingSession>();
    SSESession sse(cap);
    CHEN_CHECK(sse.sendSSEHeaders());

    CHEN_CHECK(sse.sendEvent("sse-1", "1", "msg") > 0);
    CHEN_CHECK_EQ(cap->body(), std::string("id: 1\nevent: msg\ndata: sse-1\n\n"));
}

CHEN_TEST(sse, event_multiline_and_empty_data)
{
    auto cap = std::make_shared<CapturingSession>();
    SSESession sse(cap);
    CHEN_CHECK(sse.sendSSEHeaders());

    CHEN_CHECK(sse.sendEvent("line-1\nline-2") > 0);
    // 多行 data 每行都要带 "data: " 前缀
    CHEN_CHECK_EQ(cap->body(), std::string("data: line-1\ndata: line-2\n\n"));

    cap->body().clear();
    CHEN_CHECK(sse.sendEvent("") > 0);
    // 空数据也要输出一个 data 字段，否则客户端收不到事件
    CHEN_CHECK_EQ(cap->body(), std::string("data: \n\n"));
}

CHEN_TEST(sse, comment_and_retry)
{
    auto cap = std::make_shared<CapturingSession>();
    SSESession sse(cap);
    CHEN_CHECK(sse.sendSSEHeaders());

    CHEN_CHECK(sse.sendComment("ping") > 0);
    CHEN_CHECK_EQ(cap->body(), std::string(": ping\n\n"));

    cap->body().clear();
    CHEN_CHECK(sse.sendRetry(3000) > 0);
    CHEN_CHECK_EQ(cap->body(), std::string("retry: 3000\n\n"));
}

CHEN_TEST(sse, close_sends_end_stream)
{
    auto cap = std::make_shared<CapturingSession>();
    SSESession sse(cap);
    CHEN_CHECK(sse.sendSSEHeaders());

    CHEN_CHECK(sse.closeStream() >= 0);
    CHEN_CHECK(cap->end_stream());
    CHEN_CHECK(cap->body().empty());
    // close 本身是一次 0 字节的 sendData
    CHEN_CHECK_EQ(cap->data_calls(), 1);
}

CHEN_TEST_MAIN()
