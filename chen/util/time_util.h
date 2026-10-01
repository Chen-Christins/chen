/**
 * @file time_util.h
 * @brief 时间工具函数（互联网和游戏常用）
 * @author Christins
 * @date 2026-06-26
 */
#pragma once

#include <cstdint>
#include <string>

#include <time.h>

namespace chen {

/**
 * @brief 时间工具类
 */
class TimeUtil {
public:
    /**
     * @brief 获取当前时间的微秒
     * @return uint64_t 微秒时间戳
     */
    static uint64_t GetCurrentUs();

    /**
     * @brief 获取当前时间的毫秒
     * @return uint64_t 毫秒时间戳
     */
    static uint64_t GetCurrentMs();

    /**
     * @brief 获取当前时间的纳秒
     * @return uint64_t 纳秒时间戳
     */
    static uint64_t GetCurrentNanos();

    /**
     * @brief 获取当前时间的秒
     * @return time_t 秒时间戳
     */
    static time_t GetCurrentSec();

    /**
     * @brief 将时间转换为字符串（秒级，默认使用本地时区）
     * @param ts 秒时间戳, 默认为当前时间
     * @param format 格式字符串, 默认为 "%Y-%m-%d %H:%M:%S"
     * @return std::string 格式化后的时间字符串
     */
    static std::string Time2Str(time_t ts = time(0), const std::string& format = "%Y-%m-%d %H:%M:%S");

    /**
     * @brief 将时间转换为字符串（毫秒级，默认使用本地时区，支持 %f 占位符替换为毫秒）
     * @param ts 毫秒时间戳
     * @param format 格式字符串, 默认为 "%Y-%m-%d %H:%M:%S.%f"
     * @return std::string 格式化后的时间字符串
     */
    static std::string Time2Str(uint64_t ts, const std::string& format = "%Y-%m-%d %H:%M:%S.%f");

    /**
     * @brief 将时间转换为字符串（指定时区偏移）
     * @param ts 秒时间戳 (UTC)
     * @param format 格式字符串
     * @param tz_offset 时区偏移秒数（UTC 以东为正），如 UTC=0, CST=28800, EST=-18000
     * @return std::string 格式化后的时间字符串
     */
    static std::string Time2Str(time_t ts, const std::string& format, int tz_offset);

    /**
     * @brief 将时间转换为字符串（毫秒级，指定时区偏移）
     * @param ts 毫秒时间戳 (UTC)
     * @param format 格式字符串
     * @param tz_offset 时区偏移秒数（UTC 以东为正）
     * @return std::string 格式化后的时间字符串
     */
    static std::string Time2Str(uint64_t ts, const std::string& format, int tz_offset);

    /**
     * @brief 将字符串转换为时间戳
     * @param str 时间字符串
     * @param format 格式字符串
     * @return time_t 秒时间戳, 失败返回 0
     */
    static time_t Str2Time(const char* str, const char* format);

    /**
     * @brief 将字符串转换为时间戳（自动检测常用格式）
     * @details 支持以下格式自动检测:
     *          - "2024-01-15 10:30:00"         标准格式
     *          - "2024-01-15T10:30:00"         ISO 8601
     *          - "2024-01-15T10:30:00Z"        ISO 8601 UTC
     *          - "2024-01-15T10:30:00+08:00"  ISO 8601 带时区
     *          - "2024-01-15T10:30:00.000Z"   ISO 8601 带毫秒 UTC
     *          - "2024-01-15"                  仅日期
     *          - "10:30:00"                    仅时间
     * @param str 时间字符串
     * @return time_t 秒时间戳, 无法识别返回 0
     */
    static time_t Str2Time(const std::string& str);

    /**
     * @brief 获取 RFC1123 格式的 HTTP 时间字符串
     * @param ts 秒时间戳, 默认为当前时间
     * @return std::string "Thu, 01 Dec 2022 16:00:00 GMT" 格式
     */
    static std::string GetRFC1123Time(time_t ts = time(0));

    /**
     * @brief 毫秒时间戳转秒时间戳
     */
    static inline time_t UnixMsToSec(uint64_t ms) { return static_cast<time_t>(ms / 1000); }

    /**
     * @brief 秒时间戳转毫秒时间戳
     */
    static inline uint64_t UnixSecToMs(time_t s) { return static_cast<uint64_t>(s) * 1000; }

    /**
     * @brief 获取当前系统时区偏移（UTC 以东秒数）
     * @return int 时区偏移，如 CST +08:00 返回 28800, EST -05:00 返回 -18000
     */
    static int GetTimezoneOffset();

    /**
     * @brief 判断时间戳是否是一天的开始（凌晨 0 点，允许 1 秒误差）
     * @param ts 时间戳（毫秒或秒均可）, 默认为当前秒时间戳
     * @param day_ms 一天的毫秒数，默认 24 小时
     * @return bool
     */
    static bool IsZeroOfDay(uint64_t ts = time(0), uint64_t day_ms = 24 * 3600 * 1000);

