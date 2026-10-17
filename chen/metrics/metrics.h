/**
 * @file metrics.h
 * @brief 指标注册表与 Prometheus 文本渲染
 * @author Christins
 * @date 2026-10-17
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../util/noncopyable.h"
#include "../util/singleton.h"

namespace chen::metrics {

/// 指标标签集合（key-value），注册时按 key 排序
using Labels = std::vector<std::pair<std::string, std::string>>;

/**
 * @brief 指标基类
 */
class Metric : public Noncopyable {
public:
    typedef std::shared_ptr<Metric> ptr;

    Metric(const std::string& name, const std::string& help, const Labels& labels);

    virtual ~Metric() {}

    const std::string& getName() const { return m_name; }

    const std::string& getHelp() const { return m_help; }

    const Labels& getLabels() const { return m_labels; }

    /// Prometheus 类型名：counter / gauge / histogram
    virtual const char* getType() const = 0;

    /// 渲染样本行（不含 HELP/TYPE）
    virtual std::string render() const = 0;

protected:
    /**
     * @brief 格式化标签
     * @param extra_key 额外追加的标签名（为空则不追加）
     * @param extra_value 额外追加的标签值
     * @return 形如 `{k="v"}` 的字符串，无标签时返回空串
     */
    std::string formatLabels(const std::string& extra_key = "", const std::string& extra_value = "") const;

    std::string m_name;
    std::string m_help;
    Labels m_labels;
    /// 预计算的 Prometheus 指标名（非法字符已替换）
    std::string m_sanitizedName;
    /// 预计算的 label 串，形如 `{k="v"}`，无 label 时为空
    std::string m_labelString;
};

/**
 * @brief 单调递增计数器
 */
class Counter : public Metric {
public:
    Counter(const std::string& name, const std::string& help, const Labels& labels);

    void inc(uint64_t v = 1);

    uint64_t value() const;

    const char* getType() const override { return "counter"; }

    std::string render() const override;

private:
    /// 每线程分片，避免同一计数器跨核 cacheline 争用
    struct alignas(64) Shard {
        std::atomic<uint64_t> value{0};
    };
    std::unique_ptr<Shard[]> m_shards;
};

/**
 * @brief 可增可减的瞬时值
 */
class Gauge : public Metric {
public:
    Gauge(const std::string& name, const std::string& help, const Labels& labels);

    void set(int64_t v);

    void add(int64_t v);

    void inc();

    void dec();

    int64_t value() const;

    const char* getType() const override { return "gauge"; }

    std::string render() const override;

private:
    std::atomic<int64_t> m_value{0};
};

/**
 * @brief 采集时回调取值的 Gauge
 * @details 用于运行时统计（fiber 数、调度队列长度、fd 数等），
 *          避免在热路径上额外维护计数。回调在 /metrics 渲染时执行。
 */
class GaugeFunc : public Metric {
public:
    typedef std::function<int64_t()> Callback;

    GaugeFunc(const std::string& name, const std::string& help, const Labels& labels, Callback cb);

    const char* getType() const override { return "gauge"; }

    std::string render() const override;

private:
    Callback m_cb;
};

/**
 * @brief 固定桶直方图
 */
class Histogram : public Metric {
public:
    Histogram(const std::string& name, const std::string& help, const Labels& labels, const std::vector<double>& buckets);
    ~Histogram() override;

    void observe(double v);

    const char* getType() const override { return "histogram"; }

    std::string render() const override;

    static const std::vector<double>& DefaultBuckets();

private:
    std::vector<double> m_buckets;
    /// 每线程分片：每片 [bucket_counts..., count, sum_ns, padding]
    std::atomic<uint64_t>* m_shardData = nullptr;
    /// 每片占用的元素数（按 64 字节对齐）
    size_t m_stride = 0;
};

/**
 * @brief 指标注册表
 * @details 进程级单例，线程安全。指标对象注册后生命周期与进程一致。
 */
class Registry {
public:
    Registry() = default;

    Counter* getCounter(const std::string& name, const std::string& help, const Labels& labels = {});

    Gauge* getGauge(const std::string& name, const std::string& help, const Labels& labels = {});

    GaugeFunc* getGaugeFunc(const std::string& name, const std::string& help, const Labels& labels, GaugeFunc::Callback cb);

    Histogram* getHistogram(const std::string& name, const std::string& help, const Labels& labels = {}, const std::vector<double>& buckets = {});

    std::string RenderPrometheus();

private:
    std::string makeKey(const std::string& name, const Labels& labels) const;

private:
    /// 注册表锁（临界区含 map 操作与内存分配，不适合 SpinLock）
    std::mutex m_mutex;
    /// key -> metric，key 由 name + 排序后的 label 组成
    std::map<std::string, Metric::ptr> m_metrics;
};

/// 指标注册表单例
typedef Singleton<Registry> RegistryMgr;

} // namespace chen::metrics
