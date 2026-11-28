/**
 * @file TestTimeUtil.cpp
 * @brief TimeUtil 时间转换与判定单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @details 只使用显式 tz_offset = 0（UTC）的接口断言具体字符串，
 *          避免用例结果依赖运行机器的本地时区。
 */
#include "../TestHarness.hpp"

#include "src/Util/TimeUtil.hpp"

#include <cstdint>
#include <ctime>
#include <limits>
#include <string>

using chen::TimeUtil;

CHEN_TEST(timeutil, current_time_consistency)
{
    const uint64_t us = TimeUtil::GetCurrentUs();
    const uint64_t ms = TimeUtil::GetCurrentMs();
    const uint64_t ns = TimeUtil::GetCurrentNanos();
    const time_t sec = TimeUtil::GetCurrentSec();

    CHEN_CHECK(us > 0);
    CHEN_CHECK(ms > 0);
    CHEN_CHECK(ns > 0);
    CHEN_CHECK(sec > 0);

    // 连续调用存在时钟推进，允许 ±2ms 抖动
    CHEN_CHECK(ms + 2 >= us / 1000);
    CHEN_CHECK(us / 1000 + 2 >= ms);
    CHEN_CHECK(ns / 1000000 + 2 >= ms);
    CHEN_CHECK(ms + 2 >= static_cast<uint64_t>(sec) * 1000);
}

CHEN_TEST(timeutil, utc_format_known_values)
{
    const time_t ts = 1705329000; ///< 2024-01-15 14:30:00 UTC
    CHEN_CHECK_EQ(TimeUtil::Time2StrSec(ts, "%Y-%m-%d %H:%M:%S", 0), std::string("2024-01-15 14:30:00"));

    CHEN_CHECK_EQ(
        TimeUtil::Time2StrMs(1705329000123ull, "%Y-%m-%d %H:%M:%S.%f", 0), std::string("2024-01-15 14:30:00.123"));

    // tz_offset 单位是秒：UTC+8
    CHEN_CHECK_EQ(TimeUtil::Time2StrSec(ts, "%Y-%m-%d %H:%M:%S", 8 * 3600), std::string("2024-01-15 22:30:00"));
    // UTC-5
    CHEN_CHECK_EQ(TimeUtil::Time2StrSec(ts, "%Y-%m-%d %H:%M:%S", -5 * 3600), std::string("2024-01-15 09:30:00"));
}

CHEN_TEST(timeutil, unix_sec_to_ms)
{
    CHEN_CHECK_EQ(TimeUtil::UnixSecToMs(1705329000), 1705329000000ull);
    CHEN_CHECK_EQ(TimeUtil::UnixSecToMs(0), 0ull);
}

CHEN_TEST(timeutil, day_start_and_end)
{
    const time_t ts = 1705329000; ///< 2024-01-15 14:30:00 UTC
    const time_t day_start = TimeUtil::GetDayStart(ts, 0);
    const time_t day_end = TimeUtil::GetDayEnd(ts, 0);

    CHEN_CHECK_EQ(day_start, 1705276800); ///< 2024-01-15 00:00:00 UTC
    CHEN_CHECK_EQ(TimeUtil::Time2StrSec(day_start, "%Y-%m-%d %H:%M:%S", 0), std::string("2024-01-15 00:00:00"));
    CHEN_CHECK(day_end > day_start);
    CHEN_CHECK_EQ(day_end - day_start, 86399);
}

CHEN_TEST(timeutil, is_expired_ms)
{
    const uint64_t now = TimeUtil::GetCurrentMs();
    CHEN_CHECK(!TimeUtil::IsExpiredMs(now, 60000));
    CHEN_CHECK(TimeUtil::IsExpiredMs(now - 120000, 60000));
    CHEN_CHECK(TimeUtil::IsExpiredMs(now, 0));
}

CHEN_TEST(timeutil, str2time_auto_format)
{
    const char* text = "2024-01-15 14:30:00";
    const time_t explicit_parsed = TimeUtil::Str2Time(text, "%Y-%m-%d %H:%M:%S");
    const time_t auto_parsed = TimeUtil::Str2Time(std::string(text));

    // 两个入口都按本地时区解析，只需彼此一致，不依赖具体时区
    CHEN_CHECK_EQ(auto_parsed, explicit_parsed);
    CHEN_CHECK(explicit_parsed > 0);
}

CHEN_TEST(timeutil, str2time_timezone_offsets)
{
    // PostgreSQL 的 timestamptz 文本形如 "2024-01-15 10:30:00+08"，
    // MySQL 也可能带 "Z" 或 "+08:00"；三种写法应指向同一个瞬间
    const time_t utc = TimeUtil::Str2Time("2024-01-15T02:30:00Z");
    CHEN_CHECK(utc > 0);
    CHEN_CHECK_EQ(TimeUtil::Str2Time("2024-01-15 10:30:00+08:00"), utc);
    CHEN_CHECK_EQ(TimeUtil::Str2Time("2024-01-15 10:30:00+08"), utc);
    CHEN_CHECK_EQ(TimeUtil::Str2Time("2024-01-15T10:30:00+08:00"), utc);
}

CHEN_TEST(timeutil, str2time_fractional_and_invalid)
{
    CHEN_CHECK_EQ(TimeUtil::Str2Time("2024-01-15 10:30:00.123"), TimeUtil::Str2Time("2024-01-15 10:30:00"));
    CHEN_CHECK_EQ(TimeUtil::Str2Time("2024-01-15"), TimeUtil::Str2Time("2024-01-15 00:00:00"));
    CHEN_CHECK_EQ(TimeUtil::Str2Time(""), static_cast<time_t>(0));
    CHEN_CHECK_EQ(TimeUtil::Str2Time("not a time"), static_cast<time_t>(0));
}

CHEN_TEST(timeutil, next_reset_time_zero_interval)
{
    // interval_ms == 0 时 elapsed / interval_ms 会触发 SIGFPE
    const uint64_t base_ms = 1700000000000ull;
    CHEN_CHECK_EQ(TimeUtil::GetNextResetTimeMs(base_ms, 0), base_ms);
}

CHEN_TEST(timeutil, is_expired_handles_overflow)
{
    const uint64_t now_ms = TimeUtil::GetCurrentMs();
    // 旧实现 start_ms + ttl_ms 溢出回绕成过去的时间，导致永不过期
    CHEN_CHECK(!TimeUtil::IsExpiredMs(now_ms, std::numeric_limits<uint64_t>::max()));
    CHEN_CHECK(TimeUtil::IsExpiredMs(now_ms - 10, 1));
    CHEN_CHECK(!TimeUtil::IsExpiredMs(now_ms + 60000, 1000));
}

CHEN_TEST_MAIN()
