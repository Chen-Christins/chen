/**
 * @file log.h
 * @brief 日志模块封装
 * @author Christins
 * @date 2024-11-02
 */
#pragma once

#include <format>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

#include "../thread/thread.h" // IWYU pragma: keep
#include "../util/mutex.h"
#include "../util/singleton.h"
#include "../util/util.h" // IWYU pragma: keep

#define LOG_LEVEL(logger, level)                                                 \
    if (logger->getLevel() <= level)                                             \
        chen::LogEventWrap(chen::LogEvent::ptr(new chen::LogEvent(logger, level, \
            __FILE__, __LINE__, 0, chen::GetThreadId(), chen::GetFiberId(),      \
            chen::GetCurrentUs(), chen::Thread::GetName()))).getSS()

#define TRACE(...) chen::LogDispatch(chen::LogLevel::TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG(...) chen::LogDispatch(chen::LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define INFO(...) chen::LogDispatch(chen::LogLevel::INFO, __FILE__, __LINE__, __VA_ARGS__)
#define WARN(...) chen::LogDispatch(chen::LogLevel::WARN, __FILE__, __LINE__, __VA_ARGS__)
#define ERROR(...) chen::LogDispatch(chen::LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define FATAL(...) chen::LogDispatch(chen::LogLevel::FATAL, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_ROOT() chen::LoggerMgr::GetInstance()->getRoot()
#define LOG_NAME(name) chen::LoggerMgr::GetInstance()->getLogger(name)

namespace chen {

class LogAppender;
class LogFormatter;
class LogEvent;

/**
 * @brief 日志级别
 */
class LogLevel {
public:
    enum Level {
        /// 未知 级别
        UNKNOW = 0,
        /// TRACE 级别（比 DEBUG 更细粒度）
        TRACE = 1,
        /// DEBUG 级别
        DEBUG = 2,
        /// INFO 级别
        INFO = 3,
        /// WARN 级别
        WARN = 4,
        /// ERROR 级别
        ERROR = 5,
        /// FATAL 级别
        FATAL = 6
    };
    /**
     * @brief 将日志级别转换为字符串
     * @param level 日志级别
     */
    static const char* toString(LogLevel::Level level);

    /**
     * @brief 将输入的文本转换为日志级别
     * @param str 日志级别的字符串
     */
    static LogLevel::Level FromString(const std::string& str);
};

/**
 * @brief 日志器
 */
class Logger : public std::enable_shared_from_this<Logger> {
friend class LoggerManager;
public:
    typedef std::shared_ptr<Logger> ptr;
    typedef SpinLock MutexType;
    
    /**
     * @brief 构造函数
     */
    Logger(const std::string& name = "root");

    /**
     * @brief 写日志
     * @param level 日志级别
     * @param event 日志事件
     */
    void log(LogLevel::Level level, std::shared_ptr<LogEvent> event);

    /**
     * @brief 写trace级别日志
     * @param event 日志事件
     */
    void trace(std::shared_ptr<LogEvent> event);

    /**
     * @brief 写debug级别日志
     * @param event 日志事件
     */
    void debug(std::shared_ptr<LogEvent> event);

    /**
     * @brief 写info级别日志
     * @param event 日志事件
     */
    void info(std::shared_ptr<LogEvent> event);

    /**
     * @brief 写warn级别的日志
     * @param event 日志事件
     */
    void warn(std::shared_ptr<LogEvent> event);
    
    /**
     * @brief 写error级别的日志
     * @param event 日志事件
     */
    void error(std::shared_ptr<LogEvent> event);

    /**
     * @brief 写fatal级别的日志
     * @param event 日志事件
     */
    void fatal(std::shared_ptr<LogEvent> event);
    
    /**
     * @brief 添加日志输出地
     * @param appender 日志输出地
     */
    void addAppender(std::shared_ptr<LogAppender> appender);

    /**
     * @brief 删除日志输出地
     * @param appender 日志输出地
     */
    void delAppender(std::shared_ptr<LogAppender> appender);

