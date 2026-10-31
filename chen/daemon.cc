#include "daemon.h"

#include "log/log.h"
#include "util/macro.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

std::string ProcessInfo::toString() const
{
    std::stringstream ss;
    ss << "[ProcessInfo parent_id=" << parent_id << " main_id=" << main_id << " parent_start_time=" << TimeUtil::Time2StrSec(parent_start_time)
       << " main_start_time=" << TimeUtil::Time2StrSec(main_start_time) << " restart_count=" << restart_count << "]";

    return ss.str();
}

static int real_start(int argc, char** argv, std::function<int(int argc, char** argv)> main_cb)
{
    return main_cb(argc, argv);
}

static int real_daemon(int argc, char** argv, std::function<int(int argc, char** argv)> main_cb)
{
    int ret = daemon(1, 0);
    ASSERT(ret == 0);

    ProcessInfoMgr::GetInstance()->parent_id = getpid();
    ProcessInfoMgr::GetInstance()->parent_start_time = time(0);
    ProcessInfoMgr::GetInstance()->main_id = getpid();
    ProcessInfoMgr::GetInstance()->main_start_time = time(0);

    INFO(logger) << "process start pid=" << getpid();

    return real_start(argc, argv, main_cb);
}

int start_daemon(int argc, char** argv, std::function<int(int argc, char** argv)> main_cb, bool is_daemon)
{
    if (!is_daemon) {
        return real_start(argc, argv, main_cb);
    }
    return real_daemon(argc, argv, main_cb);
}

} // namespace chen
