#include "Http.hpp"

#include "../Util/JsonUtil.hpp"
#include "../Util/StringUtil.hpp"
#include "../Util/TimeUtil.hpp"

namespace chen::http {

HttpMethod StringToHttpMethod(const std::string& m)
{
#define XX(num, name, string)              \
    if (strcmp(#string, m.c_str()) == 0) { \
        return HttpMethod::name;           \
    }
    HTTP_METHOD_MAP(XX)
#undef XX
    return HttpMethod::INVALID_METHOD;
}

HttpMethod CharsToHttpMethod(const char* m)
{
#define XX(num, name, string)                        \
    if (strncmp(#string, m, strlen(#string)) == 0) { \
        return HttpMethod::name;                     \
    }
    HTTP_METHOD_MAP(XX)
#undef XX
    return HttpMethod::INVALID_METHOD;
}

static const char* s_method_string[] = {
#define XX(num, name, string) #string,
    HTTP_METHOD_MAP(XX)
#undef XX
};

const char* HttpMethodToString(const HttpMethod& m)
{
    uint32_t idx = (uint32_t)m;
    if (idx >= sizeof(s_method_string) / sizeof(s_method_string[0])) {
        return "<unknown>";
    }
    return s_method_string[idx];
}

const char* HttpStatusToString(const HttpStatus& s)
{
    switch (s) {
#define XX(code, name, msg) \
    case HttpStatus::name:  \
        return #msg;
        HTTP_STATUS_MAP(XX)
#undef XX
    default:
        return "<unknown>";
    }
}

bool CaseInsensitiveLess::operator()(const std::string& lhs, const std::string& rhs) const
{
    return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
}

HttpRequest::HttpRequest(uint8_t version, bool close)
    : method_(HttpMethod::GET)
    , version_(version)
    , close_(close)
    , websocket_(false)
    , parser_param_flag_(0)
    , path_("/")
{
}

std::shared_ptr<HttpResponse> HttpRequest::createResponse()
{
    HttpResponse::ptr rsp(std::make_shared<HttpResponse>(getVersion(), isClose()));
    return rsp;
}

std::string HttpRequest::getHeader(const std::string& key, const std::string& def) const
{
    auto it = headers_.find(key);
    return it == headers_.end() ? def : it->second;
}

std::string HttpRequest::getParam(const std::string& key, const std::string& def)
{
    initQueryParam();
    initBodyParam();
    auto it = params_.find(key);
    return it == params_.end() ? def : it->second;
}

std::string HttpRequest::getCookie(const std::string& key, const std::string& def)
{
    initCookies();
    auto it = cookies_.find(key);
    return it == cookies_.end() ? def : it->second;
}

void HttpRequest::setHeader(const std::string& key, const std::string& val)
{
    headers_[key] = val;
}

void HttpRequest::setParam(const std::string& key, const std::string& val)
{
    params_[key] = val;
}

void HttpRequest::setCookie(const std::string& key, const std::string& val)
{
    cookies_[key] = val;
}

void HttpRequest::delHeader(const std::string& key)
{
    headers_.erase(key);
}

void HttpRequest::delParam(const std::string& key)
{
    params_.erase(key);
}

void HttpRequest::delCookie(const std::string& key)
{
    cookies_.erase(key);
}

bool HttpRequest::hasHeader(const std::string& key, std::string* val)
{
    auto it = headers_.find(key);
    if (it == headers_.end()) {
        return false;
    }
    if (val) {
        *val = it->second;
    }
    return true;
}

bool HttpRequest::hasParam(const std::string& key, std::string* val)
{
    initQueryParam();
    initBodyParam();
    auto it = params_.find(key);
    if (it == params_.end()) {
        return false;
    }
    if (val) {
        *val = it->second;
    }
    return true;
}

bool HttpRequest::hasCookie(const std::string& key, std::string* val)
{
    initCookies();
    auto it = cookies_.find(key);
    if (it == cookies_.end()) {
        return false;
    }
    if (val) {
        *val = it->second;
    }
    return true;
}

std::ostream& HttpRequest::dump(std::ostream& os) const
{
    // GET /url HTTP/1.1
    // Host: www.chen.top
    if (version_ == kHttp2Version) {
        os << "[HTTP/2 stream=" << stream_id_ << "] " << HttpMethodToString(method_) << " "
           << (scheme_.empty() ? "" : scheme_ + "://") << (authority_.empty() ? "" : authority_) << path_
           << (query_.empty() ? "" : "?") << query_ << (fragment_.empty() ? "" : "#") << fragment_ << "\r\n";
    } else {
        os << HttpMethodToString(method_) << " " << path_ << (query_.empty() ? "" : "?") << query_
           << (fragment_.empty() ? "" : "#") << fragment_ << " HTTP/" << ((uint32_t)version_ >> 4) << "."
           << ((uint32_t)(version_ & 0x0F)) << "\r\n";
    }
    if (!websocket_) {
        os << "connection: " << (close_ ? "close" : "keep-alive") << "\r\n";
    }
    for (auto& i : headers_) {
        if (!websocket_ && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        os << i.first << ": " << i.second << "\r\n";
    }
    if (!body_.empty()) {
        os << "content-length: " << body_.length() << "\r\n\r\n" << body_;
    } else {
        os << "\r\n";
    }
    return os;
}

std::string HttpRequest::toString() const
{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

void HttpRequest::init()
{
    std::string conn = getHeader("connection");
    if (!conn.empty()) {
        if (strcasecmp(conn.c_str(), "keep-alive") == 0) {
            close_ = false;
        } else {
            close_ = true;
        }
    }
}

void HttpRequest::initParam()
{
    initQueryParam();
    initBodyParam();
    initCookies();
}

void HttpRequest::initQueryParam()
{
    if (parser_param_flag_ & 0x1) {
        return;
    }

#define PARSE_PARAM(str, m, flag, trim)                                                                                          \
    size_t pos = 0;                                                                                                              \
    do {                                                                                                                         \
        size_t last = pos;                                                                                                       \
        pos = str.find('=', pos);                                                                                                \
        if (pos == std::string::npos) {                                                                                          \
            break;                                                                                                               \
        }                                                                                                                        \
        size_t key = pos;                                                                                                        \
        pos = str.find(flag, pos);                                                                                               \
        m.insert(std::make_pair(trim(str.substr(last, key - last)), StringUtil::UrlDecode(str.substr(key + 1, pos - key - 1)))); \
        if (pos == std::string::npos) {                                                                                          \
            break;                                                                                                               \
        }                                                                                                                        \
        ++pos;                                                                                                                   \
    } while (true);

    PARSE_PARAM(query_, params_, '&', );
    parser_param_flag_ |= 0x1;
}

static void flattenJsonToParams(const Json::Value& json, HttpRequest::MapType& params, const std::string& prefix)
{
    switch (json.type()) {
    case Json::objectValue: {
        Json::Value::Members members = json.getMemberNames();
        for (auto& name : members) {
            std::string key = prefix.empty() ? name : prefix + "." + name;
            flattenJsonToParams(json[name], params, key);
        }
        break;
    }
    case Json::arrayValue: {
        for (Json::ArrayIndex i = 0; i < json.size(); ++i) {
            std::string key = prefix + "[" + std::to_string(i) + "]";
            flattenJsonToParams(json[i], params, key);
        }
        break;
    }
    case Json::stringValue:
        params[prefix] = json.asString();
        break;
    case Json::intValue:
        params[prefix] = std::to_string(json.asInt());
        break;
    case Json::uintValue:
        params[prefix] = std::to_string(json.asUInt());
        break;
    case Json::realValue:
        params[prefix] = std::to_string(json.asDouble());
        break;
    case Json::booleanValue:
        params[prefix] = json.asBool() ? "true" : "false";
        break;
    case Json::nullValue:
        params[prefix] = "null";
        break;
    default:
        break;
    }
}

void HttpRequest::initBodyParam()
{
    if (parser_param_flag_ & 0x2) {
        return;
    }
    std::string content_type = getHeader("content-type");
    if (strcasestr(content_type.c_str(), "application/x-www-form-urlencoded") != nullptr) {
        PARSE_PARAM(body_, params_, '&', );
    } else if (strcasestr(content_type.c_str(), "application/json") != nullptr) {
        Json::Value json;
        if (JsonUtil::FromString(json, body_)) {
            flattenJsonToParams(json, params_, "");
        }
    }
    parser_param_flag_ |= 0x2;
}

void HttpRequest::initCookies()
{
    if (parser_param_flag_ & 0x4) {
        return;
    }
    std::string cookie = getHeader("cookie");
    if (cookie.empty()) {
        parser_param_flag_ |= 0x4;
        return;
    }
    PARSE_PARAM(cookie, cookies_, ';', StringUtil::Trim);
    parser_param_flag_ |= 0x4;
}

HttpResponse::HttpResponse(uint8_t version, bool close)
    : status_(HttpStatus::OK)
    , version_(version)
    , close_(close)
    , websocket_(false)
{
}

void HttpResponse::init()
{
    std::string conn = getHeader("connection");
    if (!conn.empty()) {
        if (strcasecmp(conn.c_str(), "keep-alive") == 0) {
            close_ = false;
        } else {
            close_ = true;
        }
    }
}

std::string HttpResponse::getHeader(const std::string& key, std::string def) const
{
    auto it = headers_.find(key);
    return it == headers_.end() ? def : it->second;
}

void HttpResponse::setHeader(const std::string& key, const std::string& val)
{
    headers_[key] = val;
}

void HttpResponse::delHeader(const std::string& key)
{
    headers_.erase(key);
}

std::ostream& HttpResponse::dump(std::ostream& os) const
{
    if (version_ == HttpRequest::kHttp2Version) {
        os << "[HTTP/2] :status=" << (uint32_t)status_ << " "
           << (reason_.empty() ? HttpStatusToString(status_) : reason_) << "\r\n";
    } else {
        os << "HTTP/" << ((uint32_t)(version_ >> 4)) << "." << ((uint32_t)(version_ & 0x0F)) << " " << (uint32_t)status_
           << " " << (reason_.empty() ? HttpStatusToString(status_) : reason_) << "\r\n";
    }

    for (auto& i : headers_) {
        if (!websocket_ && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        os << i.first << ": " << i.second << "\r\n";
    }
    for (auto& i : cookies_) {
        os << "Set-Cookie: " << i << "\r\n";
    }
    if (!websocket_) {
        os << "connection: " << (close_ ? "close" : "keep-alive") << "\r\n";
    }

    if (!body_.empty()) {
        os << "content-length: " << body_.size() << "\r\n\r\n" << body_;
    } else {
        os << "\r\n";
    }
    return os;
}

std::string HttpResponse::toString() const
{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

void HttpResponse::setRedirect(const std::string& uri)
{
    status_ = HttpStatus::FOUND;
    setHeader("Location", uri);
}

void HttpResponse::setCookie(
    const std::string& key,
    const std::string& val,
    time_t expired,
    const std::string& path,
    const std::string& domain,
    bool secure)
{
    std::stringstream ss;
    ss << key << "=" << val;
    if (expired > 0) {
        ss << ";expires=" << TimeUtil::Time2StrSec(expired, "%a, %d %b %Y %H:%M:%S") << " GMT";
    }
    if (!domain.empty()) {
        ss << ";domain=" << domain;
    }
    if (!path.empty()) {
        ss << ";path=" << path;
    }
    if (secure) {
        ss << ";secure";
    }
    cookies_.push_back(ss.str());
}

std::ostream& operator<<(std::ostream& os, const HttpRequest& req)
{
    return req.dump(os);
}

std::ostream& operator<<(std::ostream& os, const HttpResponse& rsp)
{
    return rsp.dump(os);
}

} // namespace chen::http
