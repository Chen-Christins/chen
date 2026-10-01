#include "log.h"

#include <cctype>
#include <functional>
#include <iomanip>
#include <iostream>
#include <unordered_map>

#include "../config/config.h"

namespace chen {

// LogEvent
const char* LogLevel::toString(LogLevel::Level level) {
    switch (level) {
#define XX(name)         \
    case LogLevel::name: \
        return #name;    \
        break;

        XX(TRACE);
        XX(DEBUG);
        XX(INFO);
        XX(WARN);
        XX(ERROR);
        XX(FATAL);
#undef XX
    default:
        return "UNKNOW";
    }
    return "UNKNOW";
}

LogLevel::Level LogLevel::FromString(const std::string& str) {
#define XX(level, v)            \
    if (str == #v) {            \
        return LogLevel::level; \
    }
    XX(TRACE, trace);
    XX(DEBUG, debug);
    XX(INFO, info);
    XX(WARN, warn);
    XX(ERROR, error);
    XX(FATAL, fatal);

    XX(TRACE, TRACE);
    XX(DEBUG, DEBUG);
    XX(INFO, INFO);
    XX(WARN, WARN);
    XX(ERROR, ERROR);
    XX(FATAL, FATAL);

    return LogLevel::UNKNOW;
#undef XX
}

// Logger
Logger::Logger(const std::string& name)
        : m_name(name), m_level(LogLevel::TRACE) {
    m_formatter.reset(new LogFormatter("%d{%Y-%m-%d %H:%M:%S.%f}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
}

void Logger::log(LogLevel::Level level, std::shared_ptr<LogEvent> event) {
    if (level >= m_level) {
        auto self = shared_from_this();
        MutexType::Lock lock(m_mutex);
        if (!m_appenders.empty()) {
            for (auto& i : m_appenders) {
                i->log(self, level, event);
            }
        } else if (m_root) {
            m_root->log(level, event);
        }
    }
}

void Logger::trace(std::shared_ptr<LogEvent> event) {
    log(LogLevel::TRACE, event);
}

void Logger::debug(std::shared_ptr<LogEvent> event) {
    log(LogLevel::DEBUG, event);
}

void Logger::info(std::shared_ptr<LogEvent> event) {
    log(LogLevel::INFO, event);
}

void Logger::warn(std::shared_ptr<LogEvent> event) {
    log(LogLevel::WARN, event);
}

void Logger::error(std::shared_ptr<LogEvent> event) {
    log(LogLevel::ERROR, event);
}

void Logger::fatal(std::shared_ptr<LogEvent> event) {
    log(LogLevel::FATAL, event);
}

void Logger::addAppender(std::shared_ptr<LogAppender> appender) {
    MutexType::Lock lock(m_mutex);
    if (!appender->getFormatter()) {
        MutexType::Lock lock2(appender->m_mutex);
        appender->m_formatter = m_formatter;
    }
    m_appenders.push_back(appender);
}

void Logger::delAppender(std::shared_ptr<LogAppender> appender) {
    MutexType::Lock lock(m_mutex);
    for (auto it = m_appenders.begin(); it != m_appenders.end(); ++it) {
        if (*it == appender) {
            m_appenders.erase(it);
            break;
        }
    }
}

void Logger::setFormatter(std::shared_ptr<LogFormatter> val) {
    MutexType::Lock lock(m_mutex);
    m_formatter = val;
    for (auto& i : m_appenders) {
        MutexType::Lock lock2(i->m_mutex);
        if (!i->m_hasFormatter) {
            i->m_formatter = m_formatter;
        }
    }
}

void Logger::setFormatter(const std::string& val) {
    LogFormatter::ptr pattern(new LogFormatter(val));
    if (pattern->isError()) {
        std::cout << "Logger setFormatter name=" << m_name
            << " value=" << val << " invalid formatter"
            << std::endl;
        return ;
    }
    setFormatter(pattern);
}

void Logger::clearAppenders() {
    MutexType::Lock lock(m_mutex);
    m_appenders.clear();
}


std::string Logger::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["name"] = m_name;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::toString(m_level);
    }
    if (m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    for (auto& i : m_appenders) {
        node["appender"].push_back(YAML::Load(i->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

std::shared_ptr<LogFormatter> Logger::getFormatter() { 
    MutexType::Lock lock(m_mutex);
    return m_formatter;
}

// LogEvent
LogEvent::LogEvent(Logger::ptr logger, LogLevel::Level level
        , const char* file, int32_t line, uint32_t elapse
        , uint32_t threadId, uint32_t fiberId, uint64_t time
        , const std::string& threadName)
    : m_logger(logger)
    , m_level(level)
    , m_file(file)
    , m_line(line)
    , m_elapse(elapse)
    , m_threadId(threadId)
    , m_fiberId(fiberId)
    , m_time(time)
    , m_threadName(threadName) {
}


// LogFormatter
LogFormatter::LogFormatter(const std::string& pattern)
    :m_pattern(pattern) {
    init();
}

class MessageFormatItem : public LogFormatter::FormatItem {
public:
    MessageFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        std::stringbuf* sb = event->getContent();
        sb->pubseekpos(0, std::ios_base::in);
        os << sb;
    }
};

class LevelFormatItem : public LogFormatter::FormatItem {
public:
    LevelFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << LogLevel::toString(level);
    }
};

class ElapseFormatItem : public LogFormatter::FormatItem {
public:
    ElapseFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getElapse();
    }
};

class NameFormatItem : public LogFormatter::FormatItem {
public:
    NameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getLogger()->getName();
    }
};

class ThreadIdFormatItem : public LogFormatter::FormatItem {
public:
    ThreadIdFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getThreadId();
    }
};

class FiberIdFormatItem : public LogFormatter::FormatItem {
public:
    FiberIdFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getFiberId();
    }
};

