#include <iostream>
#include "chen/log/log.h"

static chen::Logger::ptr logger1 = LOG_ROOT();
static chen::Logger::ptr logger2 = LOG_NAME("system");

int main(int argc, char** argv) {
    chen::Logger::ptr logger(new chen::Logger);
    logger->setFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T%r%T[%p]%T[%c]%T%f:%l%T%m%n");
    logger->addAppender(chen::LogAppender::ptr(new chen::StdoutLogAppender));
    TRACE(logger) << "nihao";
    INFO(logger) << "nihao";
    DEBUG(logger) << "nihao";
    WARN(logger) << "nihao";
    ERROR(logger) << "nihao";
    FATAL(logger) << "nihao";

    INFO(logger1) << "hello chen";
    INFO(logger2) << "hello chen";

    int a = 10;
    int b = 20;
    TRACE(logger, "trace-format a={}, b={}", a, b);
    INFO("a = {}, b = {}", a, b);
    DEBUG(logger, "logger-format a={}, b={}", a, b);
    WARN(logger1, "escape braces: {{}} and value={}", a + b);
    ERROR(logger2, "string={}, num={}", std::string("hello"), 42);

    // sleep(10);

    std::cout << "Hello Chen" << std::endl;
    
    return 0;
}