#include "daemon.h"

#include <cstring>

#include <sys/wait.h>

#include "config/config.h"
#include "log/log.h"
#include "util/macro.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<uint32_t>::ptr g_daemon_restart_interval =
    Config::Lookup("daemon.restart_interval", (uint32_t)2, "daemon restart, interval");

std::string ProcessInfo::toString() const {
    std::stringstream ss;
    ss << "[ProcessInfo parent_id=" << parent_id
       << " main_id=" << main_id
       << " parent_start_time=" << Time2Str(parent_start_time)
       << " main_start_time=" << Time2Str(main_start_time)
       << " restart_count=" << restart_count << "]";
    return ss.str();
}

static int real_start(int argc, char** argv, std::function<int(int argc, char** argv)> main_cb) {
    return main_cb(argc, argv);
}

static int real_daemon(int argc, char** argv, std::function<int(int argc, char** argv)> main_cb) {
    int ret = daemon(1, 0);
    ASSERT(ret == 0);
    ProcessInfoMgr::GetInstance()->parent_id = getpid();
    ProcessInfoMgr::GetInstance()->parent_start_time = time(0);

    while (true) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程返回
            ProcessInfoMgr::GetInstance()->main_id = getpid();
            ProcessInfoMgr::GetInstance()->main_start_time = time(0);
            INFO(logger) << "process start pid=" << getpid();

            return real_start(argc, argv, main_cb);
        } else if (pid < 0) {
            ERROR(logger) << "fork fail return=" << pid << " errno"
                << errno << " errstr=" << strerror(errno);
            return -1;
        } else {
            // 父进程返回
            int status = 0;
            waitpid(pid, &status, 0);

            // 如果子进程正常退出，父进程也退出
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                INFO(logger) << "child finished pid=" << pid;
                break;
            }

            // 子进程异常退出，记录并重启
            ERROR(logger) << "child crash pid=" << pid
                << " status=" << status;
            ProcessInfoMgr::GetInstance()->restart_count += 1;
            sleep(g_daemon_restart_interval->getValue());
        }
    }
    return 0;
}

int start_daemon(int argc, char** argv, std::function<int(int argc, char** argv)> main_cb, bool is_daemon) {
    if (!is_daemon) {
        return real_start(argc, argv, main_cb);
    }
    return real_daemon(argc, argv, main_cb);
}

}