class ThreadNameFormatItem : public LogFormatter::FormatItem {
public:
    ThreadNameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getThreadName();
    }
};

class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
    DateTimeFormatItem(const std::string format = "%Y-%m-%d %H:%M:%S")
        :m_format(format) {
        if (m_format.empty()) {
            m_format = "%Y-%m-%d %H:%M:%S";
        }
    }
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        struct tm tm;
        time_t time = event->getTimeSec();
        localtime_r(&time, &tm);

        // 检查是否包含 %f 格式
        std::string format = m_format;
        size_t pos = format.find("%f");

        if (pos != std::string::npos) {
            // 分离格式字符串：处理 %f 前后的部分
            std::string before = format.substr(0, pos);
            std::string after = format.substr(pos + 2);

            // 格式化 %f 前的部分
            char buf[64];
            if (!before.empty()) {
                strftime(buf, sizeof(buf), before.c_str(), &tm);
                os << buf;
            }

            // 输出微秒（6位数字，前面补零）
            os << std::setfill('0') << std::setw(6) << event->getTimeUs();

            // 格式化 %f 后的部分
            if (!after.empty()) {
                strftime(buf, sizeof(buf), after.c_str(), &tm);
                os << buf;
            }
        } else {
            // 没有 %f，直接格式化
            char buf[64];
            strftime(buf, sizeof(buf), format.c_str(), &tm);
            os << buf;
        }
    }
private:
    std::string m_format;
};

class FileNameFormatItem : public LogFormatter::FormatItem {
public:
    FileNameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getFile();
    }
};

class LineFormatItem : public LogFormatter::FormatItem {
public:
    LineFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getLine();
    }
};

class NewLineFormatItem : public LogFormatter::FormatItem {
public:
    NewLineFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << std::endl;
    }
};

class StringFormatItem : public LogFormatter::FormatItem {
public:
    StringFormatItem(const std::string& str = "") : m_string(str) {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << m_string;
    }
private:
    std::string m_string;
};

