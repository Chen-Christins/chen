/**
 * @file test_log_reload_formatter.cc
 * @brief 测试热重载删除 logger 级 formatter 后恢复默认格式
 * @author Christins
 * @date 2026-11-06
 */
#include "chen/config/config.h"
#include "chen/log/log.h"
#include <cassert>
#include <iostream>

using namespace chen;

namespace {

constexpr const char* kLoggerName = "reload_formatter_test";
constexpr const char* kCustomPattern = "%m%n";
constexpr const char* kDefaultPattern = "%d{%Y-%m-%d %H:%M:%S.%f}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n";

void LoadLogs(const char* yaml)
{
    Config::LoadFromYaml(YAML::Load(yaml));
}

} // namespace

int main()
{
    // 1. 首次配置带自定义 formatter
    LoadLogs(R"(
logs:
  - name: reload_formatter_test
    level: info
    formatter: "%m%n"
    appenders:
      - type: StdoutLogAppender
)");

    Logger::ptr logger = LOG_NAME(kLoggerName);
    assert(logger->getFormatter()->getPattern() == kCustomPattern);

    // 2. 删除 formatter，应恢复默认格式
    LoadLogs(R"(
logs:
  - name: reload_formatter_test
    level: info
    appenders:
      - type: StdoutLogAppender
)");

    logger = LOG_NAME(kLoggerName);
    assert(logger->getFormatter()->getPattern() == kDefaultPattern);

    // 3. 删除整个 logger 后重新添加且不带 formatter，也应使用默认格式
    LoadLogs("logs: []");

    LoadLogs(R"(
logs:
  - name: reload_formatter_test
    level: info
    appenders:
      - type: StdoutLogAppender
)");

    logger = LOG_NAME(kLoggerName);
    assert(logger->getFormatter()->getPattern() == kDefaultPattern);

    std::cout << "test_log_reload_formatter passed" << std::endl;
    return 0;
}
