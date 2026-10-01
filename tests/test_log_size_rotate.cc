/**
 * @file test_log_size_rotate.cc
 * @brief 测试 FileLogAppender 按大小分片（max_size）与时间切分、保留天数的配合
 * @author Christins
 * @date 2026-09-25
 */
#include "chen/log/log.h"
#include "chen/util/fs_util.h"
#include <cassert>
#include <ctime>
#include <iostream>
#include <vector>

using namespace chen;

namespace {

/// 构造一条日志事件并写入 logger，time_us 为事件时间（微秒），用于模拟跨时间边界
void WriteLineAt(Logger::ptr logger, const std::string& msg, uint64_t time_us) {
    LogEvent::ptr event(new LogEvent(logger, LogLevel::INFO, __FILE__, __LINE__, 0
        , GetThreadId(), GetFiberId(), time_us, "test"));
    event->getSS() << msg;
    logger->log(LogLevel::INFO, event);
}

/// 写一条日志，时间取当前时间
void WriteLine(Logger::ptr logger, const std::string& msg) {
    WriteLineAt(logger, msg, GetCurrentUs());
}

/// 列出目录下指定扩展名的文件
std::vector<std::string> ListFiles(const std::string& path) {
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, path, ".log");
    return files;
}

/// 清空目录下所有 .log 文件，避免上次运行残留影响断言
void CleanDir(const std::string& path) {
    for (auto& f : ListFiles(path)) {
        FSUtil::Unlink(f, false);
    }
}

/// 在扩展名前插入分片序号，模拟预期的分片文件名：app.log -> app_1.log
std::string ShardName(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return name + "_1";
    }
    return name.substr(0, dot) + "_1" + name.substr(dot);
}

/// 写足够多的日志把当前文件写满，触发至少一个分片（默认格式单条约 200 字节，12 条必超 512）
void Fill(Logger::ptr logger, uint64_t time_us, const std::string& payload) {
    for (int i = 0; i < 12; ++i) {
        WriteLineAt(logger, "line " + std::to_string(i) + " " + payload, time_us);
    }
}

} // namespace

// 纯大小分片：time_rotate=none，超过 max_size 后生成 app_1.log 等
void test_size_rotate_none() {
    std::cout << "=== test_size_rotate_none ===" << std::endl;

    std::string dir = "/tmp/test_log_size_rotate_none";
    std::string file = dir + "/app.log";
    FSUtil::Mkdir(dir);
    CleanDir(dir);

    FileLogAppender::ptr ap(new FileLogAppender(file, FileLogAppender::NONE, 0, 512));
    Logger::ptr logger(new Logger("test_size_none"));
    logger->setLevel(LogLevel::INFO);
    logger->addAppender(ap);

    std::string payload(100, 'x');
    for (int i = 0; i < 20; ++i) {
        WriteLine(logger, "line " + std::to_string(i) + " " + payload);
    }

    // 写入 20 条约 100 字节的日志，512 字节上限，应产生多个分片
    assert(FSUtil::Exists(file));
    assert(FSUtil::Exists(dir + "/app_1.log"));

    // 基础文件不超过 max_size（可能超一条日志的长度）
    int64_t base_size = FSUtil::FileSize(file);
    assert(base_size > 0);
    assert(base_size < 512 + 200); // 允许单条日志溢出

    std::cout << "  base_size=" << base_size << std::endl;
    std::cout << "PASS" << std::endl;
}

