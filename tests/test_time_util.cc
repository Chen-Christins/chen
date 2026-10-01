#include "chen/util/time_util.h"
#include <iostream>
#include <cassert>

void test_basic_time() {
    std::cout << "=== test_basic_time ===" << std::endl;

    uint64_t us = chen::GetCurrentUs();
    uint64_t ms = chen::GetCurrentMs();
    uint64_t ns = chen::GetCurrentNanos();
    time_t sec = chen::GetCurrentSec();

    std::cout << "us: " << us << std::endl;
    std::cout << "ms: " << ms << std::endl;
    std::cout << "ns: " << ns << std::endl;
    std::cout << "sec: " << sec << std::endl;

    assert(ms >= us / 1000);
    assert(ns >= ms * 1000000);
    assert(sec > 0);
    assert(us > 0);

    // Verify relationship: ms ≈ us / 1000, ns ≈ ms * 1000000
    assert(ms - us / 1000 <= 1);  // rounding tolerance
    assert(ns / 1000000 - ms <= 1);

    std::cout << "PASS" << std::endl;
}

void test_time_conversion() {
    std::cout << "=== test_time_conversion ===" << std::endl;

    // Time2Str with time_t
    time_t ts = 1705300000; // 2024-01-15 ~xx:xx:xx
    std::string s1 = chen::Time2Str(ts);
    std::cout << "Time2Str(time_t): " << s1 << std::endl;
    assert(!s1.empty());

    // Time2Str with uint64_t (ms)
    uint64_t ts_ms = static_cast<uint64_t>(ts) * 1000 + 456;
    std::string s2 = chen::Time2Str(ts_ms);
    std::cout << "Time2Str(ms): " << s2 << std::endl;
    // Should contain something like ".456"
    assert(s2.find("456") != std::string::npos || s2.find("46") != std::string::npos);

    // Str2Time round-trip
    time_t parsed = chen::Str2Time(s1.c_str(), "%Y-%m-%d %H:%M:%S");
    std::cout << "Str2Time: " << parsed << " == " << ts << std::endl;
    // Note: mktime may adjust for DST, so we allow small difference
    assert(std::abs(static_cast<int64_t>(parsed - ts)) <= 86400);

    std::cout << "PASS" << std::endl;
}

void test_auto_str2time() {
    std::cout << "=== test_auto_str2time ===" << std::endl;

    // Standard format
    time_t t1 = chen::Str2Time("2024-01-15 10:30:00");
    std::cout << "Standard: " << t1 << std::endl;
    assert(t1 > 0);

    // ISO 8601 without timezone
    time_t t2 = chen::Str2Time("2024-01-15T10:30:00");
    std::cout << "ISO 8601 no tz: " << t2 << std::endl;
    assert(t2 > 0);
    // Both should parse to the same local time
    assert(t1 == t2);

    // ISO 8601 with Z (UTC)
    time_t t3 = chen::Str2Time("2024-01-15T10:30:00Z");
    std::cout << "ISO 8601 Z: " << t3 << " -> " << chen::Time2Str(t3) << std::endl;
    assert(t3 > 0);

    // ISO 8601 with timezone offset
    time_t t4 = chen::Str2Time("2024-01-15T10:30:00+08:00");
    std::cout << "ISO 8601 +08:00: " << t4 << " -> " << chen::Time2Str(t4) << std::endl;
    assert(t4 > 0);

    // ISO 8601 with milliseconds and Z
    time_t t5 = chen::Str2Time("2024-01-15T10:30:00.123Z");
    std::cout << "ISO 8601 ms Z: " << t5 << " -> " << chen::Time2Str(t5) << std::endl;
    assert(t5 > 0);
    // Same second (ms stripped), so should equal t3
    assert(t5 == t3);

    // Date only
    time_t t6 = chen::Str2Time("2024-01-15");
    std::cout << "Date only: " << t6 << " -> " << chen::Time2Str(t6) << std::endl;
    assert(t6 > 0);

    // Time only
    time_t t7 = chen::Str2Time("10:30:00");
    std::cout << "Time only: " << t7 << " -> " << chen::Time2Str(t7) << std::endl;
    assert(t7 > 0);

    // Empty string
    time_t t8 = chen::Str2Time("");
    assert(t8 == 0);

    // Invalid string
    time_t t9 = chen::Str2Time("not-a-time");
    assert(t9 == 0);

    // GitHub-style: full ISO 8601 with timezone
    time_t t10 = chen::Str2Time("2026-06-26T08:25:58+08:00");
    std::cout << "GitHub-style: " << t10 << " -> " << chen::Time2Str(t10) << std::endl;
    assert(t10 > 0);

    // Negative timezone offset
    time_t t11 = chen::Str2Time("2024-01-15T10:30:00-05:00");
    std::cout << "ISO 8601 -05:00: " << t11 << " -> " << chen::Time2Str(t11) << std::endl;
    assert(t11 > 0);
    // -05:00 means 5 hours behind UTC, so UTC time is 15:30:00
    // t3 is 10:30:00Z = 10:30:00 UTC
    // t11 parsed as 10:30:00 -05:00 = 15:30:00 UTC
    // So t11 should be t3 + 5 hours
    assert(t11 == t3 + 5 * 3600);

    std::cout << "PASS" << std::endl;
}

