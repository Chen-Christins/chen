/**
 * @file test_metrics_bench.cc
 * @brief Metrics 热路径微基准：Counter / Histogram / Registry 查找
 * @author Christins
 * @date 2026-10-17
 * @copyright GPL-3.0
 *
 * 用法：
 *   tests/bin/test_metrics_bench [ops_per_thread] [threads]
 *
 * 建议用 Release 构建运行，Debug（-O0）数据没有参考价值。
 */
#include "chen/metrics/metrics.h"
#include "chen/util/util.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace chen::metrics;

namespace {

using Clock = std::chrono::steady_clock;

void PrintResult(const std::string& name, uint64_t ops, Clock::duration elapsed) {
    double ns = std::chrono::duration<double, std::nano>(elapsed).count();
    double ms = ns / 1e6;
    double seconds = ns / 1e9;
    double mops = seconds > 0 ? (static_cast<double>(ops) / seconds / 1e6) : 0;

    std::cout << "[" << name << "]"
              << " ops=" << ops
              << " elapsed_ms=" << std::fixed << std::setprecision(2) << ms
              << " throughput=" << std::setprecision(2) << mops << " Mops/s"
              << " ns/op=" << std::setprecision(2) << (ops > 0 ? ns / ops : 0)
              << std::endl;
}

template <class Fn>
Clock::duration RunThreads(int threads, uint64_t ops_per_thread, Fn&& fn) {
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(threads);

    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([&, i]() {
            (void)i;
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            fn(ops_per_thread);
        });
    }

    while (ready.load(std::memory_order_acquire) < threads) {
        std::this_thread::yield();
    }

    auto begin = Clock::now();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }
    return Clock::now() - begin;
}

void RunCounterBench(int threads, uint64_t ops_per_thread) {
    auto* counter = RegistryMgr::GetInstance()->getCounter(
        "test_metrics_bench_counter_total", "bench counter",
        {{"case", threads > 1 ? "multi" : "single"}});

    auto elapsed = RunThreads(threads, ops_per_thread, [counter](uint64_t ops) {
        for (uint64_t i = 0; i < ops; ++i) {
            counter->inc();
        }
    });

    // 防止编译器把结果优化掉
    volatile uint64_t sink = counter->value();
    (void)sink;

    std::string name = threads > 1 ? "counter multi" : "counter single";
    PrintResult(name, static_cast<uint64_t>(threads) * ops_per_thread, elapsed);
}

void RunHistogramBench(int threads, uint64_t ops_per_thread) {
    auto* histogram = RegistryMgr::GetInstance()->getHistogram(
        "test_metrics_bench_histogram_seconds", "bench histogram",
        {{"case", threads > 1 ? "multi" : "single"}},
        {0.001, 0.01, 0.1});

    // 固定命中第一个桶，模拟同一指标的热点争用
    auto elapsed = RunThreads(threads, ops_per_thread, [histogram](uint64_t ops) {
        for (uint64_t i = 0; i < ops; ++i) {
            histogram->observe(0.0005);
        }
    });

    auto text = histogram->render();
    volatile size_t sink = text.size();
    (void)sink;

    std::string name = threads > 1 ? "histogram multi" : "histogram single";
    PrintResult(name, static_cast<uint64_t>(threads) * ops_per_thread, elapsed);
}

void RunRegistryLookupBench(uint64_t iterations) {
    const Labels labels = {{"server", "bench"}, {"method", "GET"}};
    auto* registry = RegistryMgr::GetInstance();
    uintptr_t sink = 0;

    auto begin = Clock::now();
    for (uint64_t i = 0; i < iterations; ++i) {
        auto* counter = registry->getCounter(
            "test_metrics_bench_lookup_total", "bench lookup", labels);
        sink += reinterpret_cast<uintptr_t>(counter);
    }
    auto elapsed = Clock::now() - begin;

    (void)sink;
    PrintResult("registry lookup", iterations, elapsed);
}

} // namespace

int main(int argc, char** argv) {
    uint64_t ops_per_thread = 2000000;
    int threads = static_cast<int>(chen::GetCPUCount());
    if (threads <= 0) {
        threads = 1;
    }
    if (argc > 1) {
        ops_per_thread = std::strtoull(argv[1], nullptr, 10);
    }
    if (argc > 2) {
        threads = std::atoi(argv[2]);
        if (threads <= 0) {
            threads = 1;
        }
    }

// 项目的 Release 配置只加 -O3，不定义 NDEBUG，因此用 __OPTIMIZE__ 判断
#if !defined(__OPTIMIZE__)
    std::cout << "[WARN] unoptimized build (-O0), benchmark numbers are not representative; "
                 "build with -O2/-O3."
              << std::endl;
#endif

    std::cout << "metrics bench: cpu=" << chen::GetCPUCount()
              << " threads=" << threads
              << " ops_per_thread=" << ops_per_thread
              << std::endl;

    RunCounterBench(1, ops_per_thread);
    RunCounterBench(threads, ops_per_thread);
    RunHistogramBench(1, ops_per_thread);
    RunHistogramBench(threads, ops_per_thread);
    // 注册表查找含 mutex + label 排序 + key 构造，迭代数压低避免跑太久
    RunRegistryLookupBench(ops_per_thread);

    return 0;
}
