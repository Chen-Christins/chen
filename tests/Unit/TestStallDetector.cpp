/**
 * @file TestStallDetector.cpp
 * @brief 调度线程 stall 检测单元测试
 * @author Christins
 * @date 2026-11-28
 * @copyright GPL-3.0
 * @note 用例在测试主线程上注册/注销，不需要调度器。
 */
#include "../TestHarness.hpp"

#include "src/Util/StallDetector.hpp"

#include <string>
#include <unistd.h>

using chen::StallDetector;

namespace {

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace

CHEN_TEST(stall, detects_stalled_thread)
{
    StallDetector* detector = chen::StallDetectorMgr::GetInstance();
    detector->registerThread("unit_stalled");
    CHEN_CHECK_EQ(detector->threadCount(), 1u);

    detector->heartbeat();
    CHEN_CHECK(detector->check(10000).empty());

    usleep(20 * 1000);
    const std::string report = detector->check(10);
    CHEN_CHECK(!report.empty());
    CHEN_CHECK(Contains(report, "unit_stalled"));
    CHEN_CHECK(Contains(report, "has not returned to the event loop"));

    detector->unregisterThread();
    CHEN_CHECK_EQ(detector->threadCount(), 0u);
}

CHEN_TEST(stall, reports_once_per_episode)
{
    StallDetector* detector = chen::StallDetectorMgr::GetInstance();
    detector->registerThread("unit_once");
    detector->heartbeat();
    usleep(20 * 1000);

    CHEN_CHECK(!detector->check(10).empty());
    // 同一段 stall 只报告一次，避免刷屏
    CHEN_CHECK(detector->check(10).empty());

    // 心跳恢复后再次 stall 会重新报告
    detector->heartbeat();
    usleep(20 * 1000);
    CHEN_CHECK(!detector->check(10).empty());

    detector->unregisterThread();
}

CHEN_TEST(stall, idle_thread_is_ignored)
{
    StallDetector* detector = chen::StallDetectorMgr::GetInstance();
    detector->registerThread("unit_idle");
    detector->setIdle(true);
    usleep(20 * 1000);
    // 空闲阻塞在 epoll_wait 的线程不是 stall
    CHEN_CHECK(detector->check(10).empty());

    detector->setIdle(false);
    CHEN_CHECK(detector->check(10).empty());

    detector->unregisterThread();
}

CHEN_TEST(stall, dump_contains_thread_state)
{
    StallDetector* detector = chen::StallDetectorMgr::GetInstance();
    detector->registerThread("unit_dump");
    detector->setOp(StallDetector::OP_READ, 12);
    detector->setFiber(99);

    const std::string dump = detector->dump();
    CHEN_CHECK(Contains(dump, "unit_dump"));
    CHEN_CHECK(Contains(dump, "fiber=99"));
    CHEN_CHECK(Contains(dump, "read(fd=12)"));

    const std::string json = detector->dumpJson();
    CHEN_CHECK(Contains(json, "\"name\":\"unit_dump\""));
    CHEN_CHECK(Contains(json, "\"fiber\":99"));
    CHEN_CHECK(Contains(json, "\"op\":\"read\""));
    CHEN_CHECK(Contains(json, "\"fd\":12"));

    detector->clearOp();
    CHEN_CHECK(!Contains(detector->dump(), "read(fd=12)"));
    CHEN_CHECK(Contains(detector->dumpJson(), "\"op\":\"none\""));

    detector->unregisterThread();
}

CHEN_TEST(stall, zero_threshold_disables_check)
{
    StallDetector* detector = chen::StallDetectorMgr::GetInstance();
    detector->registerThread("unit_disabled");
    usleep(20 * 1000);
    CHEN_CHECK(detector->check(0).empty());
    detector->unregisterThread();
}

CHEN_TEST_MAIN()