class TabFormatItem : public LogFormatter::FormatItem {
public:
    TabFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << "\t";
    }
};

// %d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n
void LogFormatter::init() {
    //str, format, type
    std::vector<std::tuple<std::string, std::string, int>> vec;
    std::string format = "";

    bool vis = (m_pattern.find("{") != std::string::npos ? true : false);
    for (size_t i = 0; i < m_pattern.size(); ++i) {
        size_t j = i;
        while (j < m_pattern.size() && m_pattern[j] == '%') {
            ++j;
        }
        i = j;
        std::string str = m_pattern.substr(j, 1);
        if (str == "d" && vis && m_pattern[j + 1] == '{') {
            size_t n = j + 2, len = 0;
            while (n < m_pattern.size() && m_pattern[n] != '}') {
                ++n;
                ++len;
            }
            format = m_pattern.substr(j + 2, len);
            i = n;
        }
        int type = !!std::isalpha(str[0]);
        vec.push_back(std::make_tuple(str, format, type));
        if (!format.empty()) {
            format.clear();
        }
    }

    static std::unordered_map<std::string, std::function<FormatItem::ptr(const std::string& str)>> 
        format_items = {
#define XX(str, C) \
        { #str, [](const std::string& fmt) { return FormatItem::ptr(new C(fmt)); } }
        XX(m, MessageFormatItem),
        XX(p, LevelFormatItem),
        XX(t, ThreadIdFormatItem),
        XX(N, ThreadNameFormatItem),
        XX(c, NameFormatItem),
        XX(F, FiberIdFormatItem),
        XX(d, DateTimeFormatItem),
        XX(f, FileNameFormatItem),
        XX(l, LineFormatItem),
        XX(T, TabFormatItem),
        XX(n, NewLineFormatItem),
        XX(r, ElapseFormatItem),
#undef XX
    };

    for (auto& [str, fmt, type] : vec) {
        if (!type) {
            m_items.emplace_back(FormatItem::ptr(new StringFormatItem(str)));
        } else {
            auto it = format_items.find(str);
            if (it == format_items.end()) {
                m_items.emplace_back(FormatItem::ptr(new StringFormatItem("<<error_format %" + str + ">>")));
                m_error = true;
            } else {
                m_items.emplace_back(it->second(fmt));
            }
        }
    }
}

std::string LogFormatter::format(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    std::stringstream ss;
    for (auto& i : m_items) {
        i->format(ss, logger, level, event);
    }
    return ss.str();
}

std::ostream& LogFormatter::format(std::ostream& ofs, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    for (auto& i : m_items) {
        i->format(ofs, logger, level, event);
    }
    return ofs;
}

// LogAppender
LogFormatter::ptr LogAppender::getFormatter() {
    MutexType::Lock lock(m_mutex);
    return m_formatter;
}

void LogAppender::setFormatter(LogFormatter::ptr val) {
    MutexType::Lock lock(m_mutex);
    m_formatter = val;
    if (m_formatter) {
        m_hasFormatter = true;
    } else {
        m_hasFormatter = false;
    }
}

// StdoutLogAppender
void StdoutLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (level >= m_level) {
        MutexType::Lock lock(m_mutex);
        // 获取日志级别对应的颜色
        const char* color = "";
        switch(level) {
        case LogLevel::TRACE: 
            color = "\033[1;36m"; // 青色
            break;
        case LogLevel::DEBUG:
            color = "\033[1;34m"; // 蓝色
            break;
        case LogLevel::INFO:  
            color = "\033[1;32m"; // 绿色
            break;
        case LogLevel::WARN:
            color = "\033[1;33m"; // 黄色
            break; 
        case LogLevel::ERROR:
            color = "\033[1;31m"; // 红色
            break;
        case LogLevel::FATAL:
            color = "\033[1;35m"; // 洋红
            break;
        default:
            break;
        }
        std::stringstream ss;
        ss << color;
        m_formatter->format(ss, logger, level, event);
        ss << "\033[0m";
        std::cout << ss.str() << std::flush;
    }
}