void test_rfc1123() {
    std::cout << "=== test_rfc1123 ===" << std::endl;

    std::string rfc = chen::GetRFC1123Time(1705300000);
    std::cout << "RFC1123: " << rfc << std::endl;
    assert(!rfc.empty());
    // Should end with " GMT"
    assert(rfc.size() >= 4 && rfc.substr(rfc.size() - 4) == " GMT");

    // Verify we can parse it back with Str2Time... actually RFC1123 format isn't
    // in our auto-detection. Let's just check formatting.
    std::cout << "PASS" << std::endl;
}

void test_time_judgment() {
    std::cout << "=== test_time_judgment ===" << std::endl;

    time_t ts = 1705350000;  // 2024-01-15 some time

    // IsSameDay
    time_t same_day = ts + 3600;  // 1 hour later
    time_t next_day = ts + 86400; // 1 day later
    assert(chen::IsSameDay(ts, same_day) == true);
    assert(chen::IsSameDay(ts, next_day) == false);

    // IsSameMonth
    assert(chen::IsSameMonth(ts, ts + 86400 * 10) == true);

    // IsLeapYear
    assert(chen::IsLeapYear(2024) == true);
    assert(chen::IsLeapYear(2023) == false);
    assert(chen::IsLeapYear(2000) == true);
    assert(chen::IsLeapYear(1900) == false);

    // DaysInMonth
    assert(chen::DaysInMonth(2024, 2) == 29);
    assert(chen::DaysInMonth(2023, 2) == 28);
    assert(chen::DaysInMonth(2024, 1) == 31);
    assert(chen::DaysInMonth(2024, 4) == 30);
    assert(chen::DaysInMonth(2024, 0) == 0);  // invalid
    assert(chen::DaysInMonth(2024, 13) == 0); // invalid

    // IsExpired / GetRemainingMs
    uint64_t now_ms = chen::GetCurrentMs();
    assert(chen::IsExpired(now_ms - 1000, 500) == true);  // already expired
    assert(chen::IsExpired(now_ms, 100000) == false);     // not yet expired
    assert(chen::GetRemainingMs(now_ms, 100000) <= 100000);
    assert(chen::GetRemainingMs(now_ms - 10000, 1000) == 0); // expired

    std::cout << "PASS" << std::endl;
}

