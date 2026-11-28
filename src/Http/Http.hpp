/**
 * @file Http.hpp
 * @brief http模块
 * @author Christins
 * @date 2024-11-24
 * @copyright GPL-3.0
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/lexical_cast.hpp>

namespace chen::http {

/* Request Methods */
#define HTTP_METHOD_MAP(XX)          \
    XX(0, DELETE, DELETE)            \
    XX(1, GET, GET)                  \
    XX(2, HEAD, HEAD)                \
    XX(3, POST, POST)                \
    XX(4, PUT, PUT)                  \
    /* pathological */               \
    XX(5, CONNECT, CONNECT)          \
    XX(6, OPTIONS, OPTIONS)          \
    XX(7, TRACE, TRACE)              \
    /* WebDAV */                     \
    XX(8, COPY, COPY)                \
    XX(9, LOCK, LOCK)                \
    XX(10, MKCOL, MKCOL)             \
    XX(11, MOVE, MOVE)               \
    XX(12, PROPFIND, PROPFIND)       \
    XX(13, PROPPATCH, PROPPATCH)     \
    XX(14, SEARCH, SEARCH)           \
    XX(15, UNLOCK, UNLOCK)           \
    XX(16, BIND, BIND)               \
    XX(17, REBIND, REBIND)           \
    XX(18, UNBIND, UNBIND)           \
    XX(19, ACL, ACL)                 \
    /* subversion */                 \
    XX(20, REPORT, REPORT)           \
    XX(21, MKACTIVITY, MKACTIVITY)   \
    XX(22, CHECKOUT, CHECKOUT)       \
    XX(23, MERGE, MERGE)             \
    /* upnp */                       \
    XX(24, MSEARCH, M - SEARCH)      \
    XX(25, NOTIFY, NOTIFY)           \
    XX(26, SUBSCRIBE, SUBSCRIBE)     \
    XX(27, UNSUBSCRIBE, UNSUBSCRIBE) \
    /* RFC-5789 */                   \
    XX(28, PATCH, PATCH)             \
    XX(29, PURGE, PURGE)             \
    /* CalDAV */                     \
    XX(30, MKCALENDAR, MKCALENDAR)   \
    /* RFC-2068, section 19.6.1.2 */ \
    XX(31, LINK, LINK)               \
    XX(32, UNLINK, UNLINK)           \
    /* icecast */                    \
    XX(33, SOURCE, SOURCE)

