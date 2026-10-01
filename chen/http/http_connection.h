/**
 * @file http_connection.h
 * @brief HTTP 连接类和连接池类的声明
 * @author Christins
 * @date 2025-01-19
 */
#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <mutex>

#include "../socket/socket_stream.h"
#include "http.h"
#include "uri.h"

namespace chen::http {

/**
 * @brief 流式数据回调类型
 * @param data 数据指针
 * @param len 数据长度
 * @return true 继续接收 | false 停止流式传输
 */
using HttpStreamCallback = std::function<bool(const char* data, size_t len)>;

struct HttpResult {
    typedef std::shared_ptr<HttpResult> ptr;
    enum class Error {
        OK = 0,
        INVALID_URL = 1,
        INVALID_HOST = 2,
        CONNECT_FAIL = 3,
        SEND_CLOSE_BY_PEER = 4,
        SEND_SOCKET_ERROR = 5,
        TIMEOUT = 6,
        CREATE_SOCKET_ERROR = 7,
        POOL_GET_CONNECTION = 8,
        POOL_INVALID_CONNECTION = 9,
    };

    HttpResult(int _result, HttpResponse::ptr _response, const std::string& _error)
        :result(_result)
        ,response(_response)
        ,error(_error) {}

    int result;
    HttpResponse::ptr response;
    std::string error;

    std::string toString() const;
};

class HttpConnectionPool;

class HttpConnection : public SocketStream {
    friend HttpConnectionPool;

public:
    typedef std::shared_ptr<HttpConnection> ptr;

    static HttpResult::ptr DoGet(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");
    
    static HttpResult::ptr DoGet(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoPost(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoPost(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoPut(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoPut(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoPatch(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoPatch(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoDelete(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoDelete(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoHead(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoHead(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoOptions(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoOptions(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoRequest(HttpMethod method
                ,const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoRequest(HttpMethod method
                ,Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");
    
    static HttpResult::ptr DoRequest(HttpRequest::ptr req
                ,Uri::ptr uri
                ,uint64_t timeout_ms);

    static HttpResult::ptr DoRequestStreaming(HttpMethod method
                ,const std::string& url
                ,uint64_t timeout_ms
                ,HttpStreamCallback callback
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoRequestStreaming(HttpMethod method
                ,Uri::ptr uri
                ,uint64_t timeout_ms
                ,HttpStreamCallback callback
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    static HttpResult::ptr DoRequestStreaming(HttpRequest::ptr req
                ,Uri::ptr uri
                ,uint64_t timeout_ms
                ,HttpStreamCallback callback);

    HttpConnection(Socket::ptr sock, bool owner = true);
    ~HttpConnection();
    HttpResponse::ptr recvResponse();

    /**
     * @brief 以流式方式接收 HTTP 响应
     * @param callback 流式数据回调，每收到一块 body 数据时调用
     * @return HttpResponse::ptr 含响应头和状态码（body 为空），失败返回 nullptr
     * @details 与 recvResponse() 不同，此方法不会将整个 body 累积到内存中，
     *          而是在接收到每个 chunk（chunked 传输）或每次读取到数据时（Content-Length）
     *          立即回调 callback。callback 返回 false 可提前终止接收。
     */
    HttpResponse::ptr recvResponseStreaming(HttpStreamCallback callback = nullptr);

    int sendRequest(HttpRequest::ptr req);

private:
    uint64_t m_createTime = 0;
    uint64_t m_request = 0;
};

class HttpConnectionPool {
public:
    typedef std::shared_ptr<HttpConnectionPool> ptr;

    HttpConnectionPool(const std::string& host, const std::string& vhost
                    ,int32_t port
                    ,uint32_t max_size
                    ,uint32_t max_alive_time
                    ,uint32_t max_request);

    HttpConnection::ptr getConnection();

    HttpResult::ptr doGet(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");
    
    HttpResult::ptr doGet(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doPost(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doPost(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doPut(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doPut(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doPatch(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doPatch(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doDelete(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doDelete(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doHead(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doHead(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doOptions(const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doOptions(Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doRequest(HttpMethod method
                ,const std::string& url
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");

    HttpResult::ptr doRequest(HttpMethod method
                ,Uri::ptr uri
                ,uint64_t timeout_ms
                ,const std::map<std::string, std::string>& headers = {}
                ,const std::string& body = "");
    
    HttpResult::ptr doRequest(HttpRequest::ptr req, uint64_t timeout_ms);

private:
    static void ReleasePtr(HttpConnection* ptr, HttpConnectionPool* pool);

private:
    std::string m_host;
    std::string m_vhost;
    uint32_t m_port;
    uint32_t m_maxSize;
    uint32_t m_maxAliveTime;
    uint32_t m_maxRequest;

    std::mutex m_mutex;
    std::list<HttpConnection*> m_conns;
    std::atomic<int32_t> m_total = {0};
};

} // namespace chen::http
