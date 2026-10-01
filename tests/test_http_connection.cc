#include <iostream>
#include "chen/http/http_connection.h"
#include "chen/http/sse_client.h"
#include "chen/log/log.h"
#include "chen/iomanager/iomanager.h"

static chen::Logger::ptr logger = LOG_ROOT();

void test_pool() {
    chen::http::HttpConnectionPool::ptr pool(new chen::http::HttpConnectionPool(
                "www.chen.top", "", 80, 10, 1000 * 30, 5));

    chen::IOManager::GetThis()->addTimer(1000, [pool](){
            auto r = pool->doGet("/", 300);
            INFO(logger) << r->toString();
    }, true);
}

void run() {
    chen::Address::ptr addr = chen::Address::LookupAny("www.chen.top:80");
    if (!addr) {
        INFO(logger) << "get addr error";
        return ;
    }

    chen::Socket::ptr sock = chen::Socket::CreateTCP(addr);
    bool rt = sock->connect(addr);
    if (!rt) {
        INFO(logger) << "connect " << *addr << " failed";
        return ;
    }

    chen::http::HttpConnection::ptr conn(new chen::http::HttpConnection(sock));
    chen::http::HttpRequest::ptr req(new chen::http::HttpRequest);
    req->setPath("/blog/");
    req->setHeader("host", "www.chen.top");
    INFO(logger) << "req: " << std::endl << *req;

    conn->sendRequest(req);
    auto rsp = conn->recvResponse();

    if (!rsp) {
        INFO(logger) << "recv response error";
        return ;
    }
    INFO(logger) << "rsp: " << std::endl << *rsp;

    std::ofstream ofs("ofs.dat");
    ofs << *rsp;

    INFO(logger) << "----------------------------------------------";

    auto r = chen::http::HttpConnection::DoGet("http://www.chen.top/blog/", 300);
    INFO(logger) << "result=" << r->result
        << " error=" << r->error
        << " rsp=" << (r->response ? r->response->toString() : "");

    INFO(logger) << "----------------------------------------------";

    test_pool();
}

// ========== SSE Client tests ==========

void test_sse_basic() {
    INFO(logger) << "=== test_sse_basic ===";
    chen::http::SSEClient sse;
    int count = 0;
    sse.setCallback([&count](const chen::http::SSEClient::SSEvent& ev) -> bool {
        count++;
        INFO(logger) << "SSE event #" << count << ": data=[" << ev.data << "] event=[" << ev.event << "] id=[" << ev.id << "]";
        return true;
    });

    // Single event
    const char* sse_data = "data: hello world\n\n";
    size_t consumed = sse.feed(sse_data, strlen(sse_data));
    INFO(logger) << "consumed=" << consumed << " count=" << count;
    if (count != 1) {
        ERROR(logger) << "test_sse_basic FAILED: expected 1 event, got " << count;
    } else {
        INFO(logger) << "test_sse_basic PASSED";
    }
}

void test_sse_multiple_events() {
    INFO(logger) << "=== test_sse_multiple_events ===";
    chen::http::SSEClient sse;
    int count = 0;
    sse.setCallback([&count](const chen::http::SSEClient::SSEvent& ev) -> bool {
        count++;
        INFO(logger) << "SSE event #" << count << ": data=[" << ev.data << "]";
        return true;
    });

    const char* sse_data = "data: first\n\ndata: second\n\ndata: third\n\n";
    size_t consumed = sse.feed(sse_data, strlen(sse_data));
    INFO(logger) << "consumed=" << consumed << " count=" << count;
    if (count != 3) {
        ERROR(logger) << "test_sse_multiple_events FAILED: expected 3 events, got " << count;
    } else {
        INFO(logger) << "test_sse_multiple_events PASSED";
    }
}

void test_sse_with_event_type_and_id() {
    INFO(logger) << "=== test_sse_with_event_type_and_id ===";
    chen::http::SSEClient sse;
    sse.setCallback([](const chen::http::SSEClient::SSEvent& ev) -> bool {
        INFO(logger) << "SSE event: data=[" << ev.data << "] event=[" << ev.event << "] id=[" << ev.id << "]";
        if (ev.event != "update") {
            ERROR(logger) << "FAILED: expected event='update', got '" << ev.event << "'";
            return true;
        }
        if (ev.id != "123") {
            ERROR(logger) << "FAILED: expected id='123', got '" << ev.id << "'";
            return true;
        }
        INFO(logger) << "test_sse_with_event_type_and_id PASSED";
        return true;
    });

    const char* sse_data = "id: 123\nevent: update\ndata: {\"key\":\"value\"}\n\n";
    sse.feed(sse_data, strlen(sse_data));
}