/* Status Codes */
#define HTTP_STATUS_MAP(XX)                                                   \
    XX(100, CONTINUE, Continue)                                               \
    XX(101, SWITCHING_PROTOCOLS, Switching Protocols)                         \
    XX(102, PROCESSING, Processing)                                           \
    XX(200, OK, OK)                                                           \
    XX(201, CREATED, Created)                                                 \
    XX(202, ACCEPTED, Accepted)                                               \
    XX(203, NON_AUTHORITATIVE_INFORMATION, Non - Authoritative Information)   \
    XX(204, NO_CONTENT, No Content)                                           \
    XX(205, RESET_CONTENT, Reset Content)                                     \
    XX(206, PARTIAL_CONTENT, Partial Content)                                 \
    XX(207, MULTI_STATUS, Multi - Status)                                     \
    XX(208, ALREADY_REPORTED, Already Reported)                               \
    XX(226, IM_USED, IM Used)                                                 \
    XX(300, MULTIPLE_CHOICES, Multiple Choices)                               \
    XX(301, MOVED_PERMANENTLY, Moved Permanently)                             \
    XX(302, FOUND, Found)                                                     \
    XX(303, SEE_OTHER, See Other)                                             \
    XX(304, NOT_MODIFIED, Not Modified)                                       \
    XX(305, USE_PROXY, Use Proxy)                                             \
    XX(307, TEMPORARY_REDIRECT, Temporary Redirect)                           \
    XX(308, PERMANENT_REDIRECT, Permanent Redirect)                           \
    XX(400, BAD_REQUEST, Bad Request)                                         \
    XX(401, UNAUTHORIZED, Unauthorized)                                       \
    XX(402, PAYMENT_REQUIRED, Payment Required)                               \
    XX(403, FORBIDDEN, Forbidden)                                             \
    XX(404, NOT_FOUND, Not Found)                                             \
    XX(405, METHOD_NOT_ALLOWED, Method Not Allowed)                           \
    XX(406, NOT_ACCEPTABLE, Not Acceptable)                                   \
    XX(407, PROXY_AUTHENTICATION_REQUIRED, Proxy Authentication Required)     \
    XX(408, REQUEST_TIMEOUT, Request Timeout)                                 \
    XX(409, CONFLICT, Conflict)                                               \
    XX(410, GONE, Gone)                                                       \
    XX(411, LENGTH_REQUIRED, Length Required)                                 \
    XX(412, PRECONDITION_FAILED, Precondition Failed)                         \
    XX(413, PAYLOAD_TOO_LARGE, Payload Too Large)                             \
    XX(414, URI_TOO_LONG, URI Too Long)                                       \
    XX(415, UNSUPPORTED_MEDIA_TYPE, Unsupported Media Type)                   \
    XX(416, RANGE_NOT_SATISFIABLE, Range Not Satisfiable)                     \
    XX(417, EXPECTATION_FAILED, Expectation Failed)                           \
    XX(421, MISDIRECTED_REQUEST, Misdirected Request)                         \
    XX(422, UNPROCESSABLE_ENTITY, Unprocessable Entity)                       \
    XX(423, LOCKED, Locked)                                                   \
    XX(424, FAILED_DEPENDENCY, Failed Dependency)                             \
    XX(426, UPGRADE_REQUIRED, Upgrade Required)                               \
    XX(428, PRECONDITION_REQUIRED, Precondition Required)                     \
    XX(429, TOO_MANY_REQUESTS, Too Many Requests)                             \
    XX(431, REQUEST_HEADER_FIELDS_TOO_LARGE, Request Header Fields Too Large) \
    XX(451, UNAVAILABLE_FOR_LEGAL_REASONS, Unavailable For Legal Reasons)     \
    XX(500, INTERNAL_SERVER_ERROR, Internal Server Error)                     \
    XX(501, NOT_IMPLEMENTED, Not Implemented)                                 \
    XX(502, BAD_GATEWAY, Bad Gateway)                                         \
    XX(503, SERVICE_UNAVAILABLE, Service Unavailable)                         \
    XX(504, GATEWAY_TIMEOUT, Gateway Timeout)                                 \
    XX(505, HTTP_VERSION_NOT_SUPPORTED, HTTP Version Not Supported)           \
    XX(506, VARIANT_ALSO_NEGOTIATES, Variant Also Negotiates)                 \
    XX(507, INSUFFICIENT_STORAGE, Insufficient Storage)                       \
    XX(508, LOOP_DETECTED, Loop Detected)                                     \
    XX(510, NOT_EXTENDED, Not Extended)                                       \
    XX(511, NETWORK_AUTHENTICATION_REQUIRED, Network Authentication Required)

enum class HttpMethod {
#define XX(num, name, string) name = num,
    HTTP_METHOD_MAP(XX)
#undef XX
    INVALID_METHOD
};

enum class HttpStatus {
#define XX(code, name, description) name = code,
    HTTP_STATUS_MAP(XX)
#undef XX
};

/**
 * @brief 将String类型转换为Method
 * @param m 待转换的字符串
 * @return HttpMethod
 */
HttpMethod StringToHttpMethod(const std::string& m);

/**
 * @brief 将Char字符串转换为Method
 * @param m 待转换的字符串
 * @return HttpMethod
 */
HttpMethod CharsToHttpMethod(const char* m);

/**
 * @brief 将Method转换为字符串
 */
const char* HttpMethodToString(const HttpMethod& m);

/**
 * @brief 将Status转换为字符串
 */
const char* HttpStatusToString(const HttpStatus& s);

// 重载Map的比较函数，让其规则为大小写无关的比较
struct CaseInsensitiveLess {
    bool operator()(const std::string& lhs, const std::string& rhs) const;
};

