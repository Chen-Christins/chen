/**
 * @file http_client.cc
 * @brief HTTP 异步客户端工具实现
 * @author chen
 * @date 2026-10-25
 * @copyright GPL-3.0
 */
#include "http_client.h"

#include <exception>
#include <memory>

#include "../iomanager/iomanager.h"
#include "../log/log.h"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

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
            result = HttpConnection::DoRequest(method, url, timeout_ms, headers, body);
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
