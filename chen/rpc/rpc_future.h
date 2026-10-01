/**
 * @file rpc_future.h
 * @brief RPC 异步结果类型与 Future 句柄：支持并发发起、之后在协程内等待结果
 * @author chen
 * @date 2026-09-17
 */
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../fiber/fiber.h"
#include "../schedule/schedule.h"

namespace chen::rpc {

/**
 * @brief 异步调用传输层状态码
 * @details 仅表示本地传输层结果；服务端业务错误码通过 RpcResult::code 表达。
 */
enum class RpcStatus : int32_t {
    OK = 0,                 ///< 成功收到响应
    TIMEOUT = -1,           ///< 请求超时
    SEND_FAILED = -2,       ///< 发送失败
    CONNECTION_CLOSED = -3, ///< 连接断开
    DECODE_ERROR = -4,      ///< 响应反序列化失败
};

/**
 * @brief 传输层状态码转可读字符串
 */
inline const char* StatusToString(RpcStatus status) {
    switch (status) {
        case RpcStatus::OK:                return "ok";
        case RpcStatus::TIMEOUT:           return "request timeout";
        case RpcStatus::SEND_FAILED:       return "send request failed";
        case RpcStatus::CONNECTION_CLOSED: return "connection closed";
        case RpcStatus::DECODE_ERROR:      return "decode response failed";
    }
    return "unknown error";
}

/**
 * @brief 异步调用结果
 * @tparam R 返回值类型（可为 void，见下方特化）
 * @details status 表示传输层结果，code 表示服务端业务码（0=成功）。
 *          仅当 ok() 为 true 时 value 有效。
 */
template <typename R>
struct RpcResult {
    RpcStatus status = RpcStatus::OK; ///< 传输层结果
    int32_t code = 0;                 ///< 服务端业务码（0=成功）
    std::string error;                ///< 错误信息
    R value{};                        ///< 业务返回值（ok() 为 true 时有效）

    /**
     * @brief 是否调用成功（传输层与业务层均成功）
     */
    bool ok() const { return status == RpcStatus::OK && code == 0; }
};

/**
 * @brief 异步调用结果（void 返回值特化，无 value 字段）
 */
template <>
struct RpcResult<void> {
    RpcStatus status = RpcStatus::OK; ///< 传输层结果
    int32_t code = 0;                 ///< 服务端业务码（0=成功）
    std::string error;                ///< 错误信息

    bool ok() const { return status == RpcStatus::OK && code == 0; }
};

/**
 * @brief 异步调用回调类型
 * @tparam R 返回值类型
 */
template <typename R>
using RpcCallback = std::function<void(RpcResult<R>)>;

/**
 * @brief Future 共享状态：保存结果并唤醒等待的协程
 * @tparam R 返回值类型
 * @details 由 RpcClient 的完成回调写入，由 RpcFuture::get() 读取。
 *          支持多个协程同时等待同一结果。
 */
template <typename R>
struct RpcFutureState {
    typedef std::shared_ptr<RpcFutureState> ptr;

    /**
     * @brief 填充结果并唤醒所有等待的协程（由完成回调调用）
     * @param result 调用结果
     */
    void complete(RpcResult<R> result) {
        std::lock_guard lock(m_mutex);
        m_result = std::move(result);
        m_ready = true;
        for (auto& waiter : m_waiters) {
            waiter.first->schedule(waiter.second);
        }
        m_waiters.clear();
    }

    /**
     * @brief 非阻塞查询结果是否就绪
     */
    bool ready() const {
        std::lock_guard lock(m_mutex);
        return m_ready;
    }

    /**
     * @brief 等待结果（挂起当前协程，不阻塞线程）
     * @return RpcResult<R> 调用结果
     * @details 必须在 fiber/调度器上下文调用；结果就绪时立即返回，可多次调用。
     */
    RpcResult<R> get() {
        std::unique_lock lock(m_mutex);
        if (!m_ready) {
            m_waiters.emplace_back(Scheduler::GetThis(), Fiber::GetThis());
            lock.unlock();
            Fiber::YieldToHold();
            lock.lock();
        }
        return m_result;
    }

    mutable std::mutex m_mutex;                                 ///< 保护状态与等待队列
    bool m_ready = false;                                       ///< 结果是否就绪
    RpcResult<R> m_result;                                      ///< 调用结果
    std::vector<std::pair<Scheduler*, Fiber::ptr>> m_waiters;   ///< 等待中的协程
};

/**
 * @class RpcFuture
 * @brief RPC 异步调用句柄：发起后立即返回，get() 时挂起当前协程等待结果
 * @tparam R 返回值类型（可为 void）
 *
 * 用法：
 * - 通过 RpcClient::callFuture() 系列接口获得；
 * - get() 返回 RpcResult<R>（不抛异常）；getValue() 成功返回 value、失败抛异常；
 * - ready() 可非阻塞查询是否完成。
 */
template <typename R>
class RpcFuture {
public:
    typedef std::shared_ptr<RpcFuture> ptr;

    /**
     * @brief 构造函数
     * @param state 共享状态
     */
    explicit RpcFuture(std::shared_ptr<RpcFutureState<R>> state)
        : m_state(std::move(state)) {}

    /**
     * @brief 等待并返回调用结果（挂起当前协程）
     */
    RpcResult<R> get() { return m_state->get(); }

    /**
     * @brief 非阻塞查询结果是否就绪
     */
    bool ready() const { return m_state->ready(); }

    /**
     * @brief 等待并返回业务值
     * @return R 业务返回值（R=void 时无返回）
     * @throw std::runtime_error 传输层失败或服务端返回非 0 错误码
     */
    R getValue() {
        RpcResult<R> result = m_state->get();
        if (!result.ok()) {
            throw std::runtime_error(result.error.empty()
                ? StatusToString(result.status) : result.error);
        }
        if constexpr (!std::is_void_v<R>) {
            return result.value;
        }
    }

private:
    std::shared_ptr<RpcFutureState<R>> m_state; ///< 共享状态
};

} // namespace chen::rpc
