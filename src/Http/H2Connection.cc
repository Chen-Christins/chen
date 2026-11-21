#include "H2Connection.hpp"

#include <cstring>

#include "../Log/Log.hpp"
#include "../Socket/Address.hpp"
#include "../Socket/Socket.hpp"
#include "H2Util.hpp"

namespace chen {
namespace http {

static Logger::ptr logger = LOG_NAME("system");

H2Connection::~H2Connection()
{
    if (session_) {
        nghttp2_session_del(session_);
        session_ = nullptr;
    }
}

bool H2Connection::connect(const std::string& host, int port, uint64_t timeout_ms)
{
    // Resolve and connect
    auto addr = Address::LookupAnyIPAddress(host);
    if (!addr) {
        ERROR(logger) << "Failed to resolve: " << host;
        return false;
    }
    addr->setPort(port);

    socket_ = Socket::CreateTCP(addr);
    if (!socket_) {
        ERROR(logger) << "Failed to create socket";
        return false;
    }

    if (!socket_->connect(addr, timeout_ms)) {
        ERROR(logger) << "Failed to connect to " << host << ":" << port;
        return false;
    }

    // Create nghttp2 client session
    nghttp2_session_callbacks* callbacks;
    int rv = nghttp2_session_callbacks_new(&callbacks);
    if (rv != 0) {
        ERROR(logger) << "nghttp2_session_callbacks_new failed: " << nghttp2_strerror(rv);
        return false;
    }

    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
    nghttp2_session_callbacks_set_on_header_callback2(callbacks, on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers);
    nghttp2_session_callbacks_set_error_callback2(
        callbacks, [](nghttp2_session* session, int lib_error_code, const char* msg, size_t len, void* user_data) -> int {
            ERROR(LOG_NAME("system")) << "nghttp2 client error: " << std::string(msg, len) << " (code=" << lib_error_code << ")";
            return 0;
        });

    rv = nghttp2_session_client_new(&session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);

    if (rv != 0) {
        ERROR(logger) << "nghttp2_session_client_new failed: " << nghttp2_strerror(rv);
        return false;
    }

    // Submit SETTINGS to complete connection preface
    nghttp2_settings_entry iv[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535},
    };
    nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv, sizeof(iv) / sizeof(iv[0]));

    // Send connection preface + SETTINGS
    doSend();

    // Read server's SETTINGS ack
    uint8_t buf[65536];
    socket_->setRecvTimeout(timeout_ms);
    int len = socket_->recv(buf, sizeof(buf));
    if (len <= 0) {
        ERROR(logger) << "Failed to receive server SETTINGS";
        return false;
    }

    rv = nghttp2_session_mem_recv(session_, buf, len);
    if (rv < 0) {
        ERROR(logger) << "nghttp2_session_mem_recv failed: " << nghttp2_strerror((int)rv);
        return false;
    }

    // Send any pending frames (SETTINGS ACK)
    doSend();

    connected_ = true;
    return true;
}

HttpResult::ptr H2Connection::doRequest(HttpRequest::ptr req, uint64_t timeout_ms)
{
    if (!connected_) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::CONNECT_FAIL, nullptr, "Not connected");
    }

    // Build nghttp2_nv from the request.
    // We must keep all string data alive until nghttp2_submit_request() completes.
    // Use std::list to avoid reallocation-invalidation of c_str() pointers.
    std::vector<nghttp2_nv> nva;
    std::list<std::string> strs; // keeps temporary strings alive (no pointer invalidation)

    // :method
    strs.push_back(HttpMethodToString(req->getMethod()));
    nva.push_back(make_nv(":method", strs.back()));

    // :path
    strs.push_back(req->getPath());
    if (!req->getQuery().empty()) {
        strs.back() += "?" + req->getQuery();
    }
    nva.push_back(make_nv(":path", strs.back()));

    // :scheme
    strs.push_back(req->getScheme().empty() ? "http" : req->getScheme());
    nva.push_back(make_nv(":scheme", strs.back()));

    // :authority
    strs.push_back(req->getHeader("host"));
    nva.push_back(make_nv(":authority", strs.back()));

    // Regular headers
    for (auto& h : req->getHeaders()) {
        if (strcasecmp(h.first.c_str(), "host") == 0) {
            continue;
        }
        if (is_http1_specific_header(h.first)) {
            continue;
        }
        strs.push_back(to_lower(h.first));
        nva.push_back(make_nv(strs.back(), h.second));
    }

    // Submit request
    int32_t stream_id = nghttp2_submit_request(session_, nullptr, nva.data(), nva.size(), nullptr, nullptr);
    if (stream_id < 0) {
        return std::make_shared<HttpResult>(
            (int)HttpResult::Error::SEND_SOCKET_ERROR, nullptr, std::string("nghttp2_submit_request: ") + nghttp2_strerror(stream_id));
    }

    // Send request frames
    doSend();

    // Read and process response
    std::vector<uint8_t> buf(65536);
    while (true) {
        // Check if response is complete
        {
            std::shared_lock lock(resp_mutex_);
            auto it = responses_.find(stream_id);
            if (it != responses_.end() && it->second.complete) {
                if (!it->second.success) {
                    return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_CLOSE_BY_PEER, nullptr, "Stream closed with error");
                }
                auto rsp = it->second.response;
                if (!it->second.body.empty()) {
                    rsp->setBody(it->second.body);
                }
                return std::make_shared<HttpResult>(0, rsp, "");
            }
        }

        socket_->setRecvTimeout(timeout_ms);
        int len = socket_->recv(buf.data(), buf.size());
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
                // Timeout/retry, loop back to check for response completion
                doSend();
                continue;
            }
            return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_SOCKET_ERROR, nullptr, std::string("Recv error: ") + strerror(errno));
        }
        if (len == 0) {
            return std::make_shared<HttpResult>((int)HttpResult::Error::SEND_CLOSE_BY_PEER, nullptr, "Connection closed by peer");
        }
        nghttp2_session_mem_recv(session_, buf.data(), len);

        // Send any pending frames (e.g., WINDOW_UPDATE)
        doSend();
    }
}

