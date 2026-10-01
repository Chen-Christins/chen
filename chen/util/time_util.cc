#include "time_util.h"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

#include <sys/time.h>

namespace chen {

// ============================================================================
// 内部辅助：将 struct tm 转换为指定时区
// ============================================================================

/// 根据 tz_offset（UTC 以东秒数）将 UTC 时间戳转为 struct tm
/// 原理：localtime = UTC + offset，直接用 gmtime_r 处理偏移后的时间戳
static void gmtime_tz(const time_t* ts, struct tm* result, int tz_offset) {
    time_t local_ts = *ts + tz_offset;
    gmtime_r(&local_ts, result);
}

/// 根据 tz_offset 将 struct tm 转回 time_t（UTC 时间戳）
/// 原理：先当作 UTC 算出 time_t，再减去 offset
static time_t timegm_tz(struct tm* tm, int tz_offset) {
    // Temporarily set TZ=UTC, call mktime, restore
    char* old_tz = getenv("TZ");
    setenv("TZ", "UTC", 1);
    tzset();
    time_t result = mktime(tm);
    if (old_tz) {
        setenv("TZ", old_tz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return result - tz_offset;
}

// ============================================================================
// 当前时间获取
// ============================================================================

time_t TimeUtil::GetCurrentSec() {
    return time(0);
}

uint64_t TimeUtil::GetCurrentUs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 * 1000ul + tv.tv_usec;
}

uint64_t TimeUtil::GetCurrentMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
}

uint64_t TimeUtil::GetCurrentNanos() {
    auto now = std::chrono::system_clock::now();
    uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return nanoseconds;
}

int TimeUtil::GetTimezoneOffset() {
    time_t t = time(0);
    struct tm local_tm;
    localtime_r(&t, &local_tm);
    // tm_gmtoff 是 glibc 扩展，直接给出 UTC 以东的秒数
    return static_cast<int>(local_tm.tm_gmtoff);
}

std::string TimeUtil::Time2Str(time_t ts, const std::string& format) {
    struct tm tm;
    localtime_r(&ts, &tm);
    char buf[64];
    if (strftime(buf, sizeof(buf), format.c_str(), &tm) == 0) {
        buf[0] = '\0';
    }
    return buf;
}

std::string TimeUtil::Time2Str(uint64_t ts, const std::string& format) {
    time_t sec = static_cast<time_t>(ts / 1000);
    uint64_t ms = ts % 1000;
    struct tm tm;
    localtime_r(&sec, &tm);
    char buf[64];
    if (strftime(buf, sizeof(buf), format.c_str(), &tm) == 0) {
        buf[0] = '\0';
    }
    std::string result(buf);
    size_t pos = result.find("%f");
    if (pos != std::string::npos) {
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill('0') << ms;
        result.replace(pos, 2, oss.str());
    }
    return result;
}

std::string TimeUtil::Time2Str(time_t ts, const std::string& format, int tz_offset) {
    struct tm tm;
    gmtime_tz(&ts, &tm, tz_offset);
    char buf[64];
    if (strftime(buf, sizeof(buf), format.c_str(), &tm) == 0) {
        buf[0] = '\0';
    }
    return buf;
}

std::string TimeUtil::Time2Str(uint64_t ts, const std::string& format, int tz_offset) {
    time_t sec = static_cast<time_t>(ts / 1000);
    uint64_t ms = ts % 1000;
    struct tm tm;
    gmtime_tz(&sec, &tm, tz_offset);
    char buf[64];
    if (strftime(buf, sizeof(buf), format.c_str(), &tm) == 0) {
        buf[0] = '\0';
    }
    std::string result(buf);
    size_t pos = result.find("%f");
    if (pos != std::string::npos) {
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill('0') << ms;
        result.replace(pos, 2, oss.str());
    }
    return result;
}

time_t TimeUtil::Str2Time(const char* str, const char* format) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    if (!strptime(str, format, &t)) {
        return 0;
    }
    return mktime(&t);
}