template <class MapType, class T>
bool checkGetAs(const MapType& m, const std::string& key, T& val, const T& def = T())
{
    auto it = m.find(key);
    if (it == m.end()) {
        val = def;
        return false;
    }
    try {
        val = boost::lexical_cast<T>(it->second);
        return true;
    } catch (...) {
        val = def;
    }
    return false;
}

template <class MapType, class T>
T getAs(const MapType& m, const std::string& key, const T& def = T())
{
    auto it = m.find(key);
    if (it == m.end()) {
        return def;
    }
    try {
        return boost::lexical_cast<T>(it->second);
    } catch (...) {
    }
    return def;
}

class HttpResponse;

class HttpRequest {
public:
    typedef std::shared_ptr<HttpRequest> ptr;
    typedef std::map<std::string, std::string, CaseInsensitiveLess> MapType;

    static constexpr uint8_t kHttp2Version = 0x20;

    HttpRequest(uint8_t version = 0x11, bool close = true);
    std::shared_ptr<HttpResponse> createResponse();

    HttpMethod getMethod() const { return method_; }
    uint8_t getVersion() const { return version_; }
    const std::string& getPath() const { return path_; }
    const std::string& getQuery() const { return query_; }
    const std::string& getBody() const { return body_; }

    const MapType& getHeaders() const { return headers_; }
    const MapType& getParams() const { return params_; }
    const MapType& getCookie() const { return cookies_; }

    void setMethod(HttpMethod v) { method_ = v; }
    void setVersion(uint8_t v) { version_ = v; }
    void setPath(std::string v) { path_ = v; }
    void setQuery(std::string v) { query_ = v; }
    void setFragment(std::string v) { fragment_ = v; }
    void setBody(std::string v) { body_ = v; }

    void setWebsocket(bool v) { websocket_ = v; }
    bool isWebsocket() const { return websocket_; }

    bool isClose() const { return close_; }
    void setClose(bool v) { close_ = v; }

    // HTTP/2
    bool isHttp2() const { return version_ == kHttp2Version; }
    int32_t getStreamId() const { return stream_id_; }
    void setStreamId(int32_t v) { stream_id_ = v; }
    const std::string& getScheme() const { return scheme_; }
    void setScheme(const std::string& v) { scheme_ = v; }
    const std::string& getAuthority() const { return authority_; }
    void setAuthority(const std::string& v) { authority_ = v; }

    void setHeaders(const MapType& v) { headers_ = v; }
    void setParams(const MapType& v) { params_ = v; }
    void setCookies(const MapType& v) { cookies_ = v; }

    std::string getHeader(const std::string& key, const std::string& def = "") const;
    std::string getParam(const std::string& key, const std::string& def = "");
    std::string getCookie(const std::string& key, const std::string& def = "");

    void setHeader(const std::string& key, const std::string& val);
    void setParam(const std::string& key, const std::string& val);
    void setCookie(const std::string& key, const std::string& val);

    void delHeader(const std::string& key);
    void delParam(const std::string& key);
    void delCookie(const std::string& key);

    bool hasHeader(const std::string& key, std::string* val = nullptr);
    bool hasParam(const std::string& key, std::string* val = nullptr);
    bool hasCookie(const std::string& key, std::string* val = nullptr);

    template <class T>
    bool checkGetHeaderAs(const std::string& key, T& val, const T& def = T())
    {
        return checkGetAs(headers_, key, val, def);
    }
    template <class T>
    T getHeaderAs(const std::string& key, const T& def = T())
    {
        return getAs(headers_, key, def);
    }
    template <class T>
    bool checkGetParamAs(const std::string& key, T& val, const T& def = T())
    {
        initQueryParam();
        initBodyParam();
        return checkGetAs(params_, key, val, def);
    }
    template <class T>
    T getParamAs(const std::string& key, const T& def = T())
    {
        initQueryParam();
        initBodyParam();
        return getAs(params_, key, def);
    }
    template <class T>
    bool checkGetCookieAs(const std::string& key, T& val, const T& def = T())
    {
        initCookies();
        return checkGetAs(cookies_, key, val, def);
    }
    template <class T>
    T getCookieAs(const std::string& key, const T& def = T())
    {
        initCookies();
        return getAs(cookies_, key, def);
    }
    std::ostream& dump(std::ostream& os) const;
    std::string toString() const;

