#include "Log.hpp"

#include <cctype>
#include <functional>
#include <iomanip>
#include <iostream>
#include <unordered_map>

#include "../Config/Config.hpp"

namespace chen {

/// 默认日志格式
static constexpr const char* kDefaultFormatterPattern = "%d{%Y-%m-%d %H:%M:%S.%f}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n";

// LogEvent
const char* LogLevel::toString(Level level)
{
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

LogLevel::Level LogLevel::FromString(const std::string& str)
{
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
    : name_(name)
    , level_(LogLevel::TRACE)
{
    formatter_.reset(new LogFormatter(kDefaultFormatterPattern));
}

void Logger::log(LogLevel::Level level, std::shared_ptr<LogEvent> event)
{
    if (level >= level_) {
        auto self = shared_from_this();
        MutexType::Lock lock(mutex_);
        if (!appenders_.empty()) {
            for (auto& i : appenders_) {
                i->log(self, level, event);
            }
        } else if (root_) {
            root_->log(level, event);
        }
    }
}

void Logger::trace(std::shared_ptr<LogEvent> event)
{
    log(LogLevel::TRACE, event);
}

void Logger::debug(std::shared_ptr<LogEvent> event)
{
    log(LogLevel::DEBUG, event);
}

void Logger::info(std::shared_ptr<LogEvent> event)
{
    log(LogLevel::INFO, event);
}

void Logger::warn(std::shared_ptr<LogEvent> event)
{
    log(LogLevel::WARN, event);
}

void Logger::error(std::shared_ptr<LogEvent> event)
{
    log(LogLevel::ERROR, event);
}

void Logger::fatal(std::shared_ptr<LogEvent> event)
{
    log(LogLevel::FATAL, event);
}

void Logger::addAppender(std::shared_ptr<LogAppender> appender)
{
    MutexType::Lock lock(mutex_);
    if (!appender->getFormatter()) {
        MutexType::Lock lock2(appender->mutex_);
        appender->formatter_ = formatter_;
    }
    appenders_.push_back(appender);
}

void Logger::delAppender(std::shared_ptr<LogAppender> appender)
{
    MutexType::Lock lock(mutex_);
    for (auto it = appenders_.begin(); it != appenders_.end(); ++it) {
        if (*it == appender) {
            appenders_.erase(it);
            break;
        }
    }
}

void Logger::setFormatter(std::shared_ptr<LogFormatter> val)
{
    MutexType::Lock lock(mutex_);
    formatter_ = val;
    for (auto& i : appenders_) {
        MutexType::Lock lock2(i->mutex_);
        if (!i->has_formatter_) {
            i->formatter_ = formatter_;
        }
    }
}

void Logger::setFormatter(const std::string& val)
{
    LogFormatter::ptr pattern(new LogFormatter(val));
    if (pattern->isError()) {
        std::cout << "Logger setFormatter name=" << name_ << " value=" << val << " invalid formatter" << std::endl;
        return;
    }
    setFormatter(pattern);
}

void Logger::resetFormatter()
{
    LogFormatter::ptr pattern(new LogFormatter(kDefaultFormatterPattern));
    setFormatter(pattern);
}

void Logger::clearAppenders()
{
    MutexType::Lock lock(mutex_);
    appenders_.clear();
}

void Logger::flush()
{
    MutexType::Lock lock(mutex_);
    for (auto& i : appenders_) {
        if (i) {
            i->flush();
        }
    }
}

std::string Logger::toYamlString()
{
    MutexType::Lock lock(mutex_);
    YAML::Node node;
    node["name"] = name_;
    if (level_ != LogLevel::UNKNOW) {
        node["level"] = LogLevel::toString(level_);
    }
    if (formatter_) {
        node["formatter"] = formatter_->getPattern();
    }
    for (auto& i : appenders_) {
        node["appender"].push_back(YAML::Load(i->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

std::shared_ptr<LogFormatter> Logger::getFormatter()
{
    MutexType::Lock lock(mutex_);
    return formatter_;
}

// LogEvent
LogEvent::LogEvent(
    Logger::ptr logger,
    LogLevel::Level level,
    const char* file,
    int32_t line,
    uint32_t elapse,
    uint32_t thread_id,
    uint32_t fiber_id,
    uint64_t time,
    const std::string& thread_name)
    : logger_(logger)
    , level_(level)
    , file_(file)
    , line_(line)
    , elapse_(elapse)
    , thread_id_(thread_id)
    , fiber_id_(fiber_id)
    , time_(time)
    , thread_name_(thread_name)
{
}

// LogFormatter
LogFormatter::LogFormatter(const std::string& pattern)
    : pattern_(pattern)
{
    init();
}

class MessageFormatItem : public LogFormatter::FormatItem {
public:
    MessageFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        std::stringbuf* sb = event->getContent();
        sb->pubseekpos(0, std::ios_base::in);
        os << sb;
    }
};

class LevelFormatItem : public LogFormatter::FormatItem {
public:
    LevelFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << LogLevel::toString(level); }
};

class ElapseFormatItem : public LogFormatter::FormatItem {
public:
    ElapseFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << event->getElapse(); }
};

class NameFormatItem : public LogFormatter::FormatItem {
public:
    NameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << event->getLogger()->getName(); }
};

class ThreadIdFormatItem : public LogFormatter::FormatItem {
public:
    ThreadIdFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << event->getThreadId(); }
};

class FiberIdFormatItem : public LogFormatter::FormatItem {
public:
    FiberIdFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << event->getFiberId(); }
};

class ThreadNameFormatItem : public LogFormatter::FormatItem {
public:
    ThreadNameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << event->getThreadName(); }
};