    /**
     * @brief 清空日志输出地
     */
    void clearAppenders();

    /**
     * @brief 返会日志级别
     */
    LogLevel::Level getLevel() const { return m_level; }

    /**
     * @brief 设置日志级别
     */
    void setLevel(LogLevel::Level val) { m_level = val; }

    /**
     * @brief 返回日志名称
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief 设置日志格式器
     */
    void setFormatter(std::shared_ptr<LogFormatter> val);

    /**
     * @brief 设置日志格式器
     * @param pattern 通过string的方式
     */
    void setFormatter(const std::string& val);
    /**
     * @brief 返回日志格式器
     */
    std::shared_ptr<LogFormatter> getFormatter();

    /**
     * @brief 转换为YamlString
     */
    std::string toYamlString();
private:
    /// 日志名称
    std::string m_name;
    /// 日志级别
    LogLevel::Level m_level;
    /// 日志目标集合
    std::list<std::shared_ptr<LogAppender>> m_appenders;
    /// 日志格式器
    std::shared_ptr<LogFormatter> m_formatter;
    /// 主日志器
    Logger::ptr m_root;
    /// Mutex
    MutexType m_mutex;
};

/**
 * @brief 日志事件
 */
class LogEvent {
public:
    typedef std::shared_ptr<LogEvent> ptr;
    /**
     * @brief 构造函数
     * @param logger 日志器
     * @param level 日志级别
     * @param file 文件名称
     * @param line 行号
     * @param elapse 程序启动到现在的耗时
     * @param threadId 线程号
     * @param fiberId 协程号
     * @param time 日志事件(秒)
     * @param threadName 线程名称
     */
    LogEvent(Logger::ptr logger, LogLevel::Level level
        ,const char* file, int32_t line, uint32_t elapse
        ,uint32_t threadId, uint32_t fiberId, uint64_t time
        ,const std::string& threadName);

    /**
     * @brief 获取文件名称
     */
    const char* getFile() const { return m_file; }

    /**
     * @brief 获取日志当前的行号
     */
    int32_t getLine() const { return m_line; }

    /**
     * @brief 返回程序启动到现在的时间
     */
    uint32_t getElapse() const { return m_elapse; }

    /**
     * @brief 返回线程号
     */
    uint32_t getThreadId() const { return m_threadId; }

    /**
     * @brief 返回协程号
     */
    uint32_t getFiberId() const { return m_fiberId; }

    /**
     * @brief 返回时间（微秒时间戳）
     */
    uint64_t getTime() const { return m_time; }

    /**
     * @brief 返回时间的秒部分
     */
    time_t getTimeSec() const { return m_time / 1000000; }

    /**
     * @brief 返回时间的微秒部分
     */
    uint32_t getTimeUs() const { return m_time % 1000000; }

    /**
     * @brief 返回线程名称
     */
    const std::string& getThreadName() const { return m_threadName; }

    /**
     * @brief 返回日志消息体
     */
    std::stringbuf* getContent() const { return m_ss.rdbuf(); }

    /**
     * @brief 返回日志器
     */
    Logger::ptr getLogger() const { return m_logger; }

    /**
     * @brief 返回日志级别
     */
    LogLevel::Level getLevel() const { return m_level; }

    /**
     * @brief 返回日志内容字符串流
     */
    std::stringstream& getSS() { return m_ss; }
private:
    /// 日志器
    Logger::ptr m_logger;
    /// 日志等级
    LogLevel::Level m_level;
    /// 文件名称
    const char* m_file = nullptr;
    /// 行号
    int32_t m_line = 0;
    /// 程序启动到现在的秒数
    uint32_t m_elapse = 0; 
    /// 线程id
    uint32_t m_threadId = 0;
    /// 协程id
    uint32_t m_fiberId = 0;
    /// 时间戳
    uint64_t m_time = 0;
    /// 线程名称
    std::string m_threadName;
    /// 日志内容
    std::stringstream m_ss;
};

/**
 * @brief 日志事件包装器
 */
class LogEventWrap {
public:
    typedef std::shared_ptr<LogEventWrap> ptr;
    typedef std::ostream& (*StreamManipulator)(std::ostream&);