std::string StdoutLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::toString(m_level);
    }
    if (m_hasFormatter && m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}


// FileLogAppender

namespace {

/**
 * @brief 在文件名扩展名前插入分片序号，序号为0时返回原文件名
 * @param filename 基础文件名
 * @param index 分片序号
 * @return std::string
 */
std::string AppendShardIndex(const std::string& filename, uint64_t index) {
    if (index == 0) {
        return filename;
    }
    std::string suffix = "_" + std::to_string(index);
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        return filename.substr(0, dot_pos) + suffix + filename.substr(dot_pos);
    }
    return filename + suffix;
}

} // namespace

FileLogAppender::FileLogAppender(const std::string& filename, TimeRotateType rotate_type, int retention_days, uint64_t max_size)
        : m_filename(filename), m_rotateType(rotate_type), m_retentionDays(retention_days), m_maxSize(max_size) {
    if (m_rotateType != NONE) {
        // 如果设置了时间分文件，立即生成当前时间的文件名
        m_periodFileName = getNewFileName(time(0));
    } else {
        m_periodFileName = m_filename;
    }
    openFitFile();
}

void FileLogAppender::openFitFile() {
    uint64_t index = 0;
    std::string name = m_periodFileName;
    if (m_maxSize > 0) {
        // 从基础名开始，跳过已写满的分片文件
        while (true) {
            int64_t size = FSUtil::FileSize(name);
            if (size < 0) {
                size = 0;
            }
            if (static_cast<uint64_t>(size) < m_maxSize) {
                break;
            }
            ++index;
            name = AppendShardIndex(m_periodFileName, index);
        }
    }
    MutexType::Lock lock(m_mutex);
    if (m_filestream) {
        m_filestream.close();
    }
    FSUtil::OpenForWrite(m_filestream, name, std::ios::app);
    m_lastFileName = name;
    m_shardIndex = index;
    int64_t size = FSUtil::FileSize(name);
    m_currentSize = (size > 0) ? static_cast<uint64_t>(size) : 0;
}

void FileLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (level >= m_level) {
        uint64_t now = event->getTimeSec();
        if (now >= (m_lastTime + 3)) {
            if (m_rotateType != NONE) {
                if (shouldRotate(now)) {
                    std::string newFileName = getNewFileName(now);
                    if (m_periodFileName != newFileName) {
                        m_periodFileName = newFileName;
                        openFitFile();
                        cleanOldFiles();
                    }
                }
            } else {
                reopen();
            }
            m_lastTime = now;
        }

        // 大小分片：每次写入前检查，超限则切换到下一个分片
        if (shouldRotateBySize()) {
            openFitFile();
        }

        MutexType::Lock lock(m_mutex);
        std::streampos before = m_filestream.tellp();
        if (!m_formatter->format(m_filestream, logger, level, event)) {
            std::cout << "error" << std::endl;
        } else {
            std::streampos after = m_filestream.tellp();
            if (before >= 0 && after >= before) {
                m_currentSize += static_cast<uint64_t>(after - before);
            }
        }
    }
}

bool FileLogAppender::reopen() {
    MutexType::Lock lock(m_mutex);
    if (m_filestream) {
        m_filestream.close();
    }
    std::string name = m_lastFileName.empty() ? m_filename : m_lastFileName;
    if (!FSUtil::OpenForWrite(m_filestream, name, std::ios::app)) {
        return false;
    }
    int64_t size = FSUtil::FileSize(name);
    m_currentSize = (size > 0) ? static_cast<uint64_t>(size) : 0;
    return true;
}

