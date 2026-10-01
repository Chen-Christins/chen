/**
 * @file h2_connection.h
 * @brief HTTP/2 连接管理
 */
#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include <nghttp2/nghttp2.h>

#include "http.h"
#include "http_connection.h"

namespace chen {

class Socket;
class IOManager;

namespace http {

class H2Session;

class H2Connection : public std::enable_shared_from_this<H2Connection> {
public:
    typedef std::shared_ptr<H2Connection> ptr;

    ~H2Connection();

    // Connect to host:port and establish HTTP/2 session
    bool connect(const std::string& host, int port, uint64_t timeout_ms);

    // Perform a single HTTP request over the HTTP/2 connection.
    HttpResult::ptr doRequest(HttpRequest::ptr req, uint64_t timeout_ms);

    // Static convenience methods
    static HttpResult::ptr DoGet(const std::string& url, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {},
                                 const std::string& body = "");

    static HttpResult::ptr DoPost(const std::string& url, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {},
                                  const std::string& body = "");

    static HttpResult::ptr DoRequest(HttpMethod method, const std::string& url, uint64_t timeout_ms,
                                     const std::map<std::string, std::string>& headers = {}, const std::string& body = "");

private:
    // nghttp2 callbacks for client side
    static int on_frame_recv(nghttp2_session* session, const nghttp2_frame* frame, void* user_data);

    static int on_header(nghttp2_session* session, const nghttp2_frame* frame, nghttp2_rcbuf* name, nghttp2_rcbuf* value,
                         uint8_t flags, void* user_data);
    
    static int on_data_chunk_recv(nghttp2_session* session, uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len,
                                  void* user_data);
    
    static int on_stream_close(nghttp2_session* session, int32_t stream_id, uint32_t error_code, void* user_data);
    
    static int on_begin_headers(nghttp2_session* session, const nghttp2_frame* frame, void* user_data);

    void doSend();
    
    bool runRequest(int32_t stream_id, uint64_t timeout_ms);

private:
    Socket::ptr m_socket;
    nghttp2_session* m_session = nullptr;
    bool m_connected = false;

    // Pending response data (built by callbacks)
    struct PendingResponse {
        HttpResponse::ptr response;
        std::string body;
        bool complete = false;
        bool success = true;
    };
    std::unordered_map<int32_t, PendingResponse> m_responses;
    std::shared_mutex m_respMutex;
};

} // namespace http
} // namespace chen
