#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
#include <thread>

#include "../util/type_util.h"

namespace chen::metrics {

namespace {

/**
 * @brief 分片数量：不小于 16，按 CPU 数向上取 2 的幂，最多 64
 * @details 线程数超过分片数时会有不同线程共享同一分片，只影响争用，不影响正确性
 */
size_t ShardCount()
{
    static const size_t count = [] {
        unsigned int hardware = std::thread::hardware_concurrency();
        size_t value = 16;
        while (value < hardware && value < 64) {
            value <<= 1;
        }
        return value;
    }();
    return count;
}

/**
 * @brief 获取当前线程的分片下标
 * @details 每个线程首次调用时分配一个分片；线程退出后其分片数据保留到进程结束
 */
uint32_t GetShardIndex()
{
    static std::atomic<uint32_t> next{0};
    static thread_local uint32_t index = next.fetch_add(1, std::memory_order_relaxed) % ShardCount();
    return index;
}

bool IsNameStart(char c, bool allow_colon)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (allow_colon && c == ':');
}

bool IsNameChar(char c, bool allow_colon)
{
    return IsNameStart(c, allow_colon) || (c >= '0' && c <= '9');
}

std::string SanitizeName(const std::string& name, bool allow_colon)
{
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        bool valid = (i == 0) ? IsNameStart(c, allow_colon) : IsNameChar(c, allow_colon);
        out.push_back(valid ? c : '_');
    }
    if (out.empty()) {
        out = allow_colon ? "chen_metric" : "label";
    }
    return out;
}

std::string SanitizeMetricName(const std::string& name)
{
    return SanitizeName(name, true);
}

std::string SanitizeLabelName(const std::string& name)
{
    return SanitizeName(name, false);
}

std::string EscapeLabelValue(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string EscapeHelp(const std::string& help)
{
    std::string out;
    out.reserve(help.size());
    for (char c : help) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

void WarnTypeMismatch(const std::string& name)
{
    // metrics 是 log 的下层依赖，不能使用日志宏，直接输出到 stderr
    std::fprintf(stderr, "metrics: name=%s already registered with another type\n", name.c_str());
}

std::string BuildLabelString(const Labels& labels)
{
    if (labels.empty()) {
        return "";
    }

    std::string out = "{";
    bool first = true;
    for (auto& [key, value] : labels) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += SanitizeLabelName(key);
        out += "=\"";
        out += EscapeLabelValue(value);
        out += "\"";
    }
    out += "}";
    return out;
}

Labels NormalizeLabels(const Labels& labels)
{
    Labels sorted = labels;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    Labels dedup;
    dedup.reserve(sorted.size());
    for (auto& label : sorted) {
        if (!dedup.empty() && dedup.back().first == label.first) {
            dedup.back().second = label.second;
        } else {
            dedup.push_back(label);
        }
    }
    return dedup;
}

} // namespace

Metric::Metric(const std::string& name, const std::string& help, const Labels& labels)
    : m_name(name)
    , m_help(help)
    , m_labels(NormalizeLabels(labels))
{
    m_sanitizedName = SanitizeMetricName(m_name);
    m_labelString = BuildLabelString(m_labels);
}

std::string Metric::formatLabels(const std::string& extra_key, const std::string& extra_value) const
{
    if (extra_key.empty()) {
        return m_labelString;
    }

    std::string out;
    if (m_labelString.empty()) {
        out = "{";
    } else {
        out = m_labelString;
        out.pop_back(); // 去掉结尾的 '}'
        out += ",";
    }
    out += SanitizeLabelName(extra_key);
    out += "=\"";
    out += EscapeLabelValue(extra_value);
    out += "\"}";
    return out;
}

Counter::Counter(const std::string& name, const std::string& help, const Labels& labels)
    : Metric(name, help, labels)
    , m_shards(new Shard[ShardCount()])
{
}

void Counter::inc(uint64_t v)
{
    m_shards[GetShardIndex()].value.fetch_add(v, std::memory_order_relaxed);
}

uint64_t Counter::value() const
{
    uint64_t total = 0;
    size_t shard_count = ShardCount();
    for (size_t i = 0; i < shard_count; ++i) {
        total += m_shards[i].value.load(std::memory_order_relaxed);
    }
    return total;
}

std::string Counter::render() const
{
    return m_sanitizedName + formatLabels() + " " + TypeUtil::ToString(value()) + "\n";
}

