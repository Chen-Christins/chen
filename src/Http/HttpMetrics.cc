/**
 * @file HttpMetrics.cc
 * @brief HTTP 请求指标聚合器的实现
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "HttpMetrics.hpp"

#include "../Config/Config.hpp"
#include "../Util/TimeUtil.hpp"

#include <algorithm>
#include <mutex>

namespace chen::http {

namespace {

/// 是否启用 HTTP 请求指标（关闭后不记录总量与耗时，在途计数仍然维护）
ConfigVar<bool>::ptr g_http_metrics_enabled = Config::Lookup<bool>("http.metrics.enabled",
    true,
    "启用 HTTP 请求指标（http_requests_total / http_request_duration_seconds）");

/// 不同 (method, path) 组合的上限，超出后新路径统一记为 other
ConfigVar<uint64_t>::ptr g_http_metrics_max_paths = Config::Lookup<uint64_t>("http.metrics.max_paths",
    (uint64_t)512,
    "HTTP 请求指标跟踪的最大路径数，超出后新路径记为 other");

constexpr const char* kOtherPath = "other";
constexpr const char* kUnknownMethod = "UNKNOWN";
/// series key 的分隔符：方法名与路径都不会包含该字符
constexpr char kKeySep = '\x1f';

const char* kRequestsTotalHelp = "Total number of HTTP requests handled, by method, path and status";
const char* kDurationHelp =
    "HTTP request handling latency in seconds, from dispatch to response written, by method and path";
const char* kInFlightHelp = "Number of HTTP requests currently being handled";

metrics::Registry* registry()
{
    return metrics::RegistryMgr::GetInstance();
}

std::string makeKey(const std::string& method, const std::string& path)
{
    std::string key;
    key.reserve(method.size() + path.size() + 1);
    key.append(method).append(1, kKeySep).append(path);
    return key;
}

} // namespace

HttpMetrics::HttpMetrics(size_t max_paths)
{
    max_paths_ =
        max_paths > 0 ? max_paths : static_cast<size_t>(std::max<uint64_t>(1, g_http_metrics_max_paths->getValue()));
    in_flight_ = registry()->getGauge("http_requests_in_flight", kInFlightHelp);
}

HttpMetrics* HttpMetrics::GetInstance()
{
    static HttpMetrics* instance = new HttpMetrics();
    return instance;
}

std::string HttpMetrics::normalizePath(const std::string& path)
{
    if (path.empty()) {
        return "/";
    }

    std::string result;
    result.reserve(path.size());
    bool last_is_slash = false;
    for (char c : path) {
        if (c == '/') {
            // 合并重复的 '/'
            if (!last_is_slash) {
                result.push_back(c);
            }
            last_is_slash = true;
        } else {
            result.push_back(c);
            last_is_slash = false;
        }
    }

    if (result.empty()) {
        return "/";
    }
    // 去掉末尾多余的 '/'（根路径除外）
    if (result.size() > 1 && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

void HttpMetrics::onRequestStart()
{
    if (in_flight_) {
        in_flight_->inc();
    }
}

void HttpMetrics::onRequestAborted()
{
    if (in_flight_) {
        in_flight_->dec();
    }
}

void HttpMetrics::onRequestEnd(const std::string& method, const std::string& path, int status, uint64_t duration_us)
{
    // 在途计数必须无条件归还，即使指标被中途关闭也不能泄漏
    if (in_flight_) {
        in_flight_->dec();
    }

    if (!g_http_metrics_enabled->getValue()) {
        return;
    }

    const std::string effective_method = method.empty() ? kUnknownMethod : method;
    const std::string normalized_path = normalizePath(path);

    Series* series = getSeries(effective_method, normalized_path);
    if (!series) {
        return;
    }
    metrics::Counter* counter = getCounter(series, effective_method, status);
    if (counter) {
        counter->inc();
    }
    if (series->latency) {
        series->latency->observe(static_cast<double>(duration_us) / 1000000.0);
    }
}

HttpMetrics::Series* HttpMetrics::getSeries(const std::string& method, const std::string& path)
{
    const std::string key = makeKey(method, path);

    {
        std::shared_lock lock(mutex_);
        auto it = series_.find(key);
        if (it != series_.end()) {
            return it->second.get();
        }
    }

    std::unique_lock lock(mutex_);
    auto it = series_.find(key);
    if (it != series_.end()) {
        return it->second.get();
    }

    // 基数上限：超限后新路径统一记为 other（other 自身仍只占一个 series）
    std::string effective_path = path;
    std::string effective_key = key;
    if (series_.size() >= max_paths_ && path != kOtherPath) {
        effective_path = kOtherPath;
        effective_key = makeKey(method, effective_path);
        it = series_.find(effective_key);
        if (it != series_.end()) {
            return it->second.get();
        }
    }

    auto series = std::make_unique<Series>();
    series->label_path = effective_path;
    series->latency = registry()->getHistogram(
        "http_request_duration_seconds", kDurationHelp, metrics::Labels{{"method", method}, {"path", effective_path}});
    auto* result = series.get();
    series_.emplace(effective_key, std::move(series));
    return result;
}

metrics::Counter* HttpMetrics::getCounter(Series* series, const std::string& method, int status)
{
    {
        std::shared_lock lock(mutex_);
        auto it = series->counters.find(status);
        if (it != series->counters.end()) {
            return it->second;
        }
    }

    std::unique_lock lock(mutex_);
    auto it = series->counters.find(status);
    if (it != series->counters.end()) {
        return it->second;
    }

    auto* counter = registry()->getCounter("http_requests_total", kRequestsTotalHelp,
        metrics::Labels{{"method", method}, {"path", series->label_path}, {"status", std::to_string(status)}});
    series->counters.emplace(status, counter);
    return counter;
}

size_t HttpMetrics::pathSeriesCount() const
{
    std::shared_lock lock(mutex_);
    return series_.size();
}

HttpRequestScope::HttpRequestScope()
{
    start_us_ = TimeUtil::GetCurrentUs();
    HttpMetrics::GetInstance()->onRequestStart();
}

HttpRequestScope::~HttpRequestScope()
{
    HttpMetrics* metrics = HttpMetrics::GetInstance();
    if (method_.empty() && path_.empty()) {
        // 没记录结果（例如连接在读请求阶段就断开）：只归还在途计数
        metrics->onRequestAborted();
        return;
    }
    metrics->onRequestEnd(method_, path_, status_, TimeUtil::GetCurrentUs() - start_us_);
}

void HttpRequestScope::setOutcome(const std::string& method, const std::string& path, int status)
{
    method_ = method;
    path_ = path;
    status_ = status;
}

} // namespace chen::http