// 时间切分 + 大小分片：用伪造事件时间确定性地跨过每种时间周期边界
// 覆盖 30min/hour/12hour/day/week/month 全部 6 种类型：
//   1. 周期1内超大小 -> 分片（周期名_1）
//   2. 跨周期边界 -> 切换到周期2的基础文件
//   3. 周期2内超大小 -> 分片序号从 0 重新开始（周期2名_1）
void test_size_rotate_all_time_types() {
    std::cout << "=== test_size_rotate_all_time_types ===" << std::endl;

    // t1 与 t1+dt 在本地时区下必然跨越对应的周期边界（秒）
    const uint64_t t1_sec = 1700000000ULL; // 2023-11-14 22:13:20 UTC
    struct Case {
        const char* label;
        FileLogAppender::TimeRotateType type;
        uint64_t dt; // 周期2相对周期1的时间偏移（秒）
    };
    const Case cases[] = {
        {"30min",  FileLogAppender::MINUTE_30, 30 * 60},
        {"hour",   FileLogAppender::HOUR,       60 * 60},
        {"12hour", FileLogAppender::HOUR_12,    12 * 3600},
        {"day",    FileLogAppender::DAY,        24 * 3600},
        {"week",   FileLogAppender::WEEK,       8 * 24 * 3600},
        {"month",  FileLogAppender::MONTH,      40 * 24 * 3600},
    };

    std::string payload(100, 'y');
    for (auto& c : cases) {
        std::string dir = std::string("/tmp/test_log_size_") + c.label;
        std::string file = dir + "/app.log";
        FSUtil::Mkdir(dir);
        CleanDir(dir);

        FileLogAppender::ptr ap(new FileLogAppender(file, c.type, 0, 512));
        Logger::ptr logger(new Logger(std::string("test_size_") + c.label));
        logger->setLevel(LogLevel::INFO);
        logger->addAppender(ap);

        // 预期的两个周期文件名（用公开接口推导，避免硬编码本地时区日期）
        std::string base1 = FSUtil::Basename(ap->getNewFileName(t1_sec));
        std::string base2 = FSUtil::Basename(ap->getNewFileName(t1_sec + c.dt));
        assert(base1 != base2);

        // 周期1：写满并触发大小分片（LogEvent 时间为微秒）
        Fill(logger, t1_sec * 1000000, payload);
        assert(FSUtil::Exists(dir + "/" + base1));
        assert(FSUtil::Exists(dir + "/" + ShardName(base1)));

        // 周期2首条日志：时间轮转，切换到 base2（分片序号重置）
        WriteLineAt(logger, "cross boundary", (t1_sec + c.dt) * 1000000);
        assert(FSUtil::Exists(dir + "/" + base2));

        // 周期2：写满并再次分片
        Fill(logger, (t1_sec + c.dt) * 1000000, payload);
        assert(FSUtil::Exists(dir + "/" + ShardName(base2)));

        std::cout << "  [" << c.label << "] " << base1 << " -> " << ShardName(base1)
                  << ", rotate -> " << base2 << " -> " << ShardName(base2) << std::endl;
        std::cout << "  PASS" << std::endl;
    }
}

// 关闭分片（max_size=0）：不应产生分片文件
void test_no_size_rotate() {
    std::cout << "=== test_no_size_rotate ===" << std::endl;

    std::string dir = "/tmp/test_log_no_size_rotate";
    std::string file = dir + "/app.log";
    FSUtil::Mkdir(dir);
    CleanDir(dir);

    FileLogAppender::ptr ap(new FileLogAppender(file));
    Logger::ptr logger(new Logger("test_no_size"));
    logger->setLevel(LogLevel::INFO);
    logger->addAppender(ap);

    std::string payload(100, 'z');
    for (int i = 0; i < 50; ++i) {
        WriteLine(logger, "line " + std::to_string(i) + " " + payload);
    }

    assert(FSUtil::Exists(file));
    assert(!FSUtil::Exists(dir + "/app_1.log"));

    std::cout << "PASS" << std::endl;
}

// retention_days 清理带分片序号的过期文件，且不误删近期文件与当前写入文件
void test_retention_cleans_shards() {
    std::cout << "=== test_retention_cleans_shards ===" << std::endl;

    std::string dir = "/tmp/test_log_retention_shards";
    std::string file = dir + "/app.log";
    FSUtil::Mkdir(dir);
    CleanDir(dir);

    // 按天切分，保留 7 天，同时启用大小分片
    FileLogAppender::ptr ap(new FileLogAppender(file, FileLogAppender::DAY, 7, 512));

    auto make_date = [](time_t t) {
        char buf[32];
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        return std::string(buf);
    };
    time_t now = time(0);
    std::string expired_date = make_date(now - 10 * 86400); // 10 天前，超过 7 天
    std::string recent_date = make_date(now - 1 * 86400);   // 1 天前，在保留期内

    // 手工构造两个带分片序号的日志文件
    std::string expired = dir + "/app_" + expired_date + "_1.log";
    std::string recent = dir + "/app_" + recent_date + "_1.log";
    {
        std::ofstream ofs(expired);
        ofs << "old shard log\n";
    }
    {
        std::ofstream ofs(recent);
        ofs << "recent shard log\n";
    }

    ap->cleanOldFiles();

    // 过期分片被删除（日期解析剥离 _1 后缀），近期分片保留
    assert(!FSUtil::Exists(expired));
    assert(FSUtil::Exists(recent));

    // 当前写入文件不被删除
    std::string active = dir + "/" + FSUtil::Basename(ap->getNewFileName(now));
    assert(FSUtil::Exists(active));

    std::cout << "  expired=" << expired << " removed, recent kept, active=" << active << std::endl;
    std::cout << "PASS" << std::endl;
}

int main() {
    test_size_rotate_none();
    test_size_rotate_all_time_types();
    test_no_size_rotate();
    test_retention_cleans_shards();
    std::cout << "ALL PASS" << std::endl;
    return 0;
}