void test_sse_multi_line_data() {
    INFO(logger) << "=== test_sse_multi_line_data ===";
    chen::http::SSEClient sse;
    sse.setCallback([](const chen::http::SSEClient::SSEvent& ev) -> bool {
        INFO(logger) << "SSE event data=[" << ev.data << "]";
        std::string expected = "line1\nline2\nline3";
        if (ev.data != expected) {
            ERROR(logger) << "FAILED: expected '" << expected << "', got '" << ev.data << "'";
        } else {
            INFO(logger) << "test_sse_multi_line_data PASSED";
        }
        return true;
    });

    const char* sse_data = "data: line1\ndata: line2\ndata: line3\n\n";
    sse.feed(sse_data, strlen(sse_data));
}

void test_sse_comment() {
    INFO(logger) << "=== test_sse_comment ===";
    chen::http::SSEClient sse;
    int count = 0;
    sse.setCallback([&count](const chen::http::SSEClient::SSEvent& ev) -> bool {
        count++;
        return true;
    });

    // Comments should be ignored, only the data event should trigger callback
    const char* sse_data = ": this is a comment\n: another comment\ndata: real event\n\n";
    sse.feed(sse_data, strlen(sse_data));

    if (count != 1) {
        ERROR(logger) << "test_sse_comment FAILED: expected 1 event (comments ignored), got " << count;
    } else {
        INFO(logger) << "test_sse_comment PASSED";
    }
}

void test_sse_partial_data() {
    INFO(logger) << "=== test_sse_partial_data ===";
    chen::http::SSEClient sse;
    int count = 0;
    sse.setCallback([&count](const chen::http::SSEClient::SSEvent& ev) -> bool {
        count++;
        INFO(logger) << "SSE event #" << count << ": data=[" << ev.data << "]";
        return true;
    });

    // Feed incomplete data
    size_t c1 = sse.feed("data: partial", 13);
    INFO(logger) << "feed1 consumed=" << c1 << " count=" << count;
    if (count != 0) {
        ERROR(logger) << "FAILED: expected 0 events after partial feed, got " << count;
    }

    // Complete the event
    size_t c2 = sse.feed(" event here\n\n", 14);
    INFO(logger) << "feed2 consumed=" << c2 << " count=" << count;
    if (count != 1) {
        ERROR(logger) << "FAILED: expected 1 event after completing feed, got " << count;
    } else {
        INFO(logger) << "test_sse_partial_data PASSED";
    }
}

void test_sse_stop_callback() {
    INFO(logger) << "=== test_sse_stop_callback ===";
    chen::http::SSEClient sse;
    int count = 0;
    sse.setCallback([&count](const chen::http::SSEClient::SSEvent& ev) -> bool {
        count++;
        INFO(logger) << "SSE event #" << count << ": data=[" << ev.data << "]";
        // Stop after first event
        return false;
    });

    const char* sse_data = "data: first\n\ndata: second\n\ndata: third\n\n";
    size_t consumed = sse.feed(sse_data, strlen(sse_data));
    INFO(logger) << "consumed=" << consumed << " total_len=" << strlen(sse_data) << " count=" << count;
    if (count != 1) {
        ERROR(logger) << "test_sse_stop_callback FAILED: expected 1 event (stopped), got " << count;
    } else if (consumed >= strlen(sse_data)) {
        ERROR(logger) << "test_sse_stop_callback FAILED: should not consume all data after stop";
    } else {
        INFO(logger) << "test_sse_stop_callback PASSED";
    }
}

void test_sse_empty_event() {
    INFO(logger) << "=== test_sse_empty_event ===";
    chen::http::SSEClient sse;
    int count = 0;
    sse.setCallback([&count](const chen::http::SSEClient::SSEvent& ev) -> bool {
        count++;
        return true;
    });

    // Empty data between double newlines should not trigger callback
    const char* sse_data = "data: a\n\n\n\ndata: b\n\n";
    sse.feed(sse_data, strlen(sse_data));

    if (count != 2) {
        ERROR(logger) << "test_sse_empty_event FAILED: expected 2 events, got " << count;
    } else {
        INFO(logger) << "test_sse_empty_event PASSED";
    }
}