    /**
     * @brief 构造函数
     * @param event 日志事件
     */
    LogEventWrap(LogEvent::ptr event) : m_event(event) {}

    template <typename T>
    LogEventWrap& operator<<(const T& val) {
        if (m_event) {
            m_event->getSS() << val;
        }
        return *this;
    }

    LogEventWrap& operator<<(StreamManipulator manip) {
        if (m_event) {
            manip(m_event->getSS());
        }
        return *this;
    }

    /**
     * @brief 析构函数
     * @details 主要就是利用析构函数将日志写入
     */
    ~LogEventWrap();

    /**
     * @brief 获取日志事件器
     */
    LogEvent::ptr getEvent() const { return m_event; }
    
    /**
     * @brief 给当前日志的字符流
     */
    std::stringstream& getSS();

private:
    /// 日志事件器
    LogEvent::ptr m_event;
};

/**
 * @brief 日志格式器
 */
class LogFormatter {
public:
    typedef std::shared_ptr<LogFormatter> ptr;
    /**
     * @brief 构造函数
     * @param pattern 日志格式模块
     */
    LogFormatter(const std::string& pattern);

    /**
     * @brief 类似一个提供写日志的方法，然后转到它的子类里去写
     * @param logger 日志器
     * @param level 日志级别
     * @param event 日志事件器
     * @return std::string 
     */
    std::string format(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event);
    /**
     * @brief 类似一个提供写日志的方法，然后转到它的子类里去写
     * @param ofs 输出流
     * @param logger 日志器
     * @param level 日志级别
     * @param event 日志事件器
     * @return std::ostream& 
     */
    std::ostream& format(std::ostream& ofs, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event);
public:
    class FormatItem {
    public:
        typedef std::shared_ptr<FormatItem> ptr;
        /**
         * @brief 析构函数
         */
        virtual ~FormatItem() {}
        /**
         * @brief 格式化日志到流中，这是一个接口函数
         * @param os 日志输出流
         * @param logger 日志器
         * @param level 日志等级
         * @param event 日志事件
         */
        virtual void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) = 0;
    };

    /**
     * @brief 解析日志格式模板
     */
    void init();

    /**
     * @brief 是否存在错误
     */
    bool isError() const { return m_error; }

    /**
     * @brief 返回日志格式模板
     */
    const std::string getPattern() const { return m_pattern; }
private:
    /// 日志格式模板
    std::string m_pattern;
    /// 日志格式解析后格式
    std::vector<FormatItem::ptr> m_items;
    /// 是否有错误
    bool m_error = false;
};

/**
 * @brief 日志输出地
 */
class LogAppender {
friend class Logger;
public:
    typedef std::shared_ptr<LogAppender> ptr;
    typedef SpinLock MutexType;

    /**
     * @brief 析构函数
     */
    virtual ~LogAppender() {}

    /**
     * @brief 它为子类提供写日志的接口
     * @param logger 日志器
     * @param level 日志级别
     * @param event 日志事件器
     */
    virtual void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) = 0;

    /**
     * @brief 获取日志格式器
     */
    LogFormatter::ptr getFormatter();

    /**
     * @brief 更改日志格式器
     */
    void setFormatter(LogFormatter::ptr val);

    /**
     * @brief 获取当前日志级别
     */
    LogLevel::Level getLevel() const { return m_level; }

    /**
     * @brief 更改日志级别
     */
    void setLevel(LogLevel::Level val) { m_level = val; }

    /**
     * @brief 转换为YamlString
     */
    virtual std::string toYamlString() = 0;
protected:
    /// 日志级别
    LogLevel::Level m_level = LogLevel::TRACE;
    /// 是否有自己的日志格式器
    bool m_hasFormatter = false;
    /// 日志格式器
    LogFormatter::ptr m_formatter;
    /// 自旋锁
    MutexType m_mutex;
};

