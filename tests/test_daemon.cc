#include "chen/daemon.h"
#include "chen/log/log.h"
#include "chen/iomanager/iomanager.h"

static chen::Logger::ptr logger = LOG_ROOT();

chen::Timer::ptr timer;
int server_main(int argc, char** argv) {
    INFO(logger) << chen::ProcessInfoMgr::GetInstance()->toString();
    chen::IOManager iom(1);
    timer = iom.addTimer(1000, []() {
        INFO(logger) << "onTimer";
        static int count = 0;
        if (++count > 10) {
            timer->cancel();
        }
    }, true);
    return 0;
}

int main(int argc, char** argv) {
    return chen::start_daemon(argc, argv, server_main, argc != 1);
}