// Static convenience methods using H2Connection for HTTP/2
HttpResult::ptr H2Connection::DoGet(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body)
{
    return DoRequest(HttpMethod::GET, url, timeout_ms, headers, body);
}

HttpResult::ptr H2Connection::DoPost(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body)
{
    return DoRequest(HttpMethod::POST, url, timeout_ms, headers, body);
}

HttpResult::ptr H2Connection::DoRequest(
    HttpMethod method,
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body)
{
    Uri::ptr uri = Uri::Create(url);
    if (!uri) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URL, nullptr, "Invalid URL: " + url);
    }

    auto conn = std::make_shared<H2Connection>();
    int port = uri->getPort();
    if (port == 0) {
        port = (uri->getScheme() == "https") ? 443 : 80;
    }

    if (!conn->connect(uri->getHost(), port, timeout_ms)) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::CONNECT_FAIL, nullptr, "Failed to connect");
    }

    auto req = std::make_shared<HttpRequest>(HttpRequest::kHttp2Version, false);
    req->setMethod(method);
    req->setPath(uri->getPath());
    req->setQuery(uri->getQuery());
    req->setScheme(uri->getScheme());
    req->setHeader("host", uri->getHost());
    if (!body.empty()) {
        req->setBody(body);
    }
    for (auto& h : headers) {
        req->setHeader(h.first, h.second);
    }

    return conn->doRequest(req, timeout_ms);
}

int H2Connection::on_begin_headers(nghttp2_session* session, const nghttp2_frame* frame, void* user_data)
{
    auto* conn = static_cast<H2Connection*>(user_data);
    if (frame->hd.type != NGHTTP2_HEADERS) {
        return 0;
    }
    if (frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
        std::unique_lock lock(conn->resp_mutex_);
        auto& resp = conn->responses_[frame->hd.stream_id];
        resp.response = std::make_shared<HttpResponse>(HttpRequest::kHttp2Version, false);
        resp.complete = false;
        resp.body.clear();
    }
    return 0;
}

int H2Connection::on_header(
    nghttp2_session* session,
    const nghttp2_frame* frame,
    nghttp2_rcbuf* name,
    nghttp2_rcbuf* value,
    uint8_t flags,
    void* user_data)
{
    auto* conn = static_cast<H2Connection*>(user_data);
    if (frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
        return 0;
    }

    std::shared_lock lock(conn->resp_mutex_);
    auto it = conn->responses_.find(frame->hd.stream_id);
    if (it == conn->responses_.end()) {
        return 0;
    }

    auto name_str = rcbuf_to_string(name);
    auto value_str = rcbuf_to_string(value);

    if (name_str == ":status") {
        int code = 0;
        try {
            code = std::stoi(value_str);
        } catch (...) {
        }
        it->second.response->setStatus((HttpStatus)code);
    } else if (name_str[0] != ':') {
        it->second.response->setHeader(name_str, value_str);
    }
    return 0;
}

int H2Connection::on_frame_recv(nghttp2_session* session, const nghttp2_frame* frame, void* user_data)
{
    auto* conn = static_cast<H2Connection*>(user_data);
    if (frame->hd.type == NGHTTP2_HEADERS && frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            std::shared_lock lock(conn->resp_mutex_);
            auto it = conn->responses_.find(frame->hd.stream_id);
            if (it != conn->responses_.end()) {
                it->second.complete = true;
            }
        }
    }
    return 0;
}

int H2Connection::on_data_chunk_recv(nghttp2_session* session, uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len, void* user_data)
{
    auto* conn = static_cast<H2Connection*>(user_data);
    std::shared_lock lock(conn->resp_mutex_);
    auto it = conn->responses_.find(stream_id);
    if (it != conn->responses_.end()) {
        it->second.body.append((const char*)data, len);
        if (flags & NGHTTP2_FLAG_END_STREAM) {
            it->second.complete = true;
        }
    }
    return 0;
}

int H2Connection::on_stream_close(nghttp2_session* session, int32_t stream_id, uint32_t error_code, void* user_data)
{
    auto* conn = static_cast<H2Connection*>(user_data);
    if (error_code != 0) {
        WARN(logger) << "H2 client stream " << stream_id << " closed with error: " << nghttp2_http2_strerror(error_code);
        std::unique_lock lock(conn->resp_mutex_);
        auto it = conn->responses_.find(stream_id);
        if (it != conn->responses_.end()) {
            it->second.success = false;
            it->second.complete = true;
        }
    }
    return 0;
}

void H2Connection::doSend()
{
    for (;;) {
        const uint8_t* data = nullptr;
        ssize_t rv = nghttp2_session_mem_send(session_, &data);
        if (rv < 0) {
            ERROR(logger) << "nghttp2_session_mem_send error: " << nghttp2_strerror((int)rv);
            break;
        }
        if (rv == 0) {
            break;
        }
        socket_->send(data, rv);
    }
}

} // namespace http
} // namespace chen
