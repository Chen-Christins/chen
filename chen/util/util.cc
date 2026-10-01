#include "util.h"

#include <csignal>
#include <cstdarg>
#include <cstring>
#include <thread>

#include <unistd.h>
#include <dirent.h>
#include <execinfo.h>
#include <sys/stat.h>

#include "../fiber/fiber.h"
#include "../log/log.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

/* SYS_gettid线程号，SYS_getpid是进程号 */
pid_t GetThreadId() {
    return syscall(SYS_gettid);
}

uint32_t GetFiberId() {
    return Fiber::GetFiberId();
}

void Backtrace(std::vector<std::string>& bt, int size, int skip) {
    void** array = (void**)malloc(sizeof(void*) * size);
    size_t s = ::backtrace(array, size);

    char** strings = backtrace_symbols(array, s);
    if (strings == NULL) {
        ERROR(logger) << "backtrace_symbols error";
        return ;
    }
    for (size_t i = skip; i < s; ++i) {
        bt.push_back(strings[i]);
    }
    free(strings);
    free(array);
}

std::string BacktraceToString(int size, int skip, const std::string& prefix) {
    std::vector<std::string> bt;
    Backtrace(bt, size, skip);
    std::stringstream ss;
    for (size_t i = 0; i < bt.size(); ++i) {
        ss << prefix << bt[i] << std::endl;
    }
    return ss.str();
}

// 平台兼容的 CPU 核心数获取
#if defined(__linux__)
#endif

int32_t GetCPUCount() {
    unsigned int count = std::thread::hardware_concurrency();
#if defined(__linux__)
    if (count == 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n > 0) return static_cast<int32_t>(n);
    }
#endif
    return static_cast<int32_t>(count > 0 ? count : 1);
}
}