std::string FileLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "FileLogAppender";
    node["file"] = m_filename;
    if (m_rotateType != NONE) {
        node["time_rotate"] = TimeRotateTypeToString(m_rotateType);
    }
    if (m_retentionDays > 0) {
        node["retention_days"] = m_retentionDays;
    }
    if (m_maxSize > 0) {
        node["max_size"] = m_maxSize;
    }
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::toString(m_level);
    }
    if (m_hasFormatter && m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

const char* FileLogAppender::TimeRotateTypeToString(TimeRotateType type) {
    switch (type) {
    case NONE:
        return "none";
    case MINUTE_30:
        return "30min";
    case HOUR:
        return "hour";
    case HOUR_12:
        return "12hour";
    case DAY:
        return "day";
    case WEEK:
        return "week";
    case MONTH:
        return "month";
    default:
        return "none";
    }
}

FileLogAppender::TimeRotateType FileLogAppender::StringToTimeRotateType(const std::string& str) {
#define XX(val, type) \
    if (str == val) { \
        return type;  \
    }
    XX("none", NONE);
    XX("30min", MINUTE_30);
    XX("hour", HOUR);
    XX("12hour", HOUR_12);
    XX("day", DAY);
    XX("week", WEEK);
    XX("month", MONTH);
#undef XX

    return NONE;
}

std::string FileLogAppender::getNewFileName(uint64_t timestamp) {
    struct tm tm;
    time_t time = timestamp;
    localtime_r(&time, &tm);

    std::string pattern = "%Y-%m-%d";

    switch (m_rotateType) {
    case NONE:
        return m_filename;
    case MINUTE_30:
        pattern = "%Y-%m-%d_%H-%M";
        // 将分钟对齐到30分钟边界
        if (tm.tm_min >= 30) {
            tm.tm_min = 30;
        } else {
            tm.tm_min = 0;
        }
        break;
    case HOUR:
        pattern = "%Y-%m-%d_%H";
        tm.tm_min = 0;
        break;
    case HOUR_12:
        pattern = "%Y-%m-%d_%H";
        // 将小时对齐到12小时边界
        if (tm.tm_hour >= 12) {
            tm.tm_hour = 12;
        } else {
            tm.tm_hour = 0;
        }
        tm.tm_min = 0;
        break;
    case DAY:
        pattern = "%Y-%m-%d";
        tm.tm_min = 0;
        tm.tm_hour = 0;
        break;
    case WEEK:
        {
            pattern = "%Y-%m-%d";
            // 对齐到周一
            tm.tm_min = 0;
            tm.tm_hour = 0;
            // 调整到周一
            int days_since_monday = (tm.tm_wday + 6) % 7;
            tm.tm_mday -= days_since_monday;
            mktime(&tm);
            break;
        }
    case MONTH:
        pattern = "%Y-%m";
        tm.tm_min = 0;
        tm.tm_hour = 0;
        tm.tm_mday = 1;
        break;
    }

    char buf[64];
    strftime(buf, sizeof(buf), pattern.c_str(), &tm);

    // 提取文件名和扩展名
    size_t dot_pos = m_filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string base = m_filename.substr(0, dot_pos);
        std::string ext = m_filename.substr(dot_pos);
        return base + "_" + buf + ext;
    } else {
        return m_filename + "_" + buf;
    }
}

