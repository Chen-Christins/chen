/**
 * @file daemon.h
 * @brief 守护进程
 * @author Christins
 * @date 2025-02-12
 */
#pragma once

#include <functional>
#include <unistd.h>

#include "util/singleton.h"

namespace chen {

struct ProcessInfo {
    /// 父进程id
    pid_t parent_id = 0;
    /// 主进程id
    pid_t main_id = 0;
    /// 父进程启动时间
    uint64_t parent_start_time = 0;
    /// 主进程启动时间
    uint64_t main_start_time = 0;
    /// 主进程重启次数
    uint32_t restart_count = 0;
    /// 转换为字符串
    std::string toString() const;
};

typedef Singleton<ProcessInfo> ProcessInfoMgr;

/**
 * @brief 启动逻辑
 * @param argc 参数个数
 * @param argv 参数值
 * @param main_cb 回调函数
 * @param is_daemon 是否以守护进程的形式启动
 * @return int 是否启动成功 -1失败|0成功
 */
int start_daemon(int argc, char** argv, std::function<int(int argc, char** argv)> main_cb, bool is_daemon);

}