// timegm replacement: convert struct tm (UTC) to time_t
time_t TimeUtil::utcMktime(struct tm* tm) {
    char* old_tz = getenv("TZ");
    setenv("TZ", "UTC", 1);
    tzset();
    time_t result = mktime(tm);
    if (old_tz) {
        setenv("TZ", old_tz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return result;
}

// Strip fractional seconds while preserving any trailing timezone suffix.
// "2024-01-15T10:30:00.123Z"     -> "2024-01-15T10:30:00Z"
// "2024-01-15T10:30:00.123+08:00" -> "2024-01-15T10:30:00+08:00"
// "2024-01-15T10:30:00.456"       -> "2024-01-15T10:30:00"
std::string TimeUtil::stripFractionalSeconds(const std::string& s) {
    size_t dot = s.find('.');
    if (dot == std::string::npos) {
        return s;
    }
    // Look for the first timezone marker after the dot
    size_t tz = s.find_first_of("Zz+-", dot + 1);
    if (tz != std::string::npos) {
        return s.substr(0, dot) + s.substr(tz);
    }
    return s.substr(0, dot);
}

// Parse an ISO 8601 timezone suffix (+HH:MM, -HH:MM, Z) and return offset in seconds.
// Returns 0 if no suffix found.  On success sets end_pos to the position of the
// timezone sign (so that str.substr(0, end_pos) is the content before the offset).
// Positive for east of UTC, negative for west.
int TimeUtil::parseTimezoneSuffix(const std::string& str, size_t& end_pos) {
    if (str.empty()) {
        return 0;
    }

    size_t len = str.length();
    // Check for 'Z' at end
    if (str.back() == 'Z' || str.back() == 'z') {
        end_pos = len - 1;
        return 0; // Z = UTC, no offset needed
    }

    // Check for +/-HH:MM or +/-HHMM at end
    // Look for + or - in the last 6 characters
    size_t sign_pos = std::string::npos;
    for (size_t i = (len > 6 ? len - 6 : 0); i < len; ++i) {
        if (str[i] == '+' || str[i] == '-') {
            sign_pos = i;
            break;
        }
    }

    if (sign_pos == std::string::npos
        || static_cast<ssize_t>(sign_pos) < static_cast<ssize_t>(len) - 6) {
        return 0;
    }

    int sign = (str[sign_pos] == '+') ? 1 : -1;
    int hour = 0, minute = 0;

    // Helper: check if N chars at offset are all ASCII digits
    auto isDigits = [&](size_t off, size_t n) -> bool {
        if (sign_pos + off + n > len) return false;
        for (size_t k = 0; k < n; ++k) {
            if (!std::isdigit(static_cast<unsigned char>(str[sign_pos + off + k])))
                return false;
        }
        return true;
    };

    // Try +HH:MM format (after sign: 2 digits, colon, 2 digits = 5 chars)
    if (sign_pos + 6 <= len && str[sign_pos + 3] == ':'
            && isDigits(1, 2) && isDigits(4, 2)) {
        hour = (str[sign_pos + 1] - '0') * 10 + (str[sign_pos + 2] - '0');
        minute = (str[sign_pos + 4] - '0') * 10 + (str[sign_pos + 5] - '0');
        end_pos = sign_pos;
        return sign * (hour * 3600 + minute * 60);
    }

    // Try +HHMM format (after sign: 4 digits)
    if (sign_pos + 5 <= len && isDigits(1, 4)) {
        hour = (str[sign_pos + 1] - '0') * 10 + (str[sign_pos + 2] - '0');
        minute = (str[sign_pos + 3] - '0') * 10 + (str[sign_pos + 4] - '0');
        end_pos = sign_pos;
        return sign * (hour * 3600 + minute * 60);
    }

    // Try +HH format (after sign: 2 digits)
    if (sign_pos + 3 <= len && isDigits(1, 2)) {
        hour = (str[sign_pos + 1] - '0') * 10 + (str[sign_pos + 2] - '0');
        end_pos = sign_pos;
        return sign * (hour * 3600);
    }

    return 0;
}

time_t TimeUtil::Str2Time(const std::string& str) {
    if (str.empty()) {
        return 0;
    }

    // Trim whitespace
    std::string s = str;
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);

    if (s.empty()) {
        return 0;
    }

    // ---- ISO 8601 with timezone (Z, +HH:MM, -HH:MM, etc.) ----
    // Try parsing as UTC time with offset adjustment
    {
        // Strip fractional seconds, preserving any trailing timezone suffix
        std::string clean = TimeUtil::stripFractionalSeconds(s);

        size_t content_end = clean.length();
        int tz_offset = TimeUtil::parseTimezoneSuffix(clean, content_end);
        if (content_end < clean.length()) {
            std::string content = clean.substr(0, content_end);

            // Replace T with space for strptime compat
            for (auto& ch : content) {
                if (ch == 'T') ch = ' ';
            }

            struct tm tm;
            memset(&tm, 0, sizeof(tm));

            if (strptime(content.c_str(), "%Y-%m-%d %H:%M:%S", &tm)) {
                time_t utc_time = TimeUtil::utcMktime(&tm);
                // Adjust: if timezone is +08:00, UTC time = local time - 8h
                return utc_time - tz_offset;
            }
        }
    }

    // ---- Standard formats (interpreted as local time) ----

    // "2024-01-15 10:30:00"
    {
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        if (strptime(s.c_str(), "%Y-%m-%d %H:%M:%S", &tm)) {
            return mktime(&tm);
        }
    }

    // "2024-01-15T10:30:00" (ISO 8601 without timezone, replace T with space for strptime compat)
    {
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        std::string s2 = s;
        for (auto& ch : s2) {
            if (ch == 'T') ch = ' ';
        }
        if (strptime(s2.c_str(), "%Y-%m-%d %H:%M:%S", &tm)) {
            return mktime(&tm);
        }
    }

    // "2024-01-15"
    {
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        if (strptime(s.c_str(), "%Y-%m-%d", &tm)) {
            return mktime(&tm);
        }
    }

    // "10:30:00"
    {
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        time_t now = time(0);
        localtime_r(&now, &tm);
        struct tm time_only;
        memset(&time_only, 0, sizeof(time_only));
        if (strptime(s.c_str(), "%H:%M:%S", &time_only)) {
            tm.tm_hour = time_only.tm_hour;
            tm.tm_min  = time_only.tm_min;
            tm.tm_sec  = time_only.tm_sec;
            return mktime(&tm);
        }
    }

    return 0;
}

std::string TimeUtil::GetRFC1123Time(time_t ts) {
    struct tm tm;
    gmtime_r(&ts, &tm);
    char buf[64];
    // RFC 1123 format: "Thu, 01 Dec 2022 16:00:00 GMT"
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return buf;
}

bool TimeUtil::IsZeroOfDay(uint64_t ts, uint64_t day_ms) {
    return (ts % day_ms) < 1000;
}

bool TimeUtil::IsZeroOfWeek(uint64_t ts) {
    time_t t = static_cast<time_t>(ts);
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_wday == 1
        && tm.tm_hour == 0
        && tm.tm_min == 0
        && tm.tm_sec < 1;
}

bool TimeUtil::IsZeroOfMonth(uint64_t ts) {
    time_t t = static_cast<time_t>(ts);
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_mday == 1
        && tm.tm_hour == 0
        && tm.tm_min == 0
        && tm.tm_sec < 1;
}

bool TimeUtil::IsSameDay(time_t t1, time_t t2, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm1, tm2;
    gmtime_tz(&t1, &tm1, tz);
    gmtime_tz(&t2, &tm2, tz);
    return tm1.tm_year == tm2.tm_year
        && tm1.tm_mon  == tm2.tm_mon
        && tm1.tm_mday == tm2.tm_mday;
}

bool TimeUtil::IsSameWeek(time_t t1, time_t t2, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    return GetWeekStart(t1, tz) == GetWeekStart(t2, tz);
}

bool TimeUtil::IsSameMonth(time_t t1, time_t t2, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm1, tm2;
    gmtime_tz(&t1, &tm1, tz);
    gmtime_tz(&t2, &tm2, tz);
    return tm1.tm_year == tm2.tm_year
        && tm1.tm_mon  == tm2.tm_mon;
}

bool TimeUtil::IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

bool TimeUtil::IsExpired(uint64_t start_ms, uint64_t ttl_ms) {
    return TimeUtil::GetCurrentMs() >= start_ms + ttl_ms;
}

time_t TimeUtil::GetDayStart(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    return timegm_tz(&tm, tz);
}

time_t TimeUtil::GetDayEnd(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    tm.tm_hour = 23;
    tm.tm_min  = 59;
    tm.tm_sec  = 59;
    return timegm_tz(&tm, tz);
}

time_t TimeUtil::GetWeekStart(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    // tm_wday: 0=Sunday, convert to Mon=0..Sun=6
    int days_from_monday = (tm.tm_wday == 0) ? 6 : (tm.tm_wday - 1);
    time_t monday = ts - days_from_monday * 86400;
    struct tm tm_monday;
    gmtime_tz(&monday, &tm_monday, tz);
    tm_monday.tm_hour = 0;
    tm_monday.tm_min  = 0;
    tm_monday.tm_sec  = 0;
    return timegm_tz(&tm_monday, tz);
}

time_t TimeUtil::GetWeekEnd(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    int days_from_monday = (tm.tm_wday == 0) ? 6 : (tm.tm_wday - 1);
    int days_to_sunday = 6 - days_from_monday;
    time_t sunday = ts + days_to_sunday * 86400;
    struct tm tm_sunday;
    gmtime_tz(&sunday, &tm_sunday, tz);
    tm_sunday.tm_hour = 23;
    tm_sunday.tm_min  = 59;
    tm_sunday.tm_sec  = 59;
    return timegm_tz(&tm_sunday, tz);
}

time_t TimeUtil::GetMonthStart(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    tm.tm_mday = 1;
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    return timegm_tz(&tm, tz);
}

time_t TimeUtil::GetMonthEnd(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    // Go to first day of next month, then subtract 1 second
    if (tm.tm_mon == 11) {
        tm.tm_year += 1;
        tm.tm_mon = 0;
    } else {
        tm.tm_mon += 1;
    }
    tm.tm_mday = 1;
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    time_t next_month_start = timegm_tz(&tm, tz);
    return next_month_start - 1;
}

uint64_t TimeUtil::GetNextResetTime(uint64_t base_ts, uint64_t interval_ms) {
    uint64_t now_ms = TimeUtil::GetCurrentMs();
    if (now_ms <= base_ts) {
        return base_ts;
    }
    uint64_t elapsed = now_ms - base_ts;
    uint64_t periods = elapsed / interval_ms;
    if (elapsed % interval_ms == 0) {
        return now_ms;
    }
    return base_ts + (periods + 1) * interval_ms;
}

int TimeUtil::GetDayOfWeek(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    return tm.tm_wday;
}

int TimeUtil::GetDayOfYear(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    return tm.tm_yday + 1;
}

int TimeUtil::GetSecondsSinceMidnight(time_t ts, int tz_offset) {
    int tz = (tz_offset == INT32_MAX) ? GetTimezoneOffset() : tz_offset;
    struct tm tm;
    gmtime_tz(&ts, &tm, tz);
    return tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
}

int TimeUtil::DaysInMonth(int year, int month) {
    static const int days_per_month[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && TimeUtil::IsLeapYear(year)) {
        return 29;
    }
    return days_per_month[month - 1];
}

int64_t TimeUtil::DaysBetween(time_t t1, time_t t2) {
    time_t start = TimeUtil::GetDayStart(t1);
    time_t end   = TimeUtil::GetDayStart(t2);
    int64_t diff = static_cast<int64_t>(end - start);
    if (diff < 0) {
        diff = -diff;
    }
    return diff / 86400;
}

uint64_t TimeUtil::GetRemainingMs(uint64_t start_ms, uint64_t ttl_ms) {
    uint64_t deadline = start_ms + ttl_ms;
    uint64_t now_ms = TimeUtil::GetCurrentMs();
    if (now_ms >= deadline) {
        return 0;
    }
    return deadline - now_ms;
}

std::string TimeUtil::FormatDuration(uint64_t ms) {
    uint64_t total_sec = ms / 1000;
    if (total_sec == 0) {
        return "0s";
    }

    uint64_t days    = total_sec / 86400;
    uint64_t hours   = (total_sec % 86400) / 3600;
    uint64_t minutes = (total_sec % 3600) / 60;
    uint64_t seconds = total_sec % 60;

    std::ostringstream oss;
    if (days > 0) {
        oss << days << "d ";
    }
    if (hours > 0) {
        oss << hours << "h ";
    }
    if (minutes > 0) {
        oss << minutes << "m ";
    }
    if (seconds > 0 || (days == 0 && hours == 0 && minutes == 0)) {
        oss << seconds << "s";
    }

    std::string result = oss.str();
    // Remove trailing space if present
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

} // namespace chen