/**
 * @brief 日志制台输出器
 */
class StdoutLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;
    /**
     * @brief 将日志写到控制台
     * @param logger 日志器
     * @param level 日志级别
     * @param event 日志事件器
     */
    void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override;

    /**
     * @brief 转换为YamlString
     */
    virtual std::string toYamlString() override;
};  

/**
 * @brief 日志文件输出器
 */
class FileLogAppender : public LogAppender {
public:
    /// 日志分文件时间频率枚举
    enum TimeRotateType {
        /// 不分文件
        NONE = 0,
        /// 每30分钟
        MINUTE_30 = 1,
        /// 每小时
        HOUR = 2,
        /// 每12小时
        HOUR_12 = 3,
        /// 每天
        DAY = 4,
        /// 每周
        WEEK = 5,
        /// 每月
        MONTH = 6
    };

    typedef std::shared_ptr<FileLogAppender> ptr;
    /**
     * @brief 构造函数
     * @param filename 文件名
     * @param rotate_type 时间分文件类型
     * @param retention_days 日志保留天数，0表示不限制
     * @param max_size 单文件最大字节数，超过则分片，0表示不限制
     */
    FileLogAppender(const std::string& filename, TimeRotateType rotate_type = NONE, int retention_days = 0, uint64_t max_size = 0);

    /**
     * @brief 将日志写到文件
     * @param logger 日志器
     * @param level 日志级别
     * @param event 日志事件器
     */
    void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override;

    /**
     * @brief 重新打开文件
     * @return bool 是否成功
     */
    bool reopen();

    /**
     * @brief 设置时间分文件类型
     * @param type 分文件类型
     */
    void setTimeRotateType(TimeRotateType type) { m_rotateType = type; }

    /**
     * @brief 获取时间分文件类型
     * @return TimeRotateType
     */
    TimeRotateType getTimeRotateType() const { return m_rotateType; }

    /**
     * @brief 设置日志保留天数
     * @param days 保留天数，0表示不限制
     */
    void setRetentionDays(int days) { m_retentionDays = days; }

    /**
     * @brief 获取日志保留天数
     * @return int 保留天数，0表示不限制
     */
    int getRetentionDays() const { return m_retentionDays; }

    /**
     * @brief 设置单文件最大字节数
     * @param max_size 最大字节数，0表示不限制
     */
    void setMaxSize(uint64_t max_size) { m_maxSize = max_size; }

    /**
     * @brief 获取单文件最大字节数
     * @return uint64_t 最大字节数，0表示不限制
     */
    uint64_t getMaxSize() const { return m_maxSize; }

    /**
     * @brief 检查当前文件是否因大小超限需要分片
     * @return bool
     */
    bool shouldRotateBySize() const { return m_maxSize > 0 && m_currentSize >= m_maxSize; }

    /**
     * @brief 根据时间获取新的文件名
     * @param timestamp 时间戳
     * @return std::string
     */
    std::string getNewFileName(uint64_t timestamp);

    /**
     * @brief 检查是否需要切换文件
     * @param timestamp 当前时间戳
     * @return bool
     */
    bool shouldRotate(uint64_t timestamp);

    /**
     * @brief 清理过期的日志文件
     */
    void cleanOldFiles();

    /**
     * @brief 转换为YamlString
     */
    virtual std::string toYamlString() override;

    /**
     * @brief 将TimeRotateType转换为字符串
     * @param type 时间分文件类型
     * @return const char*
     */
    static const char* TimeRotateTypeToString(TimeRotateType type);

