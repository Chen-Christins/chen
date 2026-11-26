/**
 * @file HttpMetrics.hpp
 * @brief HTTP 请求指标：请求总量、处理耗时与在途请求数
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#pragma once

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "../Metrics/Metrics.hpp"
#include "../Util/Noncopyable.hpp"

namespace chen::http {

/**
 * @brief HTTP 请求指标聚合器
 * @details 暴露三个指标：
 *          - `http_requests_total{method,path,status}`：请求总数
 *          - `http_request_duration_seconds{method,path}`：dispatch + 写回响应的耗时直方图
 *          - `http_requests_in_flight`：正在处理的请求数（gauge）
 *
 *          path 取自请求行（不含 query），由 normalizePath() 归一化；不同路径数超过
 *          `http.metrics.max_paths` 后的新路径统一记为 `other`，避免 404 扫描把指标基数打爆。
 *          指标注册在进程级 Registry 中，指标名固定、标签组合按需创建并缓存。
 */
class HttpMetrics : public Noncopyable {
public:
    typedef std::shared_ptr<HttpMetrics> ptr;

    /**
     * @brief 构造聚合器
     * @param max_paths 不同 (method, path) 组合的上限；0 表示读取 `http.metrics.max_paths`
     */
    explicit HttpMetrics(size_t max_paths = 0);

    /// 进程级实例（延迟创建）
    static HttpMetrics* GetInstance();

    /// 请求开始：在途 +1
    void onRequestStart();

    /**
     * @brief 请求结束：在途 -1，并记录总量与耗时
     * @param method HTTP 方法（空字符串记为 UNKNOWN）
     * @param path 请求路径（会做归一化与基数上限处理）
     * @param status HTTP 状态码
     * @param duration_us 从开始处理到响应写回的耗时（微秒）
     */
    void onRequestEnd(const std::string& method, const std::string& path, int status, uint64_t duration_us);

    /// 请求没有产生响应（连接在读请求阶段断开）：只归还在途计数
    void onRequestAborted();

    /**
     * @brief 归一化请求路径
     * @details 空路径记为 `/`；合并重复的 `/`；去掉末尾多余的 `/`（根路径除外）。
     */
    static std::string normalizePath(const std::string& path);

    /// 已缓存的不同 (method, path) 组合数（测试/排查用）
    size_t pathSeriesCount() const;

    /// 不同 (method, path) 组合的上限
    size_t maxPaths() const { return max_paths_; }

private:
    struct Series {
        /// 实际用于打标签的路径（可能因基数上限被归并为 other）
        std::string label_path;
        /// 耗时直方图，标签为 method + label_path
        metrics::Histogram* latency = nullptr;
        /// status -> 请求数计数器
        std::unordered_map<int, metrics::Counter*> counters;
    };

    /// 取（或创建）method + path 对应的 series
    Series* getSeries(const std::string& method, const std::string& path);

    /// 取（或创建）某个 series 下对应状态码的计数器
    metrics::Counter* getCounter(Series* series, const std::string& method, int status);

    mutable std::shared_mutex mutex_;
    /// (method, path) -> series；用 unique_ptr 保证 rehash 后 Series* 依然稳定
    std::unordered_map<std::string, std::unique_ptr<Series>> series_;
    /// 在途请求数
    metrics::Gauge* in_flight_ = nullptr;
    /// 不同路径组合上限
    size_t max_paths_ = 512;
};

/**
 * @brief 请求级 RAII 作用域
 * @details 构造时在途 +1 并记下起始时间；析构时按 setOutcome() 提供的方法/路径/状态码
 *          记录总量与耗时，并把在途 -1。没有调用 setOutcome() 时不记录请求，只归还计数。
 */
class HttpRequestScope : public Noncopyable {
public:
    HttpRequestScope();

    ~HttpRequestScope();

    /// 记录本次请求的方法、路径与最终状态码
    void setOutcome(const std::string& method, const std::string& path, int status);

private:
    uint64_t start_us_ = 0;
    std::string method_;
    std::string path_;
    int status_ = 0;
};

} // namespace chen::http
