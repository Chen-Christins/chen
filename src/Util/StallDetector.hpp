/**
 * @file StallDetector.hpp
 * @brief 调度线程 stall 检测
 * @author Christins
 * @date 2026-11-28
 * @copyright GPL-3.0
 * @details 每个调度线程在事件循环里打心跳（原子写，不加锁）；空闲地阻塞在
 *          epoll_wait 里的线程会标记为 idle，不参与判定。巡检方（worker 的
 *          housekeeping 定时器）发现某线程超过阈值没有回到事件循环，就把线程名、
 *          停留时长、当前协程 id、最后一次操作打出来——这是 std::mutex 跨让出
 *          这类"线程被钉死"问题的第一现场信息，通常比事后 gdb 快得多。
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Noncopyable.hpp"

namespace chen {

/**
 * @brief 线程 stall 检测器
 */
class StallDetector : public Noncopyable {
public:
    /// 线程最后一次停留的操作，用于 stall 报告
    enum Op {
        OP_NONE = 0,
        OP_EPOLL,
        OP_READ,
        OP_WRITE,
        OP_CONNECT,
        OP_ACCEPT,
        OP_SLEEP,
        OP_WAIT_LOCK,
    };

    static StallDetector* GetInstance();

    static const char* opName(int op);

    /**
     * @brief 注册当前线程（同一线程重复调用是幂等的）
     * @param name 线程名，出现在报告里
     */
    void registerThread(const std::string& name);

    /**
     * @brief 注销当前线程
     */
    void unregisterThread();

    /**
     * @brief 打心跳，标记当前线程仍在事件循环里
     */
    void heartbeat();

    /**
     * @brief 标记当前线程进入/离开空闲阻塞（epoll_wait）
     * @details 空闲线程不计入 stall，避免把正常空闲误报成卡死
     */
    void setIdle(bool idle);

    /**
     * @brief 记录当前正在执行的协程 id
     */
    void setFiber(uint64_t fiber_id);

    /**
     * @brief 记录当前线程最后一次会阻塞的操作
     */
    void setOp(int op, int fd = -1);

    void clearOp();

    /**
     * @brief 巡检，返回 stall 报告
     * @param warn_ms 超过该时长没有心跳即视为 stall
     * @return std::string 空串表示没有 stall；同一段 stall 只报告一次
     */
    std::string check(uint64_t warn_ms);

    /**
     * @brief 所有已注册线程的快照，用于 /debug 端点或日志
     */
    std::string dump() const;

    size_t threadCount() const;

private:
    StallDetector();
    ~StallDetector();

    /// 实现细节：每个线程一块状态（定义在 .cc，这里只存不透明指针）
    mutable std::mutex mutex_;
    std::unordered_map<int64_t, void*> threads_;
};

} // namespace chen