    /**
     * @brief 判断时间戳是否是一个周的开始（周一凌晨，允许 1 秒误差）
     * @param ts 秒时间戳, 默认为当前时间
     * @return bool
     */
    static bool IsZeroOfWeek(uint64_t ts = time(0));

    /**
     * @brief 判断时间戳是否是一个月的开始（1 号凌晨，允许 1 秒误差）
     * @param ts 秒时间戳, 默认为当前时间
     * @return bool
     */
    static bool IsZeroOfMonth(uint64_t ts = time(0));

    /**
     * @brief 判断两个时间戳是否在同一天（默认本地时区）
     * @param t1 秒时间戳
     * @param t2 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return bool
     */
    static bool IsSameDay(time_t t1, time_t t2, int tz_offset = INT32_MAX);

    /**
     * @brief 判断两个时间戳是否在同一周（默认本地时区，周一为一周开始）
     * @param t1 秒时间戳
     * @param t2 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return bool
     */
    static bool IsSameWeek(time_t t1, time_t t2, int tz_offset = INT32_MAX);

    /**
     * @brief 判断两个时间戳是否在同一个月（默认本地时区）
     * @param t1 秒时间戳
     * @param t2 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return bool
     */
    static bool IsSameMonth(time_t t1, time_t t2, int tz_offset = INT32_MAX);

    /**
     * @brief 判断是否为闰年
     * @param year 年份（如 2024）
     * @return bool
     */
    static bool IsLeapYear(int year);

    /**
     * @brief 判断指定的毫秒时间戳是否已过期（当前时间 >= start_ms + ttl_ms）
     * @param start_ms 起始毫秒时间戳
     * @param ttl_ms   TTL 毫秒数
     * @return bool 是否已过期
     */
    static bool IsExpired(uint64_t start_ms, uint64_t ttl_ms);