    /**
     * @brief 将字符串转换为TimeRotateType
     * @param str 字符串
     * @return TimeRotateType
     */
    static TimeRotateType StringToTimeRotateType(const std::string& str);
private:
    /**
     * @brief 打开适合写入的分片文件：从当前周期基础名开始，跳过已满文件
     */
    void openFitFile();

private:
    /// 文件名称
    std::string m_filename;
    /// 文件输出流
    std::ofstream m_filestream;
    /// 上次打开这个日志文件的时间戳
    uint64_t m_lastTime = 0;
    /// 时间分文件类型
    TimeRotateType m_rotateType = NONE;
    /// 日志保留天数，0表示不限制
    int m_retentionDays = 0;
    /// 上次文件名
    std::string m_lastFileName;
    /// 当前时间周期的基础文件名（不含分片序号）
    std::string m_periodFileName;
    /// 当前时间周期内的分片序号，0表示基础文件
    uint64_t m_shardIndex = 0;
    /// 单文件最大字节数，0表示不限制
    uint64_t m_maxSize = 0;
    /// 当前文件已写入字节数
    uint64_t m_currentSize = 0;
};

/**
 * @brief 日志管理器
 */
class LoggerManager {
public:
    typedef SpinLock MutexType;
    /**
     * @brief 构造函数
     */
    LoggerManager();

    /**
     * @brief 获取日志器
     * @param name 为日志器设定它的日志名称
     * @return Logger::ptr 返回一个日志器
     */
    Logger::ptr getLogger(const std::string& name);

    /**
     * @brief 获取根日志器
     * @return Logger::ptr 
     */
    Logger::ptr getRoot() const { return m_root; }

    /**
     * @brief 转换为YamlString
     */
    std::string toYamlString();
private:
    /// 日志器集合
    std::map<std::string, Logger::ptr> m_loggers;
    /// 根日志器
    Logger::ptr m_root;
    /// Mutex
    MutexType m_mutex;
};

/**
 * @brief 单例模式
 */
typedef Singleton<LoggerManager> LoggerMgr;

inline std::string NormalizeFormatArg(const char* val) {
    return val ? std::string(val) : std::string("(null)");
}

inline std::string NormalizeFormatArg(char* val) {
    return val ? std::string(val) : std::string("(null)");
}

template <typename T>
decltype(auto) NormalizeFormatArg(T&& val) {
    return std::forward<T>(val);
}

template <typename... Args>
inline void LogByFormatWithLogger(Logger::ptr logger, LogLevel::Level level
        , const char* file, int32_t line, const std::string& pattern, Args&&... args) {
    if (!logger || logger->getLevel() > level) {
        return;
    }

    LogEvent::ptr event(
        new LogEvent(logger,level, file, line, 0, chen::GetThreadId(), chen::GetFiberId(), chen::GetCurrentUs(), chen::Thread::GetName())
    );

    try {
        auto formatArgs = std::make_tuple(NormalizeFormatArg(std::forward<Args>(args))...);
        std::apply([&](auto&... vals) { event->getSS() << std::vformat(pattern, std::make_format_args(vals...)); }, formatArgs);
    } catch (const std::format_error& e) {
        event->getSS() << "[format_error: " << e.what() << "] " << pattern;
    }
    logger->log(level, event);
}

inline LogEventWrap LogDispatch(LogLevel::Level level, const char* file, int32_t line, Logger::ptr logger) {
    if (!logger || logger->getLevel() > level) {
        return LogEventWrap(nullptr);
    }

    LogEvent::ptr event(
        new LogEvent(logger, level, file, line, 0, chen::GetThreadId(), chen::GetFiberId(), chen::GetCurrentUs(), chen::Thread::GetName())
    );

    return LogEventWrap(event);
}

template <typename... Args>
inline void LogDispatch(LogLevel::Level level, const char* file, int32_t line, const std::string& pattern, Args&&... args) {
    LogByFormatWithLogger(LoggerMgr::GetInstance()->getRoot(), level, file, line, pattern, std::forward<Args>(args)...);
}

template <typename... Args>
inline void LogDispatch(LogLevel::Level level, const char* file, int32_t line, Logger::ptr logger, const std::string& pattern, Args&&... args) {
    LogByFormatWithLogger(logger, level, file, line, pattern, std::forward<Args>(args)...);
}

} // namespace chen
