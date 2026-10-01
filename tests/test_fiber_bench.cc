#include "chen/fiber/fiber.h"
#include "chen/log/log.h"
#include "chen/util/util.h"
#include "chen/thread/thread.h"

#include <iostream>
#include <iomanip>

static chen::Logger::ptr g_logger = LOG_ROOT();

//
// 测量单次 jump_fcontext 上下文切换的耗时
//
// 每次往返: main --[swapIn]--> fiber --[YieldToHold/swapOut]--> main
// 包含 2 次 jump_fcontext（切入 + 切回）
//

struct BenchState {
    int64_t rounds;
    int64_t count;
};

static void runner(BenchState* s) {
    while (s->count < s->rounds) {
        ++s->count;
        chen::Fiber::YieldToHold();
    }
}

// 单 fiber 往返，每次往返 = 2次 jump_fcontext
static uint64_t benchSingleFiber(int64_t rounds) {
    BenchState state;
    state.rounds = rounds;
    state.count = 0;

    chen::Fiber::ptr f(new chen::Fiber(std::bind(runner, &state)));

    uint64_t start = chen::GetCurrentNanos();
    while (state.count < state.rounds) {
        f->swapIn();
    }
    return chen::GetCurrentNanos() - start;
}

// 双 fiber ping-pong，每次循环 = 4次 jump_fcontext
static uint64_t benchPingPong(int64_t rounds) {
    BenchState state;
    state.rounds = rounds;
    state.count = 0;

    chen::Fiber::ptr a(new chen::Fiber(std::bind(runner, &state)));
    chen::Fiber::ptr b(new chen::Fiber(std::bind(runner, &state)));

    uint64_t start = chen::GetCurrentNanos();
    while (state.count < state.rounds) {
        a->swapIn();
        b->swapIn();
    }
    return chen::GetCurrentNanos() - start;
}

int main(int argc, char** argv) {
    chen::Thread::SetName("main");
    chen::Fiber::GetThis(); // 创建 main fiber

    const int64_t warmupRounds = 100000;
    const int64_t benchRounds  = 2000000;

    // ---- mode 1: 单 fiber 往返 ----
    benchSingleFiber(warmupRounds);
    uint64_t t1 = benchSingleFiber(benchRounds);

    // 每次往返 = swapIn 切入 + YieldToHold 切回 = 2 次 jump_fcontext
    int64_t switches1 = benchRounds * 2;

    // ---- mode 2: 双 fiber ping-pong ----
    benchPingPong(warmupRounds);
    uint64_t t2 = benchPingPong(benchRounds);

    // mode2 双 fiber: 每轮 count 增加 2（A+1, B+1），主循环跑 benchRounds/2 轮
    // 每轮 4 次 jump_fcontext，总数 = (benchRounds/2) * 4 = benchRounds * 2
    int64_t switches2 = benchRounds * 2;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n";
    std::cout << "┌─────────────────────────────────────────────────┐\n";
    std::cout << "│       Fiber Context Switch Benchmark             │\n";
    std::cout << "├─────────────────────────────────────────────────┤\n";
    std::cout << "│ 模式1: 单 fiber 往返 (main ⇄ fiber)              │\n";
    std::cout << "│   " << std::setw(10) << benchRounds << " 次往返 → " << std::setw(12) << switches1 << " 次 jump_fcontext\n";
    std::cout << "│   总耗时:    " << std::setw(8) << t1 / 1000 << " μs\n";
    std::cout << "│   每次切换:  " << std::setw(8) << (double)t1 / switches1 << " ns\n";
    std::cout << "│                                                 │\n";
    std::cout << "│ 模式2: 双 fiber ping-pong (A ⇄ B 交替)           │\n";
    std::cout << "│   " << std::setw(10) << benchRounds << " 次递增 → " << std::setw(12) << switches2 << " 次 jump_fcontext\n";
    std::cout << "│   总耗时:    " << std::setw(8) << t2 / 1000 << " μs\n";
    std::cout << "│   每次切换:  " << std::setw(8) << (double)t2 / switches2 << " ns\n";
    std::cout << "└─────────────────────────────────────────────────┘\n\n";

    INFO(g_logger) << std::fixed << std::setprecision(2)
                   << "mode1(single): " << (double)t1 / switches1 << " ns/switch, "
                   << "mode2(pingpong): " << (double)t2 / switches2 << " ns/switch";

    return 0;
}
