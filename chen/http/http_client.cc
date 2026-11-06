/**
 * @file http_client.cc
 * @brief HTTP 异步客户端工具实现
 * @author chen
 * @date 2026-10-25
 * @copyright GPL-3.0
 */
#include "http_client.h"

#include <exception>
#include <map>
#include <memory>
#include <mutex>

#include "../config/config.h"
#include "../iomanager/iomanager.h"
#include "../log/log.h"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

/// 是否复用连接池
static ConfigVar<bool>::ptr g_clientPoolEnable = Config::Lookup<bool>("http.client.pool_enable", true, "HttpClient 是否复用连接池");
/// 每个 host 的连接池上限
static ConfigVar<uint32_t>::ptr g_clientPoolSize = Config::Lookup<uint32_t>("http.client.pool_size", 8, "每个 host 的连接池上限");
/// 连接最长存活时间（毫秒）
static ConfigVar<uint32_t>::ptr g_clientPoolMaxAlive = Config::Lookup<uint32_t>("http.client.max_alive_time", 30000, "连接池中连接的最长存活毫秒数");
/// 单条连接最多复用次数
static ConfigVar<uint32_t>::ptr g_clientPoolMaxRequest = Config::Lookup<uint32_t>("http.client.max_request", 100, "连接池中单条连接的最大复用次数");

namespace {

/**
 * @brief 连接池注册表：scheme://host:port -> pool
 */
struct PoolRegistry {
    /// 保护 pools
    std::mutex mutex;
    /// 连接池集合
    std::map<std::string, HttpConnectionPool::ptr> pools;
};

PoolRegistry& poolRegistry()
{
    static PoolRegistry registry;
    return registry;
}

/**
 * @brief 获取（或创建）uri 对应的连接池
 */
HttpConnectionPool::ptr getPool(const Uri::ptr& uri)
{
    const std::string scheme = uri->getScheme();
    const std::string key = scheme + "://" + uri->getHost() + ":" + std::to_string(uri->getPort());

    PoolRegistry& registry = poolRegistry();
    std::lock_guard lock(registry.mutex);
    auto it = registry.pools.find(key);
    if (it != registry.pools.end()) {
        return it->second;
    }

    HttpConnectionPool::ptr pool = std::make_shared<HttpConnectionPool>(
        uri->getHost(), uri->getHost(), uri->getPort(), g_clientPoolSize->getValue(), g_clientPoolMaxAlive->getValue(),
        g_clientPoolMaxRequest->getValue(), scheme == "https");
    registry.pools.emplace(key, pool);
    DEBUG(logger) << "HttpClient create pool, key=" << key << " ssl=" << (scheme == "https");
    return pool;
}

/**
 * @brief 是否是可重试的空闲连接失效错误
 * @details 池里取出的连接可能已被对端关闭，而 TCP 半关闭对 isConnected() 不可见
 */
bool isStaleConnectionError(int code)
{
    return code == (int)HttpResult::Error::POOL_INVALID_CONNECTION || code == (int)HttpResult::Error::SEND_CLOSE_BY_PEER
        || code == (int)HttpResult::Error::SEND_SOCKET_ERROR;
}

/**
 * @brief 执行一次请求：优先走连接池
 */
HttpResult::ptr doRequest(
    HttpMethod method,
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body)
{
    if (!g_clientPoolEnable->getValue()) {
        return HttpConnection::DoRequest(method, url, timeout_ms, headers, body);
    }

    Uri::ptr uri = Uri::Create(url);
    if (!uri) {
        return std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URL, nullptr, "invalid url: " + url);
    }
    const std::string& scheme = uri->getScheme();
    if (scheme != "http" && scheme != "https") {
        return HttpConnection::DoRequest(method, url, timeout_ms, headers, body);
    }

    HttpConnectionPool::ptr pool = getPool(uri);
    // 复用连接时必须用 keep-alive
    HttpResult::ptr result = pool->doRequest(HttpConnection::BuildRequest(method, uri, headers, body, false), timeout_ms);
    if (result && isStaleConnectionError(result->result)) {
        WARN(logger) << "HttpClient retry with fresh connection, url=" << url << " err=" << result->error;
        result = HttpConnection::DoRequest(HttpConnection::BuildRequest(method, uri, headers, body, true), uri, timeout_ms);
    }
    return result;
}

} // namespace

void HttpClient::clearPools()
{
    PoolRegistry& registry = poolRegistry();
    std::lock_guard lock(registry.mutex);
    registry.pools.clear();
}

bool HttpClient::submit(
    HttpMethod method,
    const std::string& url,
    uint64_t timeout_ms,
    Callback cb,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    Scheduler* sched = ResolveScheduler(scheduler);
    if (!sched) {
        ERROR(logger) << "HttpClient submit failed, no scheduler available, url=" << url;
        return false;
    }

    sched->schedule([method, url, timeout_ms, cb, headers, body]() {
        HttpResult::ptr result;
        try {
            result = doRequest(method, url, timeout_ms, headers, body);
        } catch (const std::exception& e) {
            ERROR(logger) << "HttpClient request exception, url=" << url << " err=" << e.what();
        } catch (...) {
            ERROR(logger) << "HttpClient request exception, url=" << url << " err=unknown";
        }

        if (cb) {
            cb(result);
            return;
        }
        if (!result || result->result != 0) {
            WARN(logger) << "HttpClient request failed, url=" << url << " err=" << (result ? result->error : std::string("exception"));
        }
    });
    return true;
}

Future<HttpResult::ptr> HttpClient::request(
    HttpMethod method,
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    std::shared_ptr<Promise<HttpResult::ptr>> promise = std::make_shared<Promise<HttpResult::ptr>>();
    Future<HttpResult::ptr> future = promise->getFuture();

    const bool submitted = submit(
        method, url, timeout_ms,
        [promise](HttpResult::ptr result) {
            promise->setValue(std::move(result));
        },
        headers, body, scheduler);

    if (!submitted) {
        // 没有可用调度器：返回已完成且值为空的 Future，调用方通过判空处理
        promise->setValue(nullptr);
    }
    return future;
}

Future<HttpResult::ptr> HttpClient::get(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    return request(HttpMethod::GET, url, timeout_ms, headers, body, scheduler);
}

Future<HttpResult::ptr> HttpClient::post(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    return request(HttpMethod::POST, url, timeout_ms, headers, body, scheduler);
}

Future<HttpResult::ptr> HttpClient::put(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    return request(HttpMethod::PUT, url, timeout_ms, headers, body, scheduler);
}

Future<HttpResult::ptr> HttpClient::patch(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    return request(HttpMethod::PATCH, url, timeout_ms, headers, body, scheduler);
}

Future<HttpResult::ptr> HttpClient::del(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    return request(HttpMethod::DELETE, url, timeout_ms, headers, body, scheduler);
}

Future<HttpResult::ptr> HttpClient::head(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    return request(HttpMethod::HEAD, url, timeout_ms, headers, body, scheduler);
}

Future<HttpResult::ptr> HttpClient::options(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    Scheduler* scheduler)
{
    return request(HttpMethod::OPTIONS, url, timeout_ms, headers, body, scheduler);
}

} // namespace chen::http