void test_sse_reset() {
    INFO(logger) << "=== test_sse_reset ===";
    chen::http::SSEClient sse;
    int count = 0;
    sse.setCallback([&count](const chen::http::SSEClient::SSEvent& ev) -> bool {
        count++;
        return true;
    });

    // Feed partial data
    const char* partial = "data: incomplete";
    sse.feed(partial, strlen(partial));
    if (count != 0) {
        ERROR(logger) << "FAILED: expected 0 events after partial feed";
    }

    // Reset
    sse.reset();

    // Feed new event
    const char* complete = "data: fresh start\n\n";
    sse.feed(complete, strlen(complete));
    if (count != 1) {
        ERROR(logger) << "test_sse_reset FAILED: expected 1 event after reset, got " << count;
    } else {
        INFO(logger) << "test_sse_reset PASSED";
    }
}

void test_sse_ai_stream_format() {
    INFO(logger) << "=== test_sse_ai_stream_format ===";
    chen::http::SSEClient sse;
    int count = 0;
    std::string all_data;
    sse.setCallback([&count, &all_data](const chen::http::SSEClient::SSEvent& ev) -> bool {
        count++;
        all_data += ev.data;
        INFO(logger) << "SSE event #" << count << ": data=[" << ev.data << "]";
        return true;
    });

    // Simulate OpenAI-style SSE stream
    const char* chunk1 = "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n";
    const char* chunk2 = "data: {\"choices\":[{\"delta\":{\"content\":\" World\"}}]}\n\n";
    const char* chunk3 = "data: [DONE]\n\n";

    sse.feed(chunk1, strlen(chunk1));
    sse.feed(chunk2, strlen(chunk2));
    sse.feed(chunk3, strlen(chunk3));

    if (count != 3) {
        ERROR(logger) << "test_sse_ai_stream_format FAILED: expected 3 events, got " << count;
    } else {
        INFO(logger) << "test_sse_ai_stream_format PASSED";
        INFO(logger) << "all_data=" << all_data;
    }
}

void test_sse_all() {
    test_sse_basic();
    test_sse_multiple_events();
    test_sse_with_event_type_and_id();
    test_sse_multi_line_data();
    test_sse_comment();
    test_sse_partial_data();
    test_sse_stop_callback();
    test_sse_empty_event();
    test_sse_reset();
    test_sse_ai_stream_format();
    INFO(logger) << "=== All SSE tests completed ===";
}

// ========== Streaming tests ==========

void test_streaming() {
    INFO(logger) << "=== test_streaming ===";
    chen::Address::ptr addr = chen::Address::LookupAny("www.chen.top:80");
    if (!addr) {
        INFO(logger) << "get addr error, skip streaming test";
        return;
    }

    chen::Socket::ptr sock = chen::Socket::CreateTCP(addr);
    bool rt = sock->connect(addr);
    if (!rt) {
        INFO(logger) << "connect failed, skip streaming test";
        return;
    }

    chen::http::HttpConnection::ptr conn(new chen::http::HttpConnection(sock));
    chen::http::HttpRequest::ptr req(new chen::http::HttpRequest);
    req->setPath("/blog/");
    req->setHeader("host", "www.chen.top");

    conn->sendRequest(req);

    int chunk_count = 0;
    size_t total_bytes = 0;
    auto rsp = conn->recvResponseStreaming([&chunk_count, &total_bytes](const char* data, size_t len) -> bool {
        chunk_count++;
        total_bytes += len;
        INFO(logger) << "chunk #" << chunk_count << " len=" << len;
        return true;
    });

    if (rsp) {
        INFO(logger) << "streaming response status=" << (int)rsp->getStatus()
                     << " chunks=" << chunk_count
                     << " total_bytes=" << total_bytes;
    } else {
        INFO(logger) << "streaming response failed (may be normal if server doesn't support chunked)";
    }
}

int main(int argc, char** argv) {
    chen::IOManager iom(2);

    // Run SSE client tests (no network needed)
    iom.schedule(test_sse_all);

    // Run HTTP tests (need network)
    iom.schedule(run);

    // Run streaming test
    iom.schedule(test_streaming);

    return 0;
}