class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
    DateTimeFormatItem(const std::string format = "%Y-%m-%d %H:%M:%S")
        : format_(format)
    {
        if (format_.empty()) {
            format_ = "%Y-%m-%d %H:%M:%S";
        }
    }
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        struct tm tm;
        time_t time = event->getTimeSec();
        localtime_r(&time, &tm);

        // 检查是否包含 %f 格式
        std::string format = format_;
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
    std::string format_;
};

class FileNameFormatItem : public LogFormatter::FormatItem {
public:
    FileNameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << event->getFile(); }
};

class LineFormatItem : public LogFormatter::FormatItem {
public:
    LineFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << event->getLine(); }
};

class NewLineFormatItem : public LogFormatter::FormatItem {
public:
    NewLineFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << std::endl; }
};

class StringFormatItem : public LogFormatter::FormatItem {
public:
    StringFormatItem(const std::string& str = "")
        : string_(str)
    {
    }
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << string_;
    }

private:
    std::string string_;
};

class TabFormatItem : public LogFormatter::FormatItem {
public:
    TabFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override { os << "\t"; }
};

// %d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n
void LogFormatter::init()
{
    // str, format, type
    std::vector<std::tuple<std::string, std::string, int>> vec;
    std::string format = "";

    bool vis = (pattern_.find("{") != std::string::npos ? true : false);
    for (size_t i = 0; i < pattern_.size(); ++i) {
        size_t j = i;
        while (j < pattern_.size() && pattern_[j] == '%') {
            ++j;
        }
        i = j;
        std::string str = pattern_.substr(j, 1);
        if (str == "d" && vis && pattern_[j + 1] == '{') {
            size_t n = j + 2, len = 0;
            while (n < pattern_.size() && pattern_[n] != '}') {
                ++n;
                ++len;
            }
            format = pattern_.substr(j + 2, len);
            i = n;
        }
        int type = !!std::isalpha(str[0]);
        vec.push_back(std::make_tuple(str, format, type));
        if (!format.empty()) {
            format.clear();
        }
    }

    static std::unordered_map<std::string, std::function<FormatItem::ptr(const std::string& str)>> format_items = {
#define XX(str, C)                              \
    {                                           \
        #str, [](const std::string& fmt) {      \
            return FormatItem::ptr(new C(fmt)); \
        }                                       \
    }
        XX(m, MessageFormatItem), XX(p, LevelFormatItem),   XX(t, ThreadIdFormatItem), XX(N, ThreadNameFormatItem),
        XX(c, NameFormatItem),    XX(F, FiberIdFormatItem), XX(d, DateTimeFormatItem), XX(f, FileNameFormatItem),
        XX(l, LineFormatItem),    XX(T, TabFormatItem),     XX(n, NewLineFormatItem),  XX(r, ElapseFormatItem),
#undef XX
    };

    for (auto& [str, fmt, type] : vec) {
        if (!type) {
            items_.emplace_back(FormatItem::ptr(new StringFormatItem(str)));
        } else {
            auto it = format_items.find(str);
            if (it == format_items.end()) {
                items_.emplace_back(FormatItem::ptr(new StringFormatItem("<<error_format %" + str + ">>")));
                error_ = true;
            } else {
                items_.emplace_back(it->second(fmt));
            }
        }
    }
}