bool FileLogAppender::shouldRotate(uint64_t timestamp) {
    if (m_rotateType == NONE) {
        return false;
    }

    if (m_lastTime == 0) {
        return true;
    }

    struct tm current_tm, last_tm;
    time_t current_time = timestamp;
    time_t last_time = m_lastTime;
    localtime_r(&current_time, &current_tm);
    localtime_r(&last_time, &last_tm);

    switch (m_rotateType) {
    case MINUTE_30:
    {
        int current_30min = current_tm.tm_hour * 2 + (current_tm.tm_min >= 30 ? 1 : 0);
        int last_30min = last_tm.tm_hour * 2 + (last_tm.tm_min >= 30 ? 1 : 0);
        return current_30min != last_30min;
    }
    case HOUR:
        return current_tm.tm_hour != last_tm.tm_hour || current_tm.tm_mday != last_tm.tm_mday;
    case HOUR_12:
    {
        int current_12hour = current_tm.tm_hour >= 12 ? 1 : 0;
        int last_12hour = last_tm.tm_hour >= 12 ? 1 : 0;
        return current_12hour != last_12hour || current_tm.tm_mday != last_tm.tm_mday;
    }
    case DAY:
        return current_tm.tm_mday != last_tm.tm_mday 
            || current_tm.tm_mon != last_tm.tm_mon 
            || current_tm.tm_year != last_tm.tm_year;
    case WEEK:
    {
        // 计算从年初开始的周数
        int current_weeks = current_tm.tm_yday / 7;
        int last_weeks = last_tm.tm_yday / 7;
        return current_weeks != last_weeks || current_tm.tm_year != last_tm.tm_year;
    }
    case MONTH:
        return current_tm.tm_mon != last_tm.tm_mon || current_tm.tm_year != last_tm.tm_year;
    default:
        return false;
    }
}

void FileLogAppender::cleanOldFiles() {
    if (m_retentionDays <= 0 || m_rotateType == NONE) {
        return;
    }

    // 计算截止时间戳
    uint64_t now = time(0);
    time_t cutoffTime = now - m_retentionDays * 86400;

    // 获取日期格式模板
    const char* dateFormat = nullptr;
    switch (m_rotateType) {
    case MINUTE_30:
        dateFormat = "%Y-%m-%d_%H-%M";
        break;
    case HOUR:
    case HOUR_12:
        dateFormat = "%Y-%m-%d_%H";
        break;
    case DAY:
    case WEEK:
        dateFormat = "%Y-%m-%d";
        break;
    case MONTH:
        dateFormat = "%Y-%m";
        break;
    default:
        return;
    }

    // 提取文件名的基础部分和扩展名
    std::string dir = FSUtil::Dirname(m_filename);
    std::string basename = FSUtil::Basename(m_filename);

    size_t dotPos = basename.find_last_of('.');
    std::string bare;
    std::string ext;
    if (dotPos != std::string::npos) {
        bare = basename.substr(0, dotPos);
        ext = basename.substr(dotPos);
    } else {
        bare = basename;
    }

    // 列出目录下同扩展名的文件
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, dir, ext);

    std::string prefix = bare + "_";
    for (auto& fullPath : files) {
        std::string fname = FSUtil::Basename(fullPath);

        // 跳过当前正在写入的文件
        if (fname == basename || fullPath == m_lastFileName || fname == FSUtil::Basename(m_lastFileName)) {
            continue;
        }

        // 检查文件名是否匹配轮转文件命名模式
        if (fname.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }

        // 提取日期字符串
        size_t dateStart = prefix.size();
        size_t dateLen = fname.size() - prefix.size();
        if (!ext.empty() && dateLen > ext.size()) {
            dateLen -= ext.size();
        }
        std::string dateStr = fname.substr(dateStart, dateLen);

        // 剥离分片序号后缀（形如 _1），便于解析日期
        size_t last_underscore = dateStr.find_last_of('_');
        if (last_underscore != std::string::npos && last_underscore + 1 < dateStr.size()) {
            bool all_digits = true;
            for (size_t i = last_underscore + 1; i < dateStr.size(); ++i) {
                if (!isdigit(static_cast<unsigned char>(dateStr[i]))) {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits) {
                dateStr = dateStr.substr(0, last_underscore);
            }
        }

        // 解析日期并检查是否过期
        time_t fileTime = Str2Time(dateStr.c_str(), dateFormat);
        if (fileTime > 0 && fileTime < cutoffTime) {
            FSUtil::Unlink(fullPath);
        }
    }
}

