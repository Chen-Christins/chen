#include "chen/schedule/schedule.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

void test_schedule() {
    static int count = 5;
    INFO(logger) << "-----test in fiber-----" << count;

    if (--count > 0) {
        chen::Scheduler::GetThis()->schedule(&test_schedule, chen::GetThreadId());
    }
}

int main(int argc, char** argv) {
    // logger->setLevel(chen::LogLevel::INFO);
    chen::Thread::SetName("main");
    INFO(logger) << "main start";
    chen::Scheduler sc(1, false, "work");
    sc.start();
    sleep(2);
    INFO(logger) << "schedule";
    sc.schedule(&test_schedule);
    sc.stop();
    sleep(1);
    INFO(logger) << "main end";
    return 0;
}