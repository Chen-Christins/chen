/**
 * @file TestMetrics.cpp
 * @brief Metrics 注册表与 Prometheus / OpenMetrics / JSON 渲染单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @note 指标注册表是进程级单例，用例内使用唯一指标名，避免相互影响。
 */
#include "../TestHarness.hpp"

#include "src/Metrics/Metrics.hpp"

#include <json/json.h>

#include <string>
#include <thread>
#include <vector>

using namespace chen::metrics;

namespace {

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace

CHEN_TEST(metrics, counter)
{
    auto* registry = RegistryMgr::GetInstance();
    auto* counter =
        registry->getCounter("unit_metrics_counter_total", "unit counter", {{"server", "test"}, {"method", "GET"}});
    CHEN_CHECK(counter != nullptr);
    counter->inc();
    counter->inc(4);
    CHEN_CHECK_EQ(counter->value(), static_cast<uint64_t>(5));

    // 相同 name + labels（顺序无关）返回同一对象
    auto* same =
        registry->getCounter("unit_metrics_counter_total", "unit counter", {{"method", "GET"}, {"server", "test"}});
    CHEN_CHECK(same == counter);

    // label 不同则是新对象
    auto* other =
        registry->getCounter("unit_metrics_counter_total", "unit counter", {{"server", "test"}, {"method", "POST"}});
    CHEN_CHECK(other != nullptr);
    CHEN_CHECK(other != counter);
    other->inc();
    CHEN_CHECK_EQ(other->value(), static_cast<uint64_t>(1));

    const std::string text = registry->RenderPrometheus();
    CHEN_CHECK(Contains(text, "# TYPE unit_metrics_counter_total counter"));
    CHEN_CHECK(Contains(text, "unit_metrics_counter_total{method=\"GET\",server=\"test\"} 5"));
    CHEN_CHECK(Contains(text, "unit_metrics_counter_total{method=\"POST\",server=\"test\"} 1"));
}

CHEN_TEST(metrics, gauge)
{
    auto* registry = RegistryMgr::GetInstance();
    auto* gauge = registry->getGauge("unit_metrics_gauge", "unit gauge");
    CHEN_CHECK(gauge != nullptr);
    gauge->set(10);
    CHEN_CHECK_EQ(gauge->value(), 10);
    gauge->add(5);
    CHEN_CHECK_EQ(gauge->value(), 15);
    gauge->dec();
    CHEN_CHECK_EQ(gauge->value(), 14);

    const std::string text = registry->RenderPrometheus();
    CHEN_CHECK(Contains(text, "# TYPE unit_metrics_gauge gauge"));
    CHEN_CHECK(Contains(text, "unit_metrics_gauge 14"));
}

CHEN_TEST(metrics, gauge_func)
{
    auto* registry = RegistryMgr::GetInstance();
    auto* gauge = registry->getGaugeFunc("unit_metrics_gauge_func", "unit gauge func", {{"kind", "runtime"}}, []() {
        return static_cast<int64_t>(42);
    });
    CHEN_CHECK(gauge != nullptr);

    const std::string text = registry->RenderPrometheus();
    CHEN_CHECK(Contains(text, "unit_metrics_gauge_func{kind=\"runtime\"} 42"));
}

CHEN_TEST(metrics, histogram_cumulative)
{
    auto* registry = RegistryMgr::GetInstance();
    auto* histogram =
        registry->getHistogram("unit_metrics_latency_seconds", "unit latency", {{"route", "/a"}}, {0.001, 0.01, 0.1});
    CHEN_CHECK(histogram != nullptr);

    histogram->observe(0.0005);
    histogram->observe(0.05);
    histogram->observe(10.0); // 超过所有桶，只计入 +Inf

    const std::string text = registry->RenderPrometheus();
    CHEN_CHECK(Contains(text, "# TYPE unit_metrics_latency_seconds histogram"));
    // 内部只记录命中桶，渲染时输出累计值
    CHEN_CHECK(Contains(text, "unit_metrics_latency_seconds_bucket{route=\"/a\",le=\"0.001\"} 1"));
    CHEN_CHECK(Contains(text, "unit_metrics_latency_seconds_bucket{route=\"/a\",le=\"0.01\"} 1"));
    CHEN_CHECK(Contains(text, "unit_metrics_latency_seconds_bucket{route=\"/a\",le=\"0.1\"} 2"));
    CHEN_CHECK(Contains(text, "unit_metrics_latency_seconds_bucket{route=\"/a\",le=\"+Inf\"} 3"));
    CHEN_CHECK(Contains(text, "unit_metrics_latency_seconds_sum{route=\"/a\"} 10.0505"));
    CHEN_CHECK(Contains(text, "unit_metrics_latency_seconds_count{route=\"/a\"} 3"));
}

CHEN_TEST(metrics, labels_are_escaped)
{
    auto* registry = RegistryMgr::GetInstance();
    auto* counter =
        registry->getCounter("unit.metrics-bad-name", "help with \\ and \n newline", {{"path", "/a\"b\\c\nd"}});
    CHEN_CHECK(counter != nullptr);
    counter->inc();

    const std::string text = registry->RenderPrometheus();
    // 非法指标名替换为 _
    CHEN_CHECK(Contains(text, "# TYPE unit_metrics_bad_name counter"));
    CHEN_CHECK(Contains(text, "# HELP unit_metrics_bad_name help with \\\\ and \\n newline"));
    CHEN_CHECK(Contains(text, "unit_metrics_bad_name{path=\"/a\\\"b\\\\c\\nd\"} 1"));
}

CHEN_TEST(metrics, type_mismatch_returns_null)
{
    auto* registry = RegistryMgr::GetInstance();
    CHEN_CHECK(registry->getCounter("unit_metrics_type_mismatch_total", "counter") != nullptr);
    // 同名同 label 但类型不同
    CHEN_CHECK(registry->getGauge("unit_metrics_type_mismatch_total", "gauge") == nullptr);
}

CHEN_TEST(metrics, concurrent_counter)
{
    auto* counter = RegistryMgr::GetInstance()->getCounter("unit_metrics_concurrent_total", "concurrent counter");
    CHEN_CHECK(counter != nullptr);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    std::vector<std::thread> threads;
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
    CHEN_CHECK_EQ(counter->value(), static_cast<uint64_t>(kThreads * kPerThread));
}

CHEN_TEST(metrics, render_formats)
{
    auto* registry = RegistryMgr::GetInstance();

    // 自给自足：不依赖其他用例的注册结果
    registry->getCounter("unit_metrics_render_total", "render counter", {{"job", "unit"}})->inc(2);
    registry->getHistogram("unit_metrics_render_seconds", "render histogram", {{"job", "unit"}}, {0.1})->observe(0.05);

    const std::string openmetrics = registry->RenderOpenMetrics();
    CHEN_CHECK(Contains(openmetrics, "# TYPE unit_metrics_render_total counter"));
    CHEN_CHECK_EQ(openmetrics.substr(openmetrics.size() - 6), std::string("# EOF\n"));

    const std::string json_text = registry->RenderJson();
    Json::Value root;
    Json::Reader reader;
    CHEN_CHECK(reader.parse(json_text, root));
    CHEN_CHECK(root.isObject());
    CHEN_CHECK(root["metrics"].isArray());

    bool found_counter = false;
    bool found_histogram = false;
    for (const auto& item : root["metrics"]) {
        const std::string name = item["name"].asString();
        if (name == "unit_metrics_render_total") {
            found_counter = true;
            CHEN_CHECK_EQ(item["type"].asString(), std::string("counter"));
            CHEN_CHECK(item["labels"].isObject());
            CHEN_CHECK_EQ(item["value"].asInt64(), static_cast<Json::Int64>(2));
        } else if (name == "unit_metrics_render_seconds") {
            found_histogram = true;
            CHEN_CHECK(item["buckets"].isArray());
            CHEN_CHECK_EQ(item["count"].asUInt64(), static_cast<Json::UInt64>(1));
        }
    }
    CHEN_CHECK(found_counter);
    CHEN_CHECK(found_histogram);

    CHEN_CHECK(registry->Render(RenderFormat::PROMETHEUS) == registry->RenderPrometheus());
    CHEN_CHECK(registry->Render(RenderFormat::OPENMETRICS) == openmetrics);
    CHEN_CHECK(registry->Render(RenderFormat::JSON) == json_text);
}

CHEN_TEST_MAIN()