    void init();
    void initParam();
    void initQueryParam();
    void initBodyParam();
    void initCookies();

private:
    /// 请求方法
    HttpMethod method_;
    /// 协议版本
    uint8_t version_;
    /// 是否自动关闭
    bool close_;
    /// 是否为WebSocket协议
    bool websocket_;

    uint8_t parser_param_flag_;
    /// 请求路径
    std::string path_;
    /// 请求参数
    std::string query_;
    /// 请求的fragment
    std::string fragment_;
    /// 请求消息体
    std::string body_;
    /// 请求头
    MapType headers_;
    /// 请求参数MAP
    MapType params_;
    /// cookie MAP
    MapType cookies_;

    // HTTP/2
    int32_t stream_id_ = 0;
    std::string scheme_;
    std::string authority_;
};

class HttpResponse {
public:
    typedef std::shared_ptr<HttpResponse> ptr;
    typedef std::map<std::string, std::string, CaseInsensitiveLess> MapType;

    HttpResponse(uint8_t version = 0x11, bool close = true);

    HttpStatus getStatus() const { return status_; }
    uint8_t getVersion() const { return version_; }
    const std::string& getBody() const { return body_; }
    const std::string& getReason() const { return reason_; }
    const MapType& getHeaders() const { return headers_; }

    void setStatus(HttpStatus v) { status_ = v; }
    void setVersion(uint8_t v) { version_ = v; }
    void setBody(const std::string& v) { body_ = v; }
    void setReason(const std::string& v) { reason_ = v; }
    void setHeaders(const MapType& v) { headers_ = v; }

    void setWebsocket(bool v) { websocket_ = v; }
    bool isWebsocket() const { return websocket_; }

    void setStreaming(bool v) { streaming_ = v; }
    bool isStreaming() const { return streaming_; }

    bool isClose() const { return close_; }
    void setClose(bool v) { close_ = v; }

    /**
     * @brief 根据响应头补全状态
     * @details 解析 `connection` 头，决定这条连接之后能否复用；
     *          客户端解析出响应后应调用（否则 `isClose()` 恒为构造时的默认值）
     */
    void init();

    // HTTP/2
    bool isHttp2() const { return version_ == HttpRequest::kHttp2Version; }
    const std::vector<std::string>& getCookies() const { return cookies_; }

    std::string getHeader(const std::string& key, std::string def = "") const;
    void setHeader(const std::string& key, const std::string& val);
    void delHeader(const std::string& key);

    template <class T>
    bool checkGetHeaderAs(const std::string& key, T& val, const T& def = T())
    {
        return checkGetAs(headers_, key, val, def);
    }

    template <class T>
    T getHeaderAs(const std::string& key, const T& def = T())
    {
        return getAs(headers_, key, def);
    }

    std::ostream& dump(std::ostream& os) const;
    std::string toString() const;

    void setRedirect(const std::string& uri);
    void setCookie(
        const std::string& key,
        const std::string& val,
        time_t expired = 0,
        const std::string& path = "",
        const std::string& domain = "",
        bool secure = false);

private:
    /// 响应状态
    HttpStatus status_;
    /// 协议版本
    uint8_t version_;
    /// 是否关闭
    bool close_;
    /// 是否为WebSocket协议
    bool websocket_;
    /// 是否为流式响应（SSE等），此时 handler 自行管理连接
    bool streaming_ = false;
    /// 响应体
    std::string body_;
    /// 返回状态码的原因
    std::string reason_;
    /// 响应头
    MapType headers_;

    std::vector<std::string> cookies_;
};

std::ostream& operator<<(std::ostream& os, const HttpRequest& req);
std::ostream& operator<<(std::ostream& os, const HttpResponse& rsp);

} // namespace chen::http