LogFormatter::FormatItem::~FormatItem()
{
}

std::string LogFormatter::format(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event)
{
    std::stringstream ss;
    for (auto& i : items_) {
        i->format(ss, logger, level, event);
    }
    return ss.str();
}

std::ostream& LogFormatter::format(std::ostream& ofs, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event)
{
    for (auto& i : items_) {
        i->format(ofs, logger, level, event);
    }
    return ofs;
}

// LogAppender
LogAppender::~LogAppender()
{
}

void LogAppender::flush()
{
}

LogFormatter::ptr LogAppender::getFormatter()
{
    MutexType::Lock lock(mutex_);
    return formatter_;
}

void LogAppender::setFormatter(LogFormatter::ptr val)
{
    MutexType::Lock lock(mutex_);
    formatter_ = val;
    if (formatter_) {
        has_formatter_ = true;
    } else {
        has_formatter_ = false;
    }
}

// StdoutLogAppender
void StdoutLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event)
{
    if (level >= level_) {
        MutexType::Lock lock(mutex_);
        // 获取日志级别对应的颜色
        const char* color = "";
        switch (level) {
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
        formatter_->format(ss, logger, level, event);
        ss << "\033[0m";
        std::cout << ss.str() << std::flush;
    }
}

std::string StdoutLogAppender::toYamlString()
{
    MutexType::Lock lock(mutex_);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    if (level_ != LogLevel::UNKNOW) {
        node["level"] = LogLevel::toString(level_);
    }
    if (has_formatter_ && formatter_) {
        node["formatter"] = formatter_->getPattern();
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
std::string AppendShardIndex(const std::string& filename, uint64_t index)
{
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
    : filename_(filename)
    , rotate_type_(rotate_type)
    , retention_days_(retention_days)
    , max_size_(max_size)
{
    if (rotate_type_ != NONE) {
        // 如果设置了时间分文件，立即生成当前时间的文件名
        period_file_name_ = getNewFileName(time(0));
    } else {
        period_file_name_ = filename_;
    }
    openFitFile();
}

void FileLogAppender::openFitFile()
{
    uint64_t index = 0;
    std::string name = period_file_name_;
    if (max_size_ > 0) {
        // 从基础名开始，跳过已写满的分片文件
        while (true) {
            int64_t size = FSUtil::FileSize(name);
            if (size < 0) {
                size = 0;
            }
            if (static_cast<uint64_t>(size) < max_size_) {
                break;
            }
            ++index;
            name = AppendShardIndex(period_file_name_, index);
        }
    }
    MutexType::Lock lock(mutex_);
    if (filestream_) {
        filestream_.close();
    }
    FSUtil::OpenForWrite(filestream_, name, std::ios::app);
    last_file_name_ = name;
    shard_index_ = index;
    int64_t size = FSUtil::FileSize(name);
    current_size_ = (size > 0) ? static_cast<uint64_t>(size) : 0;
}

void FileLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event)
{
    if (level >= level_) {
        uint64_t now = event->getTimeSec();
        if (now >= (last_time_ + 3)) {
            if (rotate_type_ != NONE) {
                if (shouldRotate(now)) {
                    std::string new_file_name = getNewFileName(now);
                    if (period_file_name_ != new_file_name) {
                        period_file_name_ = new_file_name;
                        openFitFile();
                        cleanOldFiles();
                    }
                }
            } else {
                reopen();
            }
            last_time_ = now;
        }

        // 大小分片：每次写入前检查，超限则切换到下一个分片
        if (shouldRotateBySize()) {
            openFitFile();
        }

        MutexType::Lock lock(mutex_);
        std::streampos before = filestream_.tellp();
        if (!formatter_->format(filestream_, logger, level, event)) {
            std::cout << "error" << std::endl;
        } else {
            std::streampos after = filestream_.tellp();
            if (before >= 0 && after >= before) {
                current_size_ += static_cast<uint64_t>(after - before);
            }
        }
    }
}

void FileLogAppender::flush()
{
    MutexType::Lock lock(mutex_);
    if (filestream_) {
        filestream_.flush();
    }
}

bool FileLogAppender::reopen()
{
    MutexType::Lock lock(mutex_);
    if (filestream_) {
        filestream_.close();
    }
    std::string name = last_file_name_.empty() ? filename_ : last_file_name_;
    if (!FSUtil::OpenForWrite(filestream_, name, std::ios::app)) {
        return false;
    }
    int64_t size = FSUtil::FileSize(name);
    current_size_ = (size > 0) ? static_cast<uint64_t>(size) : 0;
    return true;
}

std::string FileLogAppender::toYamlString()
{
    MutexType::Lock lock(mutex_);
    YAML::Node node;
    node["type"] = "FileLogAppender";
    node["file"] = filename_;
    if (rotate_type_ != NONE) {
        node["time_rotate"] = TimeRotateTypeToString(rotate_type_);
    }
    if (retention_days_ > 0) {
        node["retention_days"] = retention_days_;
    }
    if (max_size_ > 0) {
        node["max_size"] = max_size_;
    }
    if (level_ != LogLevel::UNKNOW) {
        node["level"] = LogLevel::toString(level_);
    }
    if (has_formatter_ && formatter_) {
        node["formatter"] = formatter_->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

const char* FileLogAppender::TimeRotateTypeToString(TimeRotateType type)
{
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

FileLogAppender::TimeRotateType FileLogAppender::StringToTimeRotateType(const std::string& str)
{
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

std::string FileLogAppender::getNewFileName(uint64_t timestamp)
{
    struct tm tm;
    time_t time = timestamp;
    localtime_r(&time, &tm);

    std::string pattern = "%Y-%m-%d";

    switch (rotate_type_) {
    case NONE:
        return filename_;
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
    case WEEK: {
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
    size_t dot_pos = filename_.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string base = filename_.substr(0, dot_pos);
        std::string ext = filename_.substr(dot_pos);
        return base + "_" + buf + ext;
    } else {
        return filename_ + "_" + buf;
    }
}

bool FileLogAppender::shouldRotate(uint64_t timestamp)
{
    if (rotate_type_ == NONE) {
        return false;
    }

    if (last_time_ == 0) {
        return true;
    }

    struct tm current_tm, last_tm;
    time_t current_time = timestamp;
    time_t last_time = last_time_;
    localtime_r(&current_time, &current_tm);
    localtime_r(&last_time, &last_tm);

    switch (rotate_type_) {
    case MINUTE_30: {
        int current_30min = current_tm.tm_hour * 2 + (current_tm.tm_min >= 30 ? 1 : 0);
        int last_30min = last_tm.tm_hour * 2 + (last_tm.tm_min >= 30 ? 1 : 0);
        return current_30min != last_30min;
    }
    case HOUR:
        return current_tm.tm_hour != last_tm.tm_hour || current_tm.tm_mday != last_tm.tm_mday;
    case HOUR_12: {
        int current_12hour = current_tm.tm_hour >= 12 ? 1 : 0;
        int last_12hour = last_tm.tm_hour >= 12 ? 1 : 0;
        return current_12hour != last_12hour || current_tm.tm_mday != last_tm.tm_mday;
    }
    case DAY:
        return current_tm.tm_mday != last_tm.tm_mday || current_tm.tm_mon != last_tm.tm_mon || current_tm.tm_year != last_tm.tm_year;
    case WEEK: {
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

void FileLogAppender::cleanOldFiles()
{
    if (retention_days_ <= 0 || rotate_type_ == NONE) {
        return;
    }

    // 计算截止时间戳
    uint64_t now = time(0);
    time_t cutoff_time = now - retention_days_ * 86400;

    // 获取日期格式模板
    const char* date_format = nullptr;
    switch (rotate_type_) {
    case MINUTE_30:
        date_format = "%Y-%m-%d_%H-%M";
        break;
    case HOUR:
    case HOUR_12:
        date_format = "%Y-%m-%d_%H";
        break;
    case DAY:
    case WEEK:
        date_format = "%Y-%m-%d";
        break;
    case MONTH:
        date_format = "%Y-%m";
        break;
    default:
        return;
    }

    // 提取文件名的基础部分和扩展名
    std::string dir = FSUtil::Dirname(filename_);
    std::string basename = FSUtil::Basename(filename_);

    size_t dot_pos = basename.find_last_of('.');
    std::string bare;
    std::string ext;
    if (dot_pos != std::string::npos) {
        bare = basename.substr(0, dot_pos);
        ext = basename.substr(dot_pos);
    } else {
        bare = basename;
    }

    // 列出目录下同扩展名的文件
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, dir, ext);

    std::string prefix = bare + "_";
    for (auto& full_path : files) {
        std::string fname = FSUtil::Basename(full_path);

        // 跳过当前正在写入的文件
        if (fname == basename || full_path == last_file_name_ || fname == FSUtil::Basename(last_file_name_)) {
            continue;
        }

        // 检查文件名是否匹配轮转文件命名模式
        if (fname.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }

        // 提取日期字符串
        size_t date_start = prefix.size();
        size_t date_len = fname.size() - prefix.size();
        if (!ext.empty() && date_len > ext.size()) {
            date_len -= ext.size();
        }
        std::string date_str = fname.substr(date_start, date_len);

        // 剥离分片序号后缀（形如 _1），便于解析日期
        size_t last_underscore = date_str.find_last_of('_');
        if (last_underscore != std::string::npos && last_underscore + 1 < date_str.size()) {
            bool all_digits = true;
            for (size_t i = last_underscore + 1; i < date_str.size(); ++i) {
                if (!isdigit(static_cast<unsigned char>(date_str[i]))) {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits) {
                date_str = date_str.substr(0, last_underscore);
            }
        }

        // 解析日期并检查是否过期
        time_t file_time = TimeUtil::Str2Time(date_str.c_str(), date_format);
        if (file_time > 0 && file_time < cutoff_time) {
            FSUtil::Unlink(full_path);
        }
    }
}

// LogEventWrap
LogEventWrap::LogEventWrap(LogEvent::ptr event)
    : event_(event)
{
}

LogEventWrap& LogEventWrap::operator<<(StreamManipulator manip)
{
    if (event_) {
        manip(event_->getSS());
    }
    return *this;
}

LogEventWrap::~LogEventWrap()
{
    if (event_) {
        event_->getLogger()->log(event_->getLevel(), event_);
    }
}

std::stringstream& LogEventWrap::getSS()
{
    if (!event_) {
        static thread_local std::stringstream s_null_ss;
        s_null_ss.str("");
        s_null_ss.clear();
        return s_null_ss;
    }
    return event_->getSS();
}

LoggerManager::LoggerManager()
{
    root_.reset(new Logger);
    // 配置加载前的默认级别：抑制 bootstrap/静态初始化日志；
    // 加载 logs 配置后会被覆盖。子日志器默认 TRACE，仍通过 root 级别过滤。
    root_->setLevel(LogLevel::WARN);
    root_->addAppender(LogAppender::ptr(new StdoutLogAppender));

    loggers_[root_->getName()] = root_;
}

Logger::ptr LoggerManager::getLogger(const std::string& name)
{
    MutexType::Lock lock(mutex_);
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        return it->second;
    }
    Logger::ptr logger(new Logger(name));
    logger->root_ = root_;
    loggers_[name] = logger;
    return logger;
}

void LoggerManager::flushAll()
{
    MutexType::Lock lock(mutex_);
    for (auto& [name, logger] : loggers_) {
        if (logger) {
            logger->flush();
        }
    }
    // 非 root 日志器可能无自带 appender，实际输出在 root 上
    if (root_) {
        root_->flush();
    }
}

std::string LoggerManager::toYamlString()
{
    MutexType::Lock lock(mutex_);
    YAML::Node node;
    for (auto& [name, logger] : loggers_) {
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

    bool operator==(const LogAppenderDefine& oth) const
    {
        return type == oth.type && level == oth.level && formatter == oth.formatter && file == oth.file && time_rotate == oth.time_rotate
            && retention_days == oth.retention_days && max_size == oth.max_size;
    }
};

struct LogDefine {
    std::string name;
    LogLevel::Level level = LogLevel::UNKNOW;
    std::string formatter;
    std::vector<LogAppenderDefine> appenders;

    bool operator==(const LogDefine& oth) const
    {
        return name == oth.name && level == oth.level && formatter == oth.formatter && appenders == oth.appenders;
    }

    bool operator<(const LogDefine& oth) const { return name < oth.name; }
};

/* 全特化 */
template <>
class LexicalCast<std::string, std::set<LogDefine>> {
public:
    std::set<LogDefine> operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        std::set<LogDefine> vec;

        for (size_t i = 0; i < node.size(); ++i) {
            auto n = node[i];
            if (!n["name"].IsDefined()) {
                std::cout << "log config error: name is null, " << n << std::endl;
                continue;
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
                        std::cout << "log config error: appender type is null, " << a << std::endl;
                        continue;
                    }
                    std::string type = a["type"].as<std::string>();
                    LogAppenderDefine lad;
                    if (type == "FileLogAppender") {
                        lad.type = 1;
                        if (!a["file"].IsDefined()) {
                            std::cout << "log config error: fileappender file is null, " << a << std::endl;
                            continue;
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
                        std::cout << "log config error: appender type is invalid, " << a << std::endl;
                        continue;
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
    std::string operator()(const std::set<LogDefine>& v)
    {
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

static ConfigVar<std::set<LogDefine>>::ptr g_log_define = Config::Lookup("logs", std::set<LogDefine>(), "logs config");

struct LogIniter {
    LogIniter()
    {
        g_log_define->addListener([](const std::set<LogDefine>& old_value, const std::set<LogDefine>& new_value) {
            if (old_value == new_value) {
                return;
            }
            for (auto& i : new_value) {
                auto it = old_value.find(i);
                Logger::ptr logger;
                if (it == old_value.end()) {
                    logger = LOG_NAME(i.name);
                } else {
                    if (!(i == *it)) {
                        logger = LOG_NAME(i.name);
                    } else {
                        continue;
                    }
                }
                logger->setLevel(i.level);
                if (!i.formatter.empty()) {
                    logger->setFormatter(i.formatter);
                } else {
                    logger->resetFormatter();
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
                            std::cout << "log.name=" << i.name << " appender type=" << type << " formatter=" << formatter << " is invalid"
                                      << std::endl;
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
                    logger->resetFormatter();
                }
            }
            DEBUG(LOG_ROOT()) << "on_logger_conf_changed";
        });
    }
};

std::string NormalizeFormatArg(const char* val)
{
    return val ? std::string(val) : std::string("(null)");
}

std::string NormalizeFormatArg(char* val)
{
    return val ? std::string(val) : std::string("(null)");
}

LogEventWrap LogDispatch(LogLevel::Level level, const char* file, int32_t line, Logger::ptr logger)
{
    if (!logger || logger->getLevel() > level) {
        return LogEventWrap(nullptr);
    }

    LogEvent::ptr event(
        new LogEvent(logger, level, file, line, 0, chen::GetThreadId(), chen::GetFiberId(), TimeUtil::GetCurrentUs(), chen::Thread::GetName()));

    return LogEventWrap(event);
}

static LogIniter __log_init;

} // namespace chen