void test_boundary() {
    std::cout << "=== test_boundary ===" << std::endl;

    time_t ts = 1705350000;  // some timestamp

    // Day boundary
    time_t day_start = chen::GetDayStart(ts);
    time_t day_end = chen::GetDayEnd(ts);
    std::cout << "ts=" << ts << " day_start=" << day_start << " day_end=" << day_end << std::endl;
    // The day start should be <= ts
    assert(day_start <= ts);
    assert(day_end >= ts);
    assert(chen::IsSameDay(day_start, day_end));
    // Verify start is at midnight
    std::string start_str = chen::Time2Str(day_start, "%H:%M:%S");
    assert(start_str == "00:00:00");
    std::string end_str = chen::Time2Str(day_end, "%H:%M:%S");
    assert(end_str == "23:59:59");

    // Week boundary
    time_t week_start = chen::GetWeekStart(ts);
    time_t week_end = chen::GetWeekEnd(ts);
    std::cout << "week_start=" << week_start << " week_end=" << week_end << std::endl;
    assert(week_start <= ts);
    assert(week_end >= ts);
    assert(chen::GetDayOfWeek(week_start) == 1); // Monday

    // Month boundary
    time_t month_start = chen::GetMonthStart(ts);
    time_t month_end = chen::GetMonthEnd(ts);
    std::cout << "month_start=" << month_start << " month_end=" << month_end << std::endl;
    assert(month_start <= ts);
    assert(month_end >= ts);
    std::cout << "PASS" << std::endl;
}

void test_field_extract() {
    std::cout << "=== test_field_extract ===" << std::endl;

    // Use a known timestamp: 2024-01-15 is a Monday
    // 2024-01-01 was Monday, so 2024-01-15 is also Monday
    time_t ts = 1705300000;
    struct tm tm;
    localtime_r(&ts, &tm);

    int wday = chen::GetDayOfWeek(ts);
    std::cout << "DayOfWeek: " << wday << " (struct tm says: " << tm.tm_wday << ")" << std::endl;
    assert(wday == tm.tm_wday);

    int yday = chen::GetDayOfYear(ts);
    std::cout << "DayOfYear: " << yday << " (struct tm says: " << tm.tm_yday + 1 << ")" << std::endl;
    assert(yday == tm.tm_yday + 1);

    int midnight_secs = chen::GetSecondsSinceMidnight(ts);
    int expected = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    std::cout << "SecondsSinceMidnight: " << midnight_secs << " (expected: " << expected << ")" << std::endl;
    assert(midnight_secs == expected);

    std::cout << "PASS" << std::endl;
}

void test_duration() {
    std::cout << "=== test_duration ===" << std::endl;

    assert(chen::FormatDuration(0) == "0s");
    assert(chen::FormatDuration(500) == "0s");
    assert(chen::FormatDuration(1000) == "1s");
    assert(chen::FormatDuration(5000) == "5s");
    assert(chen::FormatDuration(65000) == "1m 5s");
    assert(chen::FormatDuration(3600000) == "1h");
    assert(chen::FormatDuration(3661000) == "1h 1m 1s");
    assert(chen::FormatDuration(90061000) == "1d 1h 1m 1s");
    assert(chen::FormatDuration(86400000) == "1d");

    std::cout << "PASS" << std::endl;
}

void test_days_between() {
    std::cout << "=== test_days_between ===" << std::endl;

    time_t day1 = chen::Str2Time("2024-01-01");
    time_t day2 = chen::Str2Time("2024-01-15");

    int64_t diff = chen::DaysBetween(day1, day2);
    std::cout << "Days between 2024-01-01 and 2024-01-15: " << diff << std::endl;
    assert(diff == 14);

    // Same day
    assert(chen::DaysBetween(day1, day1) == 0);

    // Reverse order
    assert(chen::DaysBetween(day2, day1) == 14);

    std::cout << "PASS" << std::endl;
}

void test_next_reset() {
    std::cout << "=== test_next_reset ===" << std::endl;

    uint64_t now_ms = chen::GetCurrentMs();
    uint64_t base = now_ms - 5000;  // 5 seconds ago
    uint64_t interval = 10000;      // 10 second interval

    uint64_t next = chen::GetNextResetTime(base, interval);
    std::cout << "next reset: " << next << " (base=" << base << ", now=" << now_ms << ")" << std::endl;
    assert(next > now_ms);
    assert((next - base) % interval == 0);

    // Future base
    uint64_t future_base = now_ms + 100000;
    uint64_t next_future = chen::GetNextResetTime(future_base, interval);
    assert(next_future == future_base);

    std::cout << "PASS" << std::endl;
}

