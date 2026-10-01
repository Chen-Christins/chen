#include "chen/schedule/schedule.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include <atomic>

static chen::Logger::ptr logger = LOG_ROOT();

// 全局计数器
std::atomic<int> g_task_count{0};
std::atomic<int> g_completed_count{0};
std::atomic<long> g_total_time{0};

// 全局调度器指针
chen::IOManager* g_scheduler = nullptr;

// 任务函数：模拟一些计算工作
void compute_task(int task_id) {
    auto start = std::chrono::high_resolution_clock::now();

    // 模拟不同复杂度的任务
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 10000);

    // 模拟计算工作
    volatile int sum = 0;
    int iterations = dis(gen);
    for (int i = 0; i < iterations; ++i) {
        sum += i;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    g_total_time += duration.count();
    g_completed_count++;

    if (task_id % 1000 == 0) {
        INFO(logger) << "Task " << task_id << " completed, took "
                     << duration.count() << " microseconds, sum=" << sum;
    }
}

// 批量任务生成函数
void batch_tasks(int start_id, int batch_size) {
    for (int i = start_id; i < start_id + batch_size; ++i) {
        g_scheduler->schedule([i]() {
            compute_task(i);
        });
    }
}

// 测试1：大量简单任务
void test_massive_simple_tasks() {
    INFO(logger) << "\n=== Test 1: Massive Simple Tasks ===";
    const int task_count = 1000000;  // 增加到100万个任务
    g_task_count = task_count;
    g_completed_count = 0;
    g_total_time = 0;

    auto start = std::chrono::high_resolution_clock::now();

    // 批量提交任务
    for (int i = 0; i < task_count; ++i) {
        g_scheduler->schedule([i]() {
            compute_task(i);
        });
    }

    // 等待所有任务完成
    while (g_completed_count < task_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    INFO(logger) << "Completed " << g_completed_count << " tasks in "
                 << total_duration.count() << " ms";
    INFO(logger) << "Average task time: " << (g_total_time / g_completed_count) << " microseconds";
    INFO(logger) << "Throughput: " << (g_completed_count * 1000.0 / total_duration.count()) << " tasks/sec";
}

// 测试2：分层任务调度
void test_hierarchical_tasks() {
    INFO(logger) << "\n=== Test 2: Hierarchical Tasks ===";
    g_task_count = 0;
    g_completed_count = 0;
    g_total_time = 0;

    auto start = std::chrono::high_resolution_clock::now();

    // 预先创建所有子任务，而不是在主任务中创建
    // 第一层：创建100个主任务
    for (int i = 0; i < 100; ++i) {
        g_task_count++;
        g_scheduler->schedule([i]() {
            INFO(logger) << "Main task " << i << " started";

            // 模拟主任务的工作
            volatile int sum = 0;
            for (int k = 0; k < 1000; ++k) {
                sum += k;
            }

            g_completed_count++;
            if (i % 10 == 0) {
                INFO(logger) << "Main task " << i << " completed";
            }
        });

        // 第二层：每个主任务对应1000个子任务
        for (int j = 0; j < 1000; ++j) {
            int task_id = i * 1000 + j;
            g_task_count++;
            g_scheduler->schedule([task_id, i]() {
                compute_task(task_id);
            });
        }
    }

    // 等待所有任务完成
    while (g_completed_count < g_task_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (g_completed_count % 100 == 0) {
            INFO(logger) << "Progress: " << g_completed_count << "/" << g_task_count;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    INFO(logger) << "Hierarchical tasks completed: " << g_completed_count
                 << " in " << total_duration.count() << " ms";
}

// 测试3：不同优先级的任务混合
void test_mixed_priority_tasks() {
    INFO(logger) << "\n=== Test 3: Mixed Priority Tasks ===";
    g_completed_count = 0;
    g_total_time = 0;

    auto start = std::chrono::high_resolution_clock::now();

    // 高优先级任务（快速任务）
    for (int i = 0; i < 10000; ++i) {
        g_scheduler->schedule([i]() {
            auto start = std::chrono::high_resolution_clock::now();
            volatile int sum = 0;
            for (int j = 0; j < 100; ++j) {
                sum += j;
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            g_total_time += duration.count();
            g_completed_count++;
        });
    }

    // 低优先级任务（慢速任务）
    for (int i = 10000; i < 15000; ++i) {
        g_scheduler->schedule([i]() {
            auto start = std::chrono::high_resolution_clock::now();
            volatile int sum = 0;
            for (int j = 0; j < 10000; ++j) {
                sum += j;
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            g_total_time += duration.count();
            g_completed_count++;

            if (i % 1000 == 0) {
                INFO(logger) << "Slow task " << i << " completed";
            }
        });
    }

    // 等待所有任务完成
    while (g_completed_count < 15000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    INFO(logger) << "Mixed priority tasks completed: " << g_completed_count
                 << " in " << total_duration.count() << " ms";
    INFO(logger) << "Average task time: " << (g_total_time / g_completed_count) << " microseconds";
}

// 测试4：持续任务流
void test_continuous_task_stream() {
    INFO(logger) << "\n=== Test 4: Continuous Task Stream ===";
    g_completed_count = 0;
    g_total_time = 0;

    std::atomic<bool> stop_flag{false};
    int total_tasks = 0;

    auto start = std::chrono::high_resolution_clock::now();

    // 启动任务生产者
    g_scheduler->schedule([&stop_flag, &total_tasks]() {
        int task_id = 0;
        while (!stop_flag) {
            // 每次产生10个任务
            for (int i = 0; i < 10; ++i) {
                g_scheduler->schedule([task_id]() {
                    compute_task(task_id);
                });
                task_id++;
                total_tasks++;
            }

            // 短暂休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        INFO(logger) << "Producer stopped, generated " << total_tasks << " tasks";
    });

    // 运行5秒
    std::this_thread::sleep_for(std::chrono::seconds(5));
    stop_flag = true;

    // 等待剩余任务完成
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    INFO(logger) << "Continuous stream: " << g_completed_count << " tasks completed in "
                 << total_duration.count() << " ms";
    INFO(logger) << "Throughput: " << (g_completed_count * 1000.0 / total_duration.count()) << " tasks/sec";
}

// 测试5：内存压力测试
void test_memory_pressure() {
    INFO(logger) << "\n=== Test 5: Memory Pressure Test ===";
    g_completed_count = 0;

    auto start = std::chrono::high_resolution_clock::now();

    // 创建大量内存密集型任务
    for (int i = 0; i < 100000; ++i) {
        g_scheduler->schedule([i]() {
            // 分配更多内存
            std::vector<int> data(10000);
            for (int j = 0; j < 10000; ++j) {
                data[j] = i * 10000 + j;
            }

            // 模拟一些计算
            volatile long sum = 0;
            for (int val : data) {
                sum += val;
            }

            g_completed_count++;

            if (i % 10000 == 0) {
                INFO(logger) << "Memory task " << i << " completed, sum=" << sum;
            }
        });
    }

    // 等待所有任务完成
    while (g_completed_count < 100000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    INFO(logger) << "Memory pressure test: " << g_completed_count
                 << " tasks completed in " << total_duration.count() << " ms";
}

// 测试6：极限并发测试
void test_extreme_concurrency() {
    INFO(logger) << "\n=== Test 6: Extreme Concurrency Test ===";
    g_completed_count = 0;
    g_total_time = 0;

    auto start = std::chrono::high_resolution_clock::now();

    // 创建超大规模的任务队列
    const int batch_count = 100;  // 100个批次
    const int tasks_per_batch = 10000;  // 每批10000个任务
    const int total_tasks = batch_count * tasks_per_batch;

    INFO(logger) << "Scheduling " << total_tasks << " tasks in " << batch_count << " batches";

    // 分批提交任务，避免一次性提交过多导致内存压力
    for (int batch = 0; batch < batch_count; ++batch) {
        // 每批提交任务
        for (int i = 0; i < tasks_per_batch; ++i) {
            int task_id = batch * tasks_per_batch + i;
            g_scheduler->schedule([task_id]() {
                // 轻量级任务
                volatile int sum = 0;
                for (int j = 0; j < 100; ++j) {
                    sum += j;
                }
                g_completed_count++;

                if (task_id % 100000 == 0) {
                    INFO(logger) << "Completed task " << task_id;
                }
            });
        }

        // 每批之间短暂休眠，让系统处理一下
        if (batch % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            INFO(logger) << "Submitted batch " << (batch + 1) << "/" << batch_count;
        }
    }

    // 等待所有任务完成
    while (g_completed_count < total_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        INFO(logger) << "Progress: " << g_completed_count << "/" << total_tasks
                     << " (" << (g_completed_count * 100 / total_tasks) << "%)";
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    INFO(logger) << "Extreme concurrency test: " << g_completed_count
                 << " tasks completed in " << total_duration.count() << " ms";
    INFO(logger) << "Peak throughput: " << (g_completed_count * 1000.0 / total_duration.count()) << " tasks/sec";
}

int main(int argc, char** argv) {
    chen::Thread::SetName("main");
    INFO(logger) << "=== Massive Task Scheduling Test Started ===";

    // 设置调度器参数
    size_t thread_count = 8;  // 使用8个工作线程以增加并发
    if (argc > 1) {
        thread_count = std::stoul(argv[1]);
    }

    INFO(logger) << "Using " << thread_count << " worker threads";

    chen::IOManager sc(thread_count, false, "test_scheduler");
    g_scheduler = &sc;  // 设置全局调度器指针
    sc.start();

    // 等待调度器完全启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 运行各种测试
    test_massive_simple_tasks();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    test_hierarchical_tasks();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    test_mixed_priority_tasks();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    test_continuous_task_stream();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    test_memory_pressure();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    test_extreme_concurrency();

    INFO(logger) << "\n=== All Tests Completed ===";

    sc.stop();
    INFO(logger) << "Scheduler stopped";

    return 0;
}