Gauge::Gauge(const std::string& name, const std::string& help, const Labels& labels)
    : Metric(name, help, labels)
{
}

void Gauge::set(int64_t v)
{
    m_value.store(v, std::memory_order_relaxed);
}

void Gauge::add(int64_t v)
{
    m_value.fetch_add(v, std::memory_order_relaxed);
}

void Gauge::inc()
{
    add(1);
}

void Gauge::dec()
{
    add(-1);
}

int64_t Gauge::value() const
{
    return m_value.load(std::memory_order_relaxed);
}

std::string Gauge::render() const
{
    return m_sanitizedName + formatLabels() + " " + TypeUtil::ToString(value()) + "\n";
}

GaugeFunc::GaugeFunc(const std::string& name, const std::string& help, const Labels& labels, Callback cb)
    : Metric(name, help, labels)
    , m_cb(std::move(cb))
{
}

std::string GaugeFunc::render() const
{
    int64_t value = m_cb ? m_cb() : 0;
    return m_sanitizedName + formatLabels() + " " + TypeUtil::ToString(value) + "\n";
}

Histogram::Histogram(const std::string& name, const std::string& help, const Labels& labels, const std::vector<double>& buckets)
    : Metric(name, help, labels)
    , m_buckets(buckets.empty() ? DefaultBuckets() : buckets)
{
    std::sort(m_buckets.begin(), m_buckets.end());
    m_buckets.erase(std::unique(m_buckets.begin(), m_buckets.end()), m_buckets.end());

    m_stride = ((m_buckets.size() + 2 + 7) / 8) * 8;

    size_t total = m_stride * ShardCount();
    m_shardData = static_cast<std::atomic<uint64_t>*>(std::aligned_alloc(64, total * sizeof(std::atomic<uint64_t>)));

    if (!m_shardData) {
        throw std::bad_alloc();
    }
    for (size_t i = 0; i < total; ++i) {
        new (&m_shardData[i]) std::atomic<uint64_t>(0);
    }
}

Histogram::~Histogram()
{
    if (!m_shardData) {
        return;
    }
    using Atomic = std::atomic<uint64_t>;
    size_t total = m_stride * ShardCount();
    for (size_t i = 0; i < total; ++i) {
        m_shardData[i].~Atomic();
    }
    std::free(m_shardData);
}

void Histogram::observe(double v)
{
    if (std::isnan(v) || v < 0) {
        v = 0;
    }
    size_t count_index = m_buckets.size();
    auto* shard = m_shardData + static_cast<size_t>(GetShardIndex()) * m_stride;

    // 只递增命中的第一个桶；未命中任何桶时归入 +Inf（仅累计 count/sum）
    auto it = std::lower_bound(m_buckets.begin(), m_buckets.end(), v);
    if (it != m_buckets.end()) {
        size_t index = static_cast<size_t>(it - m_buckets.begin());
        shard[index].fetch_add(1, std::memory_order_relaxed);
    }
    shard[count_index].fetch_add(1, std::memory_order_relaxed);
    // 以纳秒整数累加，避免依赖浮点原子操作
    shard[count_index + 1].fetch_add(static_cast<uint64_t>(v * 1e9), std::memory_order_relaxed);
}

std::string Histogram::render() const
{
    const std::string& name = m_sanitizedName;
    std::string out;
    out.reserve((m_buckets.size() + 3) * 80);

    size_t shard_count = ShardCount();
    size_t count_index = m_buckets.size();

    uint64_t cumulative = 0;
    for (size_t i = 0; i < m_buckets.size(); ++i) {
        uint64_t bucket_total = 0;
        for (size_t s = 0; s < shard_count; ++s) {
            const auto* shard = m_shardData + s * m_stride;
            bucket_total += shard[i].load(std::memory_order_relaxed);
        }
        cumulative += bucket_total;
        out += name + "_bucket";
        out += formatLabels("le", TypeUtil::ToString(m_buckets[i]));
        out += " ";
        out += TypeUtil::ToString(cumulative);
        out += "\n";
    }

    uint64_t total = 0;
    uint64_t sum_ns = 0;
    for (size_t s = 0; s < shard_count; ++s) {
        const auto* shard = m_shardData + s * m_stride;
        total += shard[count_index].load(std::memory_order_relaxed);
        sum_ns += shard[count_index + 1].load(std::memory_order_relaxed);
    }

    out += name + "_bucket";
    out += formatLabels("le", "+Inf");
    out += " ";
    out += TypeUtil::ToString(total);
    out += "\n";

    out += name + "_sum";
    out += formatLabels();
    out += " ";
    out += TypeUtil::ToString(static_cast<double>(sum_ns) / 1e9);
    out += "\n";

    out += name + "_count";
    out += formatLabels();
    out += " ";
    out += TypeUtil::ToString(total);
    out += "\n";

    return out;
}