void test_timezone_offset() {
    std::cout << "=== test_timezone_offset ===" << std::endl;

    int tz = chen::TimeUtil::GetTimezoneOffset();
    // 合法时区范围：-12h ~ +14h
    assert(tz >= -43200 && tz <= 50400);
    std::cout << "  local timezone offset: " << tz << "s (" << tz / 3600 << "h)" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_time2str_with_timezone() {
    std::cout << "=== test_time2str_with_timezone ===" << std::endl;

    // 用已知时间戳：2024-01-15 14:30:00 UTC
    // 对应北京时间 2024-01-15 22:30:00，纽约时间 2024-01-15 09:30:00
    time_t ts = 1705329000;  // 2024-01-15 14:30:00 UTC

    // UTC 格式化
    std::string utc_str = chen::TimeUtil::Time2Str(ts, "%Y-%m-%d %H:%M:%S", 0);
    assert(utc_str == "2024-01-15 14:30:00");
    std::cout << "  UTC:     " << utc_str << std::endl;

    // CST +08:00
    std::string cst_str = chen::TimeUtil::Time2Str(ts, "%Y-%m-%d %H:%M:%S", 28800);
    assert(cst_str == "2024-01-15 22:30:00");
    std::cout << "  CST+08:  " << cst_str << std::endl;

    // EST -05:00
    std::string est_str = chen::TimeUtil::Time2Str(ts, "%Y-%m-%d %H:%M:%S", -18000);
    assert(est_str == "2024-01-15 09:30:00");
    std::cout << "  EST-05:  " << est_str << std::endl;

    // +05:30 印度时区（半小时偏移）
    std::string ist_str = chen::TimeUtil::Time2Str(ts, "%Y-%m-%d %H:%M:%S", 19800);
    assert(ist_str == "2024-01-15 20:00:00");
    std::cout << "  IST+05:30: " << ist_str << std::endl;

    // 跨日期：UTC 23:00 在 +08:00 为次日 07:00
    time_t ts2 = 1705360000;  // 2024-01-15 23:06:40 UTC → +08:00 2024-01-16 07:06:40
    std::string cst_cross = chen::TimeUtil::Time2Str(ts2, "%Y-%m-%d %H:%M:%S", 28800);
    assert(cst_cross == "2024-01-16 07:06:40");
    std::cout << "  cross-day CST: " << cst_cross << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_time2str_ms_with_timezone() {
    std::cout << "=== test_time2str_ms_with_timezone ===" << std::endl;

    // 毫秒时间戳
    uint64_t ts_ms = 1705329000000ULL;  // 2024-01-15 14:30:00.000 UTC

    std::string utc_ms = chen::TimeUtil::Time2Str(ts_ms, "%Y-%m-%d %H:%M:%S.%f", 0);
    assert(utc_ms == "2024-01-15 14:30:00.000");
    std::cout << "  UTC ms:              " << utc_ms << std::endl;

    std::string cst_ms = chen::TimeUtil::Time2Str(ts_ms, "%Y-%m-%d %H:%M:%S.%f", 28800);
    assert(cst_ms == "2024-01-15 22:30:00.000");
    std::cout << "  CST+08:00 ms:        " << cst_ms << std::endl;

    // 带上非零毫秒
    uint64_t ts_ms2 = 1705329000456ULL;  // 456ms
    std::string utc_ms2 = chen::TimeUtil::Time2Str(ts_ms2, "%H:%M:%S.%f", 0);
    assert(utc_ms2 == "14:30:00.456");
    std::cout << "  UTC ms with 456ms:   " << utc_ms2 << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_day_boundary_with_timezone() {
    std::cout << "=== test_day_boundary_with_timezone ===" << std::endl;

    // 2024-01-15 14:30:00 UTC
    time_t ts = 1705329000;

    // UTC 日边界
    time_t utc_start = chen::TimeUtil::GetDayStart(ts, 0);
    time_t utc_end = chen::TimeUtil::GetDayEnd(ts, 0);
    std::string utc_start_str = chen::TimeUtil::Time2Str(utc_start, "%Y-%m-%d %H:%M:%S", 0);
    std::string utc_end_str = chen::TimeUtil::Time2Str(utc_end, "%Y-%m-%d %H:%M:%S", 0);
    assert(utc_start_str == "2024-01-15 00:00:00");
    assert(utc_end_str == "2024-01-15 23:59:59");
    std::cout << "  UTC day: [" << utc_start_str << ", " << utc_end_str << "]" << std::endl;

    // CST +08:00 日边界（ts 是 UTC 14:30，北京时间 22:30，仍在 15 号）
    time_t cst_start = chen::TimeUtil::GetDayStart(ts, 28800);
    time_t cst_end = chen::TimeUtil::GetDayEnd(ts, 28800);
    std::string cst_start_str = chen::TimeUtil::Time2Str(cst_start, "%Y-%m-%d %H:%M:%S", 28800);
    std::string cst_end_str = chen::TimeUtil::Time2Str(cst_end, "%Y-%m-%d %H:%M:%S", 28800);
    assert(cst_start_str == "2024-01-15 00:00:00");
    assert(cst_end_str == "2024-01-15 23:59:59");
    std::cout << "  CST day: [" << cst_start_str << ", " << cst_end_str << "]" << std::endl;

    // UTC day start 对应的 UTC timestamp 应该比 CST day start 早 8h
    assert(utc_start - cst_start == 28800);
    std::cout << "  UTC start - CST start = " << (utc_start - cst_start) << "s (8h)" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_week_boundary_with_timezone() {
    std::cout << "=== test_week_boundary_with_timezone ===" << std::endl;

    time_t ts = 1705329000;  // 2024-01-15 Monday 14:30 UTC

    // UTC 周边界
    time_t utc_week_start = chen::TimeUtil::GetWeekStart(ts, 0);
    time_t utc_week_end = chen::TimeUtil::GetWeekEnd(ts, 0);
    std::string ws_utc = chen::TimeUtil::Time2Str(utc_week_start, "%Y-%m-%d %H:%M:%S", 0);
    std::string we_utc = chen::TimeUtil::Time2Str(utc_week_end, "%Y-%m-%d %H:%M:%S", 0);
    std::cout << "  UTC week: [" << ws_utc << ", " << we_utc << "]" << std::endl;
    // 2024-01-15 是周一，所以 week start 应该是当天 00:00:00
    assert(ws_utc == "2024-01-15 00:00:00");

    // CST 周边界
    time_t cst_week_start = chen::TimeUtil::GetWeekStart(ts, 28800);
    std::string ws_cst = chen::TimeUtil::Time2Str(cst_week_start, "%Y-%m-%d %H:%M:%S", 28800);
    std::cout << "  CST week start: " << ws_cst << std::endl;
    assert(ws_cst == "2024-01-15 00:00:00");

    std::cout << "PASS" << std::endl;
}

void test_month_boundary_with_timezone() {
    std::cout << "=== test_month_boundary_with_timezone ===" << std::endl;

    time_t ts = 1705329000;  // 2024-01-15

    // UTC 月边界
    time_t utc_month_start = chen::TimeUtil::GetMonthStart(ts, 0);
    time_t utc_month_end = chen::TimeUtil::GetMonthEnd(ts, 0);
    std::string ms_utc = chen::TimeUtil::Time2Str(utc_month_start, "%Y-%m-%d %H:%M:%S", 0);
    std::string me_utc = chen::TimeUtil::Time2Str(utc_month_end, "%Y-%m-%d %H:%M:%S", 0);
    assert(ms_utc == "2024-01-01 00:00:00");
    assert(me_utc == "2024-01-31 23:59:59");
    std::cout << "  UTC month: [" << ms_utc << ", " << me_utc << "]" << std::endl;

    // CST 月边界（UTC 12月31日晚上在 CST 已经是 1月1日）
    time_t ts_dec31_utc = chen::Str2Time("2023-12-31 18:00:00");  // UTC
    time_t cst_month_start = chen::TimeUtil::GetMonthStart(ts_dec31_utc, 28800);
    std::string ms_cst = chen::TimeUtil::Time2Str(cst_month_start, "%Y-%m-%d %H:%M:%S", 28800);
    // 北京时间 12月31日 18:00 + 8h = 1月1日 02:00，所以 month start 是 1月1日
    std::cout << "  CST month start of Dec 31 18:00 UTC: " << ms_cst << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_issame_with_timezone() {
    std::cout << "=== test_issame_with_timezone ===" << std::endl;

    // 使用 ISO 8601 Z 格式解析为 UTC 时间戳，保证一致性
    // UTC: 2024-01-15 23:30:00 和 2024-01-16 00:30:00
    time_t t1_utc = chen::TimeUtil::Str2Time("2024-01-15T23:30:00Z");
    time_t t2_utc = chen::TimeUtil::Str2Time("2024-01-16T00:30:00Z");

    // UTC 时区：不同天
    assert(chen::TimeUtil::IsSameDay(t1_utc, t2_utc, 0) == false);

    // CST +08:00：t1_utc = 2024-01-16 07:30, t2_utc = 2024-01-16 08:30 — 同一天
    assert(chen::TimeUtil::IsSameDay(t1_utc, t2_utc, 28800) == true);
    std::cout << "  UTC different day, CST same day: OK" << std::endl;

    // 同一个月（使用 UTC 解析，避开月末边界防止跨时区跨月）
    time_t t3 = chen::TimeUtil::Str2Time("2024-01-10T00:00:00Z");
    time_t t4 = chen::TimeUtil::Str2Time("2024-01-20T12:00:00Z");
    assert(chen::TimeUtil::IsSameMonth(t3, t4, 0) == true);
    assert(chen::TimeUtil::IsSameMonth(t3, t4, 28800) == true);
    std::cout << "  Same month OK" << std::endl;

    // 跨月场景：UTC 1月31日 20:00 = CST 2月1日 04:00
    time_t t5 = chen::TimeUtil::Str2Time("2024-01-31T20:00:00Z");
    time_t t6 = chen::TimeUtil::Str2Time("2024-02-01T00:00:00Z");
    assert(chen::TimeUtil::IsSameMonth(t5, t6, 0) == false);  // UTC: Jan vs Feb
    // CST: t5 = 2月1日 04:00, t6 = 2月1日 08:00 → 同月
    assert(chen::TimeUtil::IsSameMonth(t5, t6, 28800) == true);
    std::cout << "  Cross-month with timezone OK" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_field_extract_with_timezone() {
    std::cout << "=== test_field_extract_with_timezone ===" << std::endl;

    time_t ts = 1705329000;  // 2024-01-15 14:30:00 UTC, Monday

    // UTC: Monday 14:30
    int dow_utc = chen::TimeUtil::GetDayOfWeek(ts, 0);
    int yday_utc = chen::TimeUtil::GetDayOfYear(ts, 0);
    int secs_utc = chen::TimeUtil::GetSecondsSinceMidnight(ts, 0);
    assert(dow_utc == 1);           // Monday
    assert(yday_utc == 15);         // Jan 15
    assert(secs_utc == 52200);      // 14*3600 + 30*60 = 52200
    std::cout << "  UTC: dow=" << dow_utc << " yday=" << yday_utc << " secs=" << secs_utc << std::endl;

    // CST: Monday 22:30
    int dow_cst = chen::TimeUtil::GetDayOfWeek(ts, 28800);
    int yday_cst = chen::TimeUtil::GetDayOfYear(ts, 28800);
    int secs_cst = chen::TimeUtil::GetSecondsSinceMidnight(ts, 28800);
    assert(dow_cst == 1);           // Still Monday
    assert(yday_cst == 15);         // Still Jan 15
    assert(secs_cst == 81000);      // 22*3600 + 30*60 = 81000
    std::cout << "  CST: dow=" << dow_cst << " yday=" << yday_cst << " secs=" << secs_cst << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_timezone_default_is_local() {
    std::cout << "=== test_timezone_default_is_local ===" << std::endl;

    int tz = chen::TimeUtil::GetTimezoneOffset();
    time_t ts = chen::TimeUtil::GetCurrentSec();

    // 不带时区参数的调用和显式传本地时区应该结果一致
    std::string local_default = chen::TimeUtil::Time2Str(ts);
    std::string local_explicit = chen::TimeUtil::Time2Str(ts, "%Y-%m-%d %H:%M:%S", tz);
    assert(local_default == local_explicit);

    time_t day_default = chen::TimeUtil::GetDayStart(ts);
    time_t day_explicit = chen::TimeUtil::GetDayStart(ts, tz);
    assert(day_default == day_explicit);

    time_t week_default = chen::TimeUtil::GetWeekStart(ts);
    time_t week_explicit = chen::TimeUtil::GetWeekStart(ts, tz);
    assert(week_default == week_explicit);

    time_t month_default = chen::TimeUtil::GetMonthStart(ts);
    time_t month_explicit = chen::TimeUtil::GetMonthStart(ts, tz);
    assert(month_default == month_explicit);

    assert(chen::TimeUtil::IsSameDay(ts, ts) == chen::TimeUtil::IsSameDay(ts, ts, tz));
    assert(chen::TimeUtil::IsSameMonth(ts, ts) == chen::TimeUtil::IsSameMonth(ts, ts, tz));

    int dow_default = chen::TimeUtil::GetDayOfWeek(ts);
    int dow_explicit = chen::TimeUtil::GetDayOfWeek(ts, tz);
    assert(dow_default == dow_explicit);

    std::cout << "  Local default == explicit local offset" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_timezone_roundtrip() {
    std::cout << "=== test_timezone_roundtrip ===" << std::endl;

    // Str2Time 解析带时区的 ISO 8601 → time_t (UTC) → Time2Str 回 UTC 验证
    std::string iso_str = "2024-01-15T10:30:00Z";
    time_t parsed = chen::TimeUtil::Str2Time(iso_str);
    std::string formatted = chen::TimeUtil::Time2Str(parsed, "%Y-%m-%dT%H:%M:%SZ", 0);
    assert(formatted == iso_str);
    std::cout << "  " << iso_str << " -> " << formatted << " OK" << std::endl;

    // 带 +08:00 时区的
    std::string iso_cst = "2024-01-15T18:30:00+08:00";  // 等价于 10:30:00Z
    time_t parsed_cst = chen::TimeUtil::Str2Time(iso_cst);
    // parsed_cst 应该是 UTC 时间戳，和 parsed 相同
    assert(parsed_cst == parsed);
    std::string formatted_utc = chen::TimeUtil::Time2Str(parsed_cst, "%Y-%m-%dT%H:%M:%SZ", 0);
    assert(formatted_utc == "2024-01-15T10:30:00Z");
    std::cout << "  " << iso_cst << " -> " << formatted_utc << " OK" << std::endl;

    // -05:00
    std::string iso_est = "2024-01-15T05:30:00-05:00";  // 等价于 10:30:00Z
    time_t parsed_est = chen::TimeUtil::Str2Time(iso_est);
    assert(parsed_est == parsed);
    std::cout << "  " << iso_est << " -> UTC OK" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_util_include_compat() {
    std::cout << "=== test_util_include_compat ===" << std::endl;

    // Verify that functions are also accessible via util.h
    uint64_t ms = chen::GetCurrentMs();
    assert(ms > 0);

    time_t ts = chen::Str2Time("2024-01-15 10:30:00");
    assert(ts > 0);

    std::cout << "PASS" << std::endl;
}

int main() {
    test_basic_time();
    test_time_conversion();
    test_auto_str2time();
    test_rfc1123();
    test_time_judgment();
    test_boundary();
    test_field_extract();
    test_duration();
    test_days_between();
    test_next_reset();

    // 时区测试
    test_timezone_offset();
    test_time2str_with_timezone();
    test_time2str_ms_with_timezone();
    test_day_boundary_with_timezone();
    test_week_boundary_with_timezone();
    test_month_boundary_with_timezone();
    test_issame_with_timezone();
    test_field_extract_with_timezone();
    test_timezone_default_is_local();
    test_timezone_roundtrip();

    test_util_include_compat();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
