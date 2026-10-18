/**
 * @file test_metrics.cc
 * @brief Metrics 注册表与 Prometheus 渲染测试
 * @author Christins
 * @date 2026-10-17
 * @copyright GPL-3.0
 */
#include "chen/metrics/metrics.h"

#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>

using namespace chen::metrics;

namespace {

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

void TestCounter()
{
    std::cout << "=== TestCounter ===" << std::endl;
    auto* registry = RegistryMgr::GetInstance();
    auto* counter = registry->getCounter("test_metrics_counter_total", "test counter", {{"server", "test"}, {"method", "GET"}});
    assert(counter != nullptr);
    counter->inc();
    counter->inc(4);
    assert(counter->value() == 5);

    // 相同 name + labels 返回同一对象
    auto* same = registry->getCounter("test_metrics_counter_total", "test counter", {{"method", "GET"}, {"server", "test"}});
    assert(same == counter);

    // label 不同则为新对象
    auto* other = registry->getCounter("test_metrics_counter_total", "test counter", {{"server", "test"}, {"method", "POST"}});
    assert(other != nullptr);
    assert(other != counter);
    other->inc();
    assert(other->value() == 1);

    std::string text = registry->RenderPrometheus();
    assert(Contains(text, "# TYPE test_metrics_counter_total counter"));
    // label 按 key 排序输出
    assert(Contains(text, "test_metrics_counter_total{method=\"GET\",server=\"test\"} 5"));
    assert(Contains(text, "test_metrics_counter_total{method=\"POST\",server=\"test\"} 1"));
    std::cout << "PASS" << std::endl;
}

void TestGauge()
{
    std::cout << "=== TestGauge ===" << std::endl;
    auto* registry = RegistryMgr::GetInstance();
    auto* gauge = registry->getGauge("test_metrics_gauge", "test gauge");
    assert(gauge != nullptr);
    gauge->set(10);
    assert(gauge->value() == 10);
    gauge->add(5);
    assert(gauge->value() == 15);
    gauge->dec();
    assert(gauge->value() == 14);

    std::string text = registry->RenderPrometheus();
    assert(Contains(text, "# TYPE test_metrics_gauge gauge"));
    assert(Contains(text, "test_metrics_gauge 14"));
    std::cout << "PASS" << std::endl;
}

void TestGaugeFunc()
{
    std::cout << "=== TestGaugeFunc ===" << std::endl;
    auto* registry = RegistryMgr::GetInstance();
    auto* gauge = registry->getGaugeFunc("test_metrics_gauge_func", "test gauge func", {{"kind", "runtime"}}, []() {
        return (int64_t)42;
    });
    assert(gauge != nullptr);

    std::string text = registry->RenderPrometheus();
    assert(Contains(text, "# TYPE test_metrics_gauge_func gauge"));
    assert(Contains(text, "test_metrics_gauge_func{kind=\"runtime\"} 42"));
    std::cout << "PASS" << std::endl;
}

void TestHistogram()
{
    std::cout << "=== TestHistogram ===" << std::endl;
    auto* registry = RegistryMgr::GetInstance();
    auto* histogram = registry->getHistogram("test_metrics_latency_seconds", "test latency", {{"route", "/a"}}, {0.001, 0.01, 0.1});
    assert(histogram != nullptr);

    histogram->observe(0.0005);
    histogram->observe(0.05);
    // 超过所有桶，只计入 +Inf
    histogram->observe(10.0);

    std::string text = registry->RenderPrometheus();
    assert(Contains(text, "# TYPE test_metrics_latency_seconds histogram"));
    // 内部只记录命中桶，渲染时输出累计值
    assert(Contains(text, "test_metrics_latency_seconds_bucket{route=\"/a\",le=\"0.001\"} 1"));
    assert(Contains(text, "test_metrics_latency_seconds_bucket{route=\"/a\",le=\"0.01\"} 1"));
    assert(Contains(text, "test_metrics_latency_seconds_bucket{route=\"/a\",le=\"0.1\"} 2"));
    assert(Contains(text, "test_metrics_latency_seconds_bucket{route=\"/a\",le=\"+Inf\"} 3"));
    assert(Contains(text, "test_metrics_latency_seconds_sum{route=\"/a\"} 10.0505"));
    assert(Contains(text, "test_metrics_latency_seconds_count{route=\"/a\"} 3"));
    std::cout << "PASS" << std::endl;
}

void TestLabelsEscape()
{
    std::cout << "=== TestLabelsEscape ===" << std::endl;
    auto* registry = RegistryMgr::GetInstance();
    auto* counter = registry->getCounter("test.metrics-bad-name", "help with \\ and \n newline", {{"path", "/a\"b\\c\nd"}});
    assert(counter != nullptr);
    counter->inc();

    std::string text = registry->RenderPrometheus();
    // 非法指标名替换为 _
    assert(Contains(text, "# TYPE test_metrics_bad_name counter"));
    assert(Contains(text, "# HELP test_metrics_bad_name help with \\\\ and \\n newline"));
    assert(Contains(text, "test_metrics_bad_name{path=\"/a\\\"b\\\\c\\nd\"} 1"));
    std::cout << "PASS" << std::endl;
}

void TestConcurrentCounter()
{
    std::cout << "=== TestConcurrentCounter ===" << std::endl;
    auto* counter = RegistryMgr::GetInstance()->getCounter("test_metrics_concurrent_total", "concurrent counter");
    assert(counter != nullptr);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([counter]() {
            for (int j = 0; j < kPerThread; ++j) {
                counter->inc();
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    assert(counter->value() == kThreads * kPerThread);
    std::cout << "PASS" << std::endl;
}

void TestConcurrentHistogram()
{
    std::cout << "=== TestConcurrentHistogram ===" << std::endl;
    auto* histogram = RegistryMgr::GetInstance()->getHistogram(
        "test_metrics_concurrent_latency_seconds", "concurrent histogram", {{"route", "/concurrent"}}, {0.001, 0.01, 0.1});
    assert(histogram != nullptr);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([histogram]() {
            for (int j = 0; j < kPerThread; ++j) {
                histogram->observe(0.0005);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    std::string text = RegistryMgr::GetInstance()->RenderPrometheus();
    assert(Contains(text, "test_metrics_concurrent_latency_seconds_bucket{route=\"/concurrent\",le=\"0.001\"} 4000"));
    assert(Contains(text, "test_metrics_concurrent_latency_seconds_bucket{route=\"/concurrent\",le=\"+Inf\"} 4000"));
    assert(Contains(text, "test_metrics_concurrent_latency_seconds_count{route=\"/concurrent\"} 4000"));
    std::cout << "PASS" << std::endl;
}

void TestTypeMismatch()
{
    std::cout << "=== TestTypeMismatch ===" << std::endl;
    auto* registry = RegistryMgr::GetInstance();
    auto* counter = registry->getCounter("test_metrics_type_mismatch_total", "counter");
    assert(counter != nullptr);
    // 同名同 label 但类型不同，应返回 nullptr
    auto* gauge = registry->getGauge("test_metrics_type_mismatch_total", "gauge");
    assert(gauge == nullptr);
    std::cout << "PASS" << std::endl;
}

void TestRenderFormats()
{
    std::cout << "=== TestRenderFormats ===" << std::endl;
    auto* registry = RegistryMgr::GetInstance();

    std::string openmetrics = registry->RenderOpenMetrics();
    assert(Contains(openmetrics, "# TYPE test_metrics_counter_total counter"));
    assert(openmetrics.size() >= 6);
    assert(openmetrics.compare(openmetrics.size() - 6, 6, "# EOF\n") == 0);

    std::string json_text = registry->RenderJson();
    Json::Value root;
    Json::Reader reader;
    assert(reader.parse(json_text, root));
    assert(root.isObject());
    assert(root["metrics"].isArray());

    bool found_counter = false;
    bool found_histogram = false;
    for (auto& item : root["metrics"]) {
        const std::string name = item["name"].asString();
        if (name == "test_metrics_counter_total") {
            found_counter = true;
            assert(item["type"].asString() == "counter");
            assert(item["labels"].isObject());
        } else if (name == "test_metrics_latency_seconds") {
            found_histogram = true;
            assert(item["type"].asString() == "histogram");
            assert(item["buckets"].isArray());
            assert(item["count"].asUInt64() == 3);
            assert(item["sum"].asDouble() > 10.0);
        }
    }
    assert(found_counter);
    assert(found_histogram);

    assert(registry->Render(RenderFormat::PROMETHEUS) == registry->RenderPrometheus());
    assert(registry->Render(RenderFormat::OPENMETRICS) == openmetrics);
    assert(registry->Render(RenderFormat::JSON) == json_text);
    std::cout << "PASS" << std::endl;
}

} // namespace

int main()
{
    TestCounter();
    TestGauge();
    TestGaugeFunc();
    TestHistogram();
    TestLabelsEscape();
    TestConcurrentCounter();
    TestConcurrentHistogram();
    TestTypeMismatch();
    TestRenderFormats();
    std::cout << "All metrics tests passed" << std::endl;
    return 0;
}
