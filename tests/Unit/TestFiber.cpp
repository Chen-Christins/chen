/**
 * @file TestFiber.cpp
 * @brief Fiber 线程主协程复用与调度器重建的回归测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @note 不监听端口；调度器用例自建 IOManager 并在结束时 stop。
 */
#include "../TestHarness.hpp"

#include "src/Fiber/Fiber.hpp"
#include "src/IOManager/IOManager.hpp"

#include <atomic>

using chen::Fiber;
using chen::IOManager;

CHEN_TEST(fiber, main_fiber_is_stable_on_thread)
{
    Fiber::ptr first = Fiber::GetThis();
    Fiber::ptr second = Fiber::GetThis();

    CHEN_CHECK(first != nullptr);
    CHEN_CHECK(first.get() == second.get());
    CHEN_CHECK_EQ(first->getId(), second->getId());
}

CHEN_TEST(fiber, recreate_scheduler_on_same_thread)
{
    // 回归：第一个调度器销毁后，线程的 t_fiber 被清空、但 t_threadFiber 仍持有旧主协程。
    // 第二次创建调度器时 GetThis() 先替换 t_threadFiber，旧主协程析构会执行
    // SetThis(nullptr)；若此前已经设置 t_fiber，就会被清成空指针并在这里崩溃。
    for (int i = 0; i < 3; ++i) {
        IOManager iom(2, true, "fiber_unit");

        std::atomic<bool> ran{false};
        iom.schedule([&ran]() {
            ran = true;
        });
        iom.stop();

        CHEN_CHECK(ran.load());
    }
}

CHEN_TEST_MAIN()
