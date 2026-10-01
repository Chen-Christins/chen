#include "chen/thread/thread.h"
#include "chen/fiber/fiber.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

void run_in_fiber() {
    INFO(logger) << "run_in_fiber begin total=" << chen::Fiber::TotalFibers();
    chen::Fiber::YieldToHold();
    INFO(logger) << "run_in_fiber end total=" << chen::Fiber::TotalFibers();
    chen::Fiber::YieldToHold();
}

void test_fiber() {
    INFO(logger) << "main begin -1 total=" << chen::Fiber::TotalFibers();
    {
        chen::Fiber::GetThis(); // 创造一个暂时的协程，用来接任务的
        INFO(logger) << "main begin total=" << chen::Fiber::TotalFibers();
        chen::Fiber::ptr fiber(new chen::Fiber(run_in_fiber));
        fiber->swapIn();
        INFO(logger) << "main after swapIn total=" << chen::Fiber::TotalFibers();
        fiber->swapIn();
        INFO(logger) << "main after end total=" << chen::Fiber::TotalFibers();
        fiber->swapIn();
    }
    INFO(logger) << "main after end2 total=" << chen::Fiber::TotalFibers();
}

/* 
2025-11-15 16:59:40	14519	name_0	0	[INFO]	[root]	tests/test_fiber.cc:15	main begin -1 total=0
2025-11-15 16:59:40	14519	name_0	0	[DEBUG]	[system]	chen/fiber/fiber.cc:43	Fiber::Fiber main
2025-11-15 16:59:40	14519	name_0	0	[INFO]	[root]	tests/test_fiber.cc:18	main begin total=1
2025-11-15 16:59:40	14519	name_0	0	[DEBUG]	[system]	chen/fiber/fiber.cc:67	Fiber::Fiber id=1
2025-11-15 16:59:40	14519	name_0	1	[INFO]	[root]	tests/test_fiber.cc:8	run_in_fiber begin total=2
2025-11-15 16:59:40	14519	name_0	0	[INFO]	[root]	tests/test_fiber.cc:21	main after swapIn total=2
2025-11-15 16:59:40	14519	name_0	1	[INFO]	[root]	tests/test_fiber.cc:10	run_in_fiber end total=2
2025-11-15 16:59:40	14519	name_0	0	[INFO]	[root]	tests/test_fiber.cc:23	main after end total=2
2025-11-15 16:59:40	14519	name_0	0	[DEBUG]	[system]	chen/fiber/fiber.cc:84	Fiber::~Fiber id=1 total=1
2025-11-15 16:59:40	14519	name_0	0	[INFO]	[root]	tests/test_fiber.cc:26	main after end2 total=1
2025-11-15 16:59:40	14519	name_0	0	[DEBUG]	[system]	chen/fiber/fiber.cc:84	Fiber::~Fiber id=0 total=0
2025-11-15 16:59:40	14518	main	0	[INFO]	[root]	tests/test_fiber.cc:54	0
 */

int main(int argc, char** argv) {
    // logger->setLevel(chen::LogLevel::INFO);
    chen::Thread::SetName("main");
    std::vector<chen::Thread::ptr> thrs;
    for (int i = 0; i < 1; ++i) {
        thrs.push_back(chen::Thread::ptr(new chen::Thread(&test_fiber, "name_" + std::to_string(i))));
    }
    for (auto i : thrs) {
        i->join();
    }
    INFO(logger) << chen::Fiber::TotalFibers();
    return 0;
}