// LogEventWrap
LogEventWrap::~LogEventWrap() {
    if (m_event) {
        m_event->getLogger()->log(m_event->getLevel(), m_event);
    }
}

std::stringstream& LogEventWrap::getSS() {
    if (!m_event) {
        static thread_local std::stringstream s_null_ss;
        s_null_ss.str("");
        s_null_ss.clear();
        return s_null_ss;
    }
    return m_event->getSS();
}

LoggerManager::LoggerManager() {
    m_root.reset(new Logger);
    m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));

    m_loggers[m_root->getName()] = m_root;
}

Logger::ptr LoggerManager::getLogger(const std::string& name) {
    MutexType::Lock lock(m_mutex);
    auto it = m_loggers.find(name);
    if (it != m_loggers.end()) {
        return it->second;
    }
    Logger::ptr logger(new Logger(name));
    logger->m_root = m_root;
    m_loggers[name] = logger;
    return logger;
}

std::string LoggerManager::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    for (auto& [name, logger] : m_loggers) {
        node.push_back(YAML::Load(logger->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

struct LogAppenderDefine {
    int type = 0; /* 1 file, 2 std */
    LogLevel::Level level = LogLevel::UNKNOW;
    std::string formatter;
    std::string file;
    std::string time_rotate; /* none, 30min, hour, 12hour, day, week, month */
    int retention_days = 0;
    uint64_t max_size = 0; /* 单文件最大字节数，0表示不限制 */

    bool operator==(const LogAppenderDefine& oth) const {
        return type == oth.type
            && level == oth.level
            && formatter == oth.formatter
            && file == oth.file
            && time_rotate == oth.time_rotate
            && retention_days == oth.retention_days
            && max_size == oth.max_size;
    }
};

struct LogDefine {
    std::string name;
    LogLevel::Level level = LogLevel::UNKNOW;
    std::string formatter;
    std::vector<LogAppenderDefine> appenders;

    bool operator==(const LogDefine& oth) const {
        return name == oth.name
            && level == oth.level
            && formatter == oth.formatter
            && appenders == oth.appenders;
    }

    bool operator<(const LogDefine& oth) const {
        return name < oth.name;
    }
};

/* 全特化 */
template <>
class LexicalCast<std::string, std::set<LogDefine>> {
public:
    std::set<LogDefine> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        std::set<LogDefine> vec;

        for (size_t i = 0; i < node.size(); ++i) {
            auto n = node[i];
            if (!n["name"].IsDefined()) {
                std::cout << "log config error: name is null, "
                    << n << std::endl;
                continue ;
            }

            LogDefine ld;
            ld.name = n["name"].as<std::string>();
            ld.level = LogLevel::FromString(n["level"].IsDefined() ? n["level"].as<std::string>() : "");
            if (n["formatter"].IsDefined()) {
                ld.formatter = n["formatter"].as<std::string>();
            }
            if (n["appenders"].IsDefined()) {
                for (size_t j = 0; j < n["appenders"].size(); ++j) {
                    auto a = n["appenders"][j];
                    if (!a["type"].IsDefined()) {
                        std::cout << "log config error: appender type is null, "
                            << a << std::endl;
                        continue ;
                    }
                    std::string type = a["type"].as<std::string>();
                    LogAppenderDefine lad;
                    if (type == "FileLogAppender") {
                        lad.type = 1;
                        if (!a["file"].IsDefined()) {
                            std::cout << "log config error: fileappender file is null, "
                                << a << std::endl;
                            continue ;
                        }
                        lad.file = a["file"].as<std::string>();
                        if (a["formatter"].IsDefined()) {
                            lad.formatter = a["formatter"].as<std::string>();
                        }
                        if (a["time_rotate"].IsDefined()) {
                            lad.time_rotate = a["time_rotate"].as<std::string>();
                        }
                        if (a["retention_days"].IsDefined()) {
                            lad.retention_days = a["retention_days"].as<int>();
                        }
                        if (a["max_size"].IsDefined()) {
                            lad.max_size = a["max_size"].as<uint64_t>();
                        }
                    } else if (type == "StdoutLogAppender") {
                        lad.type = 2;
                    } else {
                        std::cout << "log config error: appender type is invalid, "
                            << a << std::endl;
                        continue ;
                    }
                    ld.appenders.push_back(lad);
                }
            }
            vec.insert(ld);
        }
        return vec;
    }
};

template <>
class LexicalCast<std::set<LogDefine>, std::string> {
public:
    std::string operator()(const std::set<LogDefine>& v) {
        YAML::Node node;
        for (auto& [name, level, formatter, appenders] : v) {
            YAML::Node n;
            n["name"] = name;
            if (level != LogLevel::UNKNOW) {
                n["level"] = LogLevel::toString(level);
            }
            if (!formatter.empty()) {
                n["formatter"] = formatter;
            }

            for (auto& v : appenders) {
                YAML::Node now;
                if (v.type == 1) {
                    now["type"] = "FileLogAppender";
                    now["file"] = v.file;
                    if (!v.time_rotate.empty()) {
                        now["time_rotate"] = v.time_rotate;
                    }
                    if (v.retention_days > 0) {
                        now["retention_days"] = v.retention_days;
                    }
                    if (v.max_size > 0) {
                        now["max_size"] = v.max_size;
                    }
                } else if (v.type == 2) {
                    now["type"] = "StdoutAppender";
                }
                if (v.level != LogLevel::UNKNOW) {
                    now["level"] = LogLevel::toString(v.level);
                }

                if (!v.formatter.empty()) {
                    now["formatter"] = v.formatter;
                }

                n["appenders"].push_back(now);
            }
            node.push_back(n);
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

static ConfigVar<std::set<LogDefine>>::ptr g_log_define =
    Config::Lookup("logs", std::set<LogDefine>(), "logs config");

struct LogIniter {
    LogIniter() {
        g_log_define->addListener([](const std::set<LogDefine>& old_value, const std::set<LogDefine>& new_value) {
            for (auto& i : new_value) {
                auto it = old_value.find(i);
                Logger::ptr logger;
                if (it == old_value.end()) {
                    logger = LOG_NAME(i.name);
                } else {
                    if (!(i == *it)) {
                        logger = LOG_NAME(i.name);
                    } else {
                        continue ;
                    }
                }
                logger->setLevel(i.level);
                if (!i.formatter.empty()) {
                    logger->setFormatter(i.formatter);
                }

                logger->clearAppenders();
                for (auto& [type, level, formatter, file, time_rotate, retention_days, max_size] : i.appenders) {
                    LogAppender::ptr ap;
                    if (type == 1) {
                        FileLogAppender::TimeRotateType rotate_type = FileLogAppender::StringToTimeRotateType(time_rotate);
                        ap.reset(new FileLogAppender(file, rotate_type, retention_days, max_size));
                    } else if (type == 2) {
                        ap.reset(new StdoutLogAppender);
                    }
                    ap->setLevel(level);
                    if (!formatter.empty()) {
                        LogFormatter::ptr fmt(new LogFormatter(formatter));
                        if (!fmt->isError()) {
                            ap->setFormatter(fmt);
                        } else {
                            std::cout << "log.name=" << i.name << " appender type=" << type
                                << " formatter=" << formatter << " is invalid" << std::endl;
                        }
                    }
                    logger->addAppender(ap);
                }
            }

            for (auto& i : old_value) {
                auto it = new_value.find(i);
                if (it == new_value.end()) {
                    /* old有 new没有 */
                    auto logger = LOG_NAME(i.name);
                    logger->setLevel((LogLevel::Level)100);
                    logger->clearAppenders();
                }
            }
            INFO(LOG_ROOT()) << "on_logger_conf_changed";
        });
    }
};

static LogIniter __log_init;

}