    /**
     * @brief 获取指定时间戳所在日的 00:00:00（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return time_t 当日 0 点的秒时间戳
     */
    static time_t GetDayStart(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取指定时间戳所在日的 23:59:59（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return time_t 当日最后一秒的秒时间戳
     */
    static time_t GetDayEnd(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取指定时间戳所在周的周一 00:00:00（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return time_t 周一 0 点的秒时间戳
     */
    static time_t GetWeekStart(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取指定时间戳所在周的周日 23:59:59（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return time_t 周日最后一秒的秒时间戳
     */
    static time_t GetWeekEnd(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取指定时间戳所在月的 1 号 00:00:00（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return time_t 当月 1 号 0 点的秒时间戳
     */
    static time_t GetMonthStart(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取指定时间戳所在月的最后一天 23:59:59（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return time_t 当月最后一秒的秒时间戳
     */
    static time_t GetMonthEnd(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取下一个周期重置时间（如每日/每周刷新）
     * @param base_ts 基准时间戳秒
     * @param interval_ms 周期毫秒数
     * @return uint64_t 下一个重置点的毫秒时间戳
     */
    static uint64_t GetNextResetTime(uint64_t base_ts, uint64_t interval_ms);

    /**
     * @brief 获取星期几（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return int 0=Sunday, 1=Monday, ..., 6=Saturday
     */
    static int GetDayOfWeek(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取一年中的第几天（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return int 1-366
     */
    static int GetDayOfYear(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取当天已过去的秒数（默认本地时区）
     * @param ts 秒时间戳
     * @param tz_offset 时区偏移秒数，默认 INT_MAX 表示使用本地时区
     * @return int 0-86399
     */
    static int GetSecondsSinceMidnight(time_t ts, int tz_offset = INT32_MAX);

    /**
     * @brief 获取指定月份的天数
     * @param year 年份
     * @param month 月份 (1-12)
     * @return int 该月的天数
     */
    static int DaysInMonth(int year, int month);

    /**
     * @brief 计算两个时间戳相差的天数（绝对值）
     * @param t1 秒时间戳
     * @param t2 秒时间戳
     * @return int64_t 天数差
     */
    static int64_t DaysBetween(time_t t1, time_t t2);

    /**
     * @brief 获取距离截止时间的剩余毫秒数
     * @param start_ms 起始毫秒时间戳
     * @param ttl_ms   TTL 毫秒数
     * @return uint64_t 剩余毫秒数（已过期返回 0）
     */
    static uint64_t GetRemainingMs(uint64_t start_ms, uint64_t ttl_ms);

    /**
     * @brief 将毫秒时长格式化为人类可读字符串
     * @param ms 毫秒数
     * @return std::string 如 "2d 15h 30m 15s" / "45m 30s" / "5s" / "0s"
     */
    static std::string FormatDuration(uint64_t ms);

private:
    /**
     * @brief 将 struct tm（UTC）转换为 time_t
     */
    static time_t utcMktime(struct tm* tm);

    /**
     * @brief 去掉毫秒部分，保留尾部时区后缀
     *        "2024-01-15T10:30:00.123Z" -> "2024-01-15T10:30:00Z"
     */
    static std::string stripFractionalSeconds(const std::string& s);

    /**
     * @brief 解析 ISO 8601 时区后缀（+HH:MM, -HH:MM, Z）
     * @return 偏移秒数（东正西负），无后缀返回 0
     * @param[out] end_pos 时区符号位置，可用于 substr 截取内容
     */
    static int parseTimezoneSuffix(const std::string& str, size_t& end_pos);
};

inline uint64_t GetCurrentUs() {
    return TimeUtil::GetCurrentUs();
}

inline uint64_t GetCurrentMs() {
    return TimeUtil::GetCurrentMs(); 
}

inline uint64_t GetCurrentNanos() {
    return TimeUtil::GetCurrentNanos(); 
}

inline time_t GetCurrentSec() {
    return TimeUtil::GetCurrentSec(); 
}

inline std::string Time2Str(time_t ts = time(0), const std::string& format = "%Y-%m-%d %H:%M:%S") {
    return TimeUtil::Time2Str(ts, format); 
}

inline std::string Time2Str(uint64_t ts, const std::string& format = "%Y-%m-%d %H:%M:%S.%f") {
    return TimeUtil::Time2Str(ts, format); 
}

inline time_t Str2Time(const char* str, const char* format) {
    return TimeUtil::Str2Time(str, format); 
}

inline time_t Str2Time(const std::string& str) {
    return TimeUtil::Str2Time(str); 
}

inline std::string GetRFC1123Time(time_t ts = time(0)) {
    return TimeUtil::GetRFC1123Time(ts); 
}

inline time_t UnixMsToSec(uint64_t ms) {
    return TimeUtil::UnixMsToSec(ms); 
}

inline uint64_t UnixSecToMs(time_t s) {
    return TimeUtil::UnixSecToMs(s); 
}

inline bool IsZeroOfDay(uint64_t ts = time(0), uint64_t day_ms = 24 * 3600 * 1000) {
    return TimeUtil::IsZeroOfDay(ts, day_ms); 
}

inline bool IsZeroOfWeek(uint64_t ts = time(0)) {
    return TimeUtil::IsZeroOfWeek(ts); 
}

inline bool IsZeroOfMonth(uint64_t ts = time(0)) {
    return TimeUtil::IsZeroOfMonth(ts); 
}

inline bool IsSameDay(time_t t1, time_t t2) {
    return TimeUtil::IsSameDay(t1, t2); 
}

inline bool IsSameWeek(time_t t1, time_t t2) {
    return TimeUtil::IsSameWeek(t1, t2); 
}

inline bool IsSameMonth(time_t t1, time_t t2) {
    return TimeUtil::IsSameMonth(t1, t2); 
}

inline bool IsLeapYear(int year) {
    return TimeUtil::IsLeapYear(year); 
}

inline bool IsExpired(uint64_t start_ms, uint64_t ttl_ms) {
    return TimeUtil::IsExpired(start_ms, ttl_ms); 
}

inline time_t GetDayStart(time_t ts) {
    return TimeUtil::GetDayStart(ts); 
}

inline time_t GetDayEnd(time_t ts) {
    return TimeUtil::GetDayEnd(ts); 
}

inline time_t GetWeekStart(time_t ts) {
    return TimeUtil::GetWeekStart(ts); 
}

inline time_t GetWeekEnd(time_t ts) {
    return TimeUtil::GetWeekEnd(ts); 
}

inline time_t GetMonthStart(time_t ts) {
    return TimeUtil::GetMonthStart(ts); 
}

inline time_t GetMonthEnd(time_t ts) {
    return TimeUtil::GetMonthEnd(ts); 
}

inline uint64_t GetNextResetTime(uint64_t base_ts, uint64_t interval_ms) {
    return TimeUtil::GetNextResetTime(base_ts, interval_ms); 
}

inline int GetDayOfWeek(time_t ts) {
    return TimeUtil::GetDayOfWeek(ts);
}

inline int GetDayOfYear(time_t ts) {
    return TimeUtil::GetDayOfYear(ts);
}

inline int GetSecondsSinceMidnight(time_t ts) {
    return TimeUtil::GetSecondsSinceMidnight(ts);
}

inline int DaysInMonth(int year, int month) {
    return TimeUtil::DaysInMonth(year, month);
}

inline int64_t DaysBetween(time_t t1, time_t t2) {
    return TimeUtil::DaysBetween(t1, t2);
}

inline uint64_t GetRemainingMs(uint64_t start_ms, uint64_t ttl_ms) {
    return TimeUtil::GetRemainingMs(start_ms, ttl_ms);
}

inline std::string FormatDuration(uint64_t ms) {
    return TimeUtil::FormatDuration(ms);
}

} // namespace chen