const std::vector<double>& Histogram::DefaultBuckets()
{
    static const std::vector<double> buckets = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0};
    return buckets;
}

std::string Registry::makeKey(const std::string& name, const Labels& labels) const
{
    std::string key = name;
    for (auto& [label_key, label_value] : NormalizeLabels(labels)) {
        key.push_back('\x1f');
        key += label_key;
        key.push_back('\x1e');
        key += label_value;
    }
    return key;
}

Counter* Registry::getCounter(const std::string& name, const std::string& help, const Labels& labels)
{
    std::lock_guard lock(m_mutex);
    auto key = makeKey(name, labels);
    auto it = m_metrics.find(key);
    if (it != m_metrics.end()) {
        auto* metric = dynamic_cast<Counter*>(it->second.get());
        if (!metric) {
            WarnTypeMismatch(name);
        }
        return metric;
    }
    auto metric = std::make_shared<Counter>(name, help, labels);
    auto* result = metric.get();
    m_metrics.emplace(std::move(key), std::move(metric));
    return result;
}

Gauge* Registry::getGauge(const std::string& name, const std::string& help, const Labels& labels)
{
    std::lock_guard lock(m_mutex);
    auto key = makeKey(name, labels);
    auto it = m_metrics.find(key);
    if (it != m_metrics.end()) {
        auto* metric = dynamic_cast<Gauge*>(it->second.get());
        if (!metric) {
            WarnTypeMismatch(name);
        }
        return metric;
    }
    auto metric = std::make_shared<Gauge>(name, help, labels);
    auto* result = metric.get();
    m_metrics.emplace(std::move(key), std::move(metric));
    return result;
}

GaugeFunc* Registry::getGaugeFunc(const std::string& name, const std::string& help, const Labels& labels, GaugeFunc::Callback cb)
{
    std::lock_guard lock(m_mutex);
    auto key = makeKey(name, labels);
    auto it = m_metrics.find(key);
    if (it != m_metrics.end()) {
        auto* metric = dynamic_cast<GaugeFunc*>(it->second.get());
        if (!metric) {
            WarnTypeMismatch(name);
        }
        return metric;
    }
    auto metric = std::make_shared<GaugeFunc>(name, help, labels, std::move(cb));
    auto* result = metric.get();
    m_metrics.emplace(std::move(key), std::move(metric));
    return result;
}

Histogram* Registry::getHistogram(const std::string& name, const std::string& help, const Labels& labels, const std::vector<double>& buckets)
{
    std::lock_guard lock(m_mutex);
    auto key = makeKey(name, labels);
    auto it = m_metrics.find(key);
    if (it != m_metrics.end()) {
        auto* metric = dynamic_cast<Histogram*>(it->second.get());
        if (!metric) {
            WarnTypeMismatch(name);
        }
        return metric;
    }
    auto metric = std::make_shared<Histogram>(name, help, labels, buckets);
    auto* result = metric.get();
    m_metrics.emplace(std::move(key), std::move(metric));
    return result;
}

std::string Registry::RenderPrometheus()
{
    std::vector<Metric*> metrics;
    {
        std::lock_guard lock(m_mutex);
        metrics.reserve(m_metrics.size());
        for (auto& [key, metric] : m_metrics) {
            metrics.push_back(metric.get());
        }
    }

    std::map<std::string, std::vector<Metric*>> groups;
    for (auto* metric : metrics) {
        groups[metric->getName()].push_back(metric);
    }

    std::string out;
    out.reserve(metrics.size() * 128 + 64);
    for (auto& [name, list] : groups) {
        std::string metric_name = SanitizeMetricName(name);
        out += "# HELP " + metric_name + " " + EscapeHelp(list.front()->getHelp()) + "\n";
        out += "# TYPE " + metric_name + " " + list.front()->getType() + "\n";
        for (auto* metric : list) {
            out += metric->render();
        }
    }
    return out;
}

} // namespace chen::metrics
