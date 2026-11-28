/**
 * @file TestHttpMetrics.cpp
 * @brief HTTP 请求指标单测：路径归一化、总量/耗时打点、在途计数与路径基数上限
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @note 指标注册在进程级 Registry 中，用例之间会共享计数器；因此每个用例都用带 pid 的
 *       唯一路径，断言只针对自己那条标签组合。
 */
#include "../TestHarness.hpp"

#include "src/Http/HttpMetrics.hpp"
#include "src/Metrics/Metrics.hpp"

#include <unistd.h>

using chen::http::HttpMetrics;
using chen::http::HttpRequestScope;
using chen::metrics::Labels;
using chen::metrics::RegistryMgr;

namespace {

/// 每个用例用独立路径，避免命中别的用例留下的计数器
std::string uniquePath(const std::string& tag)
{
    return "/unit/metrics/" + tag + "_" + std::to_string(::getpid());
}

uint64_t totalValue(const std::string& method, const std::string& path, int status)
{
    auto* counter = RegistryMgr::GetInstance()->getCounter(
        "http_requests_total", "", Labels{{"method", method}, {"path", path}, {"status", std::to_string(status)}});
    CHEN_CHECK(counter != nullptr);
    return counter->value();
}

chen::metrics::Histogram::Snapshot latencySnapshot(const std::string& method, const std::string& path)
{
    auto* histogram = RegistryMgr::GetInstance()->getHistogram(
        "http_request_duration_seconds", "", Labels{{"method", method}, {"path", path}});
    CHEN_CHECK(histogram != nullptr);
    return histogram->snapshot();
}

} // namespace

CHEN_TEST(http_metrics, normalizes_paths)
{
    CHEN_CHECK_EQ(HttpMetrics::normalizePath(""), std::string("/"));
    CHEN_CHECK_EQ(HttpMetrics::normalizePath("/"), std::string("/"));
    CHEN_CHECK_EQ(HttpMetrics::normalizePath("/a"), std::string("/a"));
    CHEN_CHECK_EQ(HttpMetrics::normalizePath("/a/"), std::string("/a"));
    CHEN_CHECK_EQ(HttpMetrics::normalizePath("//a//b//"), std::string("/a/b"));
    CHEN_CHECK_EQ(HttpMetrics::normalizePath("/a/b?x=1"), std::string("/a/b?x=1"));
}

CHEN_TEST(http_metrics, records_total_and_latency)
{
    const std::string path = uniquePath("total");

    {
        HttpRequestScope scope;
        scope.setOutcome("GET", path, 200);
    }

    CHEN_CHECK_EQ(totalValue("GET", path, 200), 1ull);
    CHEN_CHECK_EQ(latencySnapshot("GET", path).count, 1ull);

    {
        HttpRequestScope scope;
        scope.setOutcome("GET", path, 200);
    }
    CHEN_CHECK_EQ(totalValue("GET", path, 200), 2ull);
    CHEN_CHECK_EQ(latencySnapshot("GET", path).count, 2ull);
}

CHEN_TEST(http_metrics, separates_status_codes)
{
    const std::string path = uniquePath("status");

    {
        HttpRequestScope scope;
        scope.setOutcome("GET", path, 200);
    }
    {
        HttpRequestScope scope;
        scope.setOutcome("GET", path, 404);
    }

    CHEN_CHECK_EQ(totalValue("GET", path, 200), 1ull);
    CHEN_CHECK_EQ(totalValue("GET", path, 404), 1ull);
    // 同一 path 的耗时直方图不区分状态码
    CHEN_CHECK_EQ(latencySnapshot("GET", path).count, 2ull);
}

CHEN_TEST(http_metrics, tracks_in_flight_requests)
{
    auto* gauge = RegistryMgr::GetInstance()->getGauge("http_requests_in_flight", "");
    CHEN_CHECK(gauge != nullptr);
    const int64_t before = gauge->value();

    {
        HttpRequestScope scope;
        CHEN_CHECK_EQ(gauge->value(), before + 1);
        scope.setOutcome("GET", uniquePath("in_flight"), 200);
    }
    CHEN_CHECK_EQ(gauge->value(), before);

    // 没有记录结果的请求（读到一半连接断开）也要归还计数
    {
        HttpRequestScope scope;
        CHEN_CHECK_EQ(gauge->value(), before + 1);
    }
    CHEN_CHECK_EQ(gauge->value(), before);
}

CHEN_TEST(http_metrics, falls_back_to_other_beyond_max_paths)
{
    // 独立实例、独立方法和路径前缀，避免与别的用例互相影响
    HttpMetrics metrics(2);
    const std::string leaf = "_" + std::to_string(::getpid());

    metrics.onRequestStart();
    metrics.onRequestEnd("BUDGET", "/unit/budget/a" + leaf, 200, 1000);
    metrics.onRequestStart();
    metrics.onRequestEnd("BUDGET", "/unit/budget/b" + leaf, 200, 1000);
    CHEN_CHECK_EQ(metrics.pathSeriesCount(), 2u);

    // 第三条开始超出上限，归并到 other
    metrics.onRequestStart();
    metrics.onRequestEnd("BUDGET", "/unit/budget/c" + leaf, 200, 1000);
    CHEN_CHECK_EQ(metrics.pathSeriesCount(), 3u);
    CHEN_CHECK_EQ(totalValue("BUDGET", "other", 200), 1ull);
}

CHEN_TEST_MAIN()
