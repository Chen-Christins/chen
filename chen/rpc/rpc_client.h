/**
 * @file rpc_client.h
 * @brief 简单 RPC 客户端封装：连接、同步/异步调用、超时控制与接收循环
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>

#include "../fiber/fiber.h"
#include "../socket/address.h"
#include "../socket/socket.h"
#include "../timer/timer.h"
#include "../util/mutex.h"
#include "function_traits.h"
#include "handler_map.h"
#include "protocol.h"
#include "rpc_connection.h"
#include "rpc_future.h"
#include "serializer.h"

namespace chen::rpc {

/**
 * @brief 中心转发（Hub）路由描述
 * @details 通过 hub 转发时使用；普通点对点 RPC 保持默认（NONE）即可。
 */
struct RpcRouting {
    RoutingMethod method = RoutingMethod::NONE; ///< 路由方式
    uint32_t dst_peer_id = 0;                   ///< DIRECT 目标对端身份
    uint32_t func_id = 0;                       ///< GROUPID/BROADCAST/BIND_ID 功能类型
    uint32_t group_id = 0;                      ///< GROUPID 分片键
    uint32_t bind_id = 0;                       ///< BIND_ID 绑定业务 ID
    bool real_random = false;                   ///< GROUPID 动态拓扑标记
};

/**
 * @class RpcClient
 * @brief RPC 客户端：负责与服务端建立 TCP 连接、发送请求、按序列号收取响应、并提供同步/异步调用接口。
 *
 * 使用方式：
 * - 先调用 connect() 建立连接；
 * - 通过 call()/callWithTimeout() 发起同步 RPC 调用；
 * - 通过 callAsync()/callAsyncWithTimeout() 发起异步 RPC 调用（回调式，不阻塞调用方）；
 * - 方法名与 CmdID 共用同一组接口：首参传方法名字符串按方法名分派，传 uint32_t 命令号
 *   按 CmdID 分派（body 直接为参数），与 registerMethod 的两个重载一一对应；
 * - 调用 close() 主动关闭连接；
 *
 * 线程安全性：
 * - 多协程/多线程并发调用 call()/callAsync() 是安全的（内部使用互斥量保护待响应映射表）；
 * - 单个 RpcClient 实例对应一条底层连接。
 */
class RpcClient : public std::enable_shared_from_this<RpcClient> {
public:
    typedef std::shared_ptr<RpcClient> ptr;

    /**
     * @brief 构造/析构
     */
    RpcClient(bool auto_heartbeat = true);
    ~RpcClient();

    /**
     * @brief 连接到 RPC 服务端
     * @param addr 服务端地址（如 "127.0.0.1:8080" 对应的 Address）
     * @param timeout_ms 连接超时（毫秒），默认 -1 表示使用 Socket 默认超时
     * @return 连接是否成功
     */
    bool connect(Address::ptr addr, uint64_t timeout_ms = -1);

    /**
     * @brief 关闭连接（安全可重入）
     */
    void close();

    /**
     * @brief 同步调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param method 远程方法名
     * @param args 传入的参数
     * @return R 返回的结果（若 R=void 则无返回）
     * @throw std::runtime_error 发送失败、超时、或服务端返回非 0 错误码
     */
    template <typename R, typename... Args>
    R call(const std::string& method, Args... args) {
        return callWithTimeout<R>(3000, method, args...);
    }

    /**
     * @brief 按 CmdID 同步调用，默认超时 3000ms（body 直接为参数，无方法名字符串）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return R 返回的结果（若 R=void 则无返回）
     * @throw std::invalid_argument cmd 为 0
     * @throw std::runtime_error 发送失败、超时、或服务端返回非 0 错误码
     */
    template <typename R, typename... Args>
    R call(uint32_t cmd, Args... args) {
        return callWithTimeout<R>(3000, cmd, args...);
    }

    /**
     * @brief 同步调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param method 远程方法名
     * @param args 传入的参数
     * @return R 返回的结果（若 R=void 则无返回）
     * @throw std::runtime_error 发送失败、超时、反序列化失败，或服务端返回非 0 错误码
     */
    template <typename R, typename... Args>
    R callWithTimeout(uint64_t timeout_ms, const std::string& method, Args... args) {
        Protocol::ptr resp = callInternal(buildNameRequest(method, args...), timeout_ms);
        return ParseCallResult<R>(resp);
    }

    /**
     * @brief 按 CmdID 同步调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return R 返回的结果（若 R=void 则无返回）
     * @throw std::invalid_argument cmd 为 0
     * @throw std::runtime_error 发送失败、超时、反序列化失败，或服务端返回非 0 错误码
     */
    template <typename R, typename... Args>
    R callWithTimeout(uint64_t timeout_ms, uint32_t cmd, Args... args) {
        Protocol::ptr resp = callInternal(buildCmdRequest(cmd, args...), timeout_ms);
        return ParseCallResult<R>(resp);
    }

    /**
     * @brief 检查底层连接是否还存活
     * @return true 连接正常
     */
    bool isConnected() const { return m_session && m_session->isConnected(); }

    /**
     * @brief 设置心跳间隔
     * @param ms 毫秒
     */
    void setHeartbeatInterval(uint64_t ms) { m_heartbeat_ms = ms; }

    /**
     * @brief 向中心转发服务（hub）注册本服务
     * @param peer_id 对端身份（配置分配，按 hub 唯一）
     * @param func_id 功能类型
     * @param instance_id 实例编号
     * @param bind_ids 绑定的业务 ID 列表
     * @return bool 注册是否成功
     * @details 内部调用 hub 的 @register 保留方法。注册成功后 hub 才能将
     *          其它服务发来的请求转发到本连接。
     */
    bool registerService(uint32_t peer_id, uint32_t func_id, uint32_t instance_id, const std::vector<uint32_t>& bind_ids);

    /**
     * @brief 通过 hub 按路由方式调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param routing 路由描述（DIRECT/GROUPID/BROADCAST/BIND_ID）
     * @param method 远程方法名
     * @param args 传入的参数
     * @return R 返回的结果（若 R=void 则无返回）
     * @throw std::runtime_error 发送失败、超时、或服务端返回非 0 错误码
     */
    template <typename R, typename... Args>
    R callRouted(const RpcRouting& routing, const std::string& method, Args... args) {
        return callRoutedWithTimeout<R>(3000, routing, method, args...);
    }

    /**
     * @brief 通过 hub 按路由方式 + CmdID 调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param routing 路由描述（DIRECT/GROUPID/BROADCAST/BIND_ID）
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return R 返回的结果
     * @throw std::invalid_argument cmd 为 0
     * @throw std::runtime_error 发送失败、超时、或服务端返回非 0 错误码
     */
    template <typename R, typename... Args>
    R callRouted(const RpcRouting& routing, uint32_t cmd, Args... args) {
        return callRoutedWithTimeout<R>(3000, routing, cmd, args...);
    }

    /**
     * @brief 通过 hub 按路由方式调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param routing 路由描述
     * @param method 远程方法名
     * @param args 传入的参数
     * @return R 返回的结果（若 R=void 则无返回）
     * @throw std::runtime_error 发送失败、超时、或服务端返回非 0 错误码
     */
    template <typename R, typename... Args>
    R callRoutedWithTimeout(uint64_t timeout_ms, const RpcRouting& routing, const std::string& method, Args... args) {
        Protocol::ptr req = buildNameRequest(method, args...);
        applyRouting(req, routing);

        Protocol::ptr resp = callInternal(req, timeout_ms);
        return ParseCallResult<R>(resp);
    }

    /**
     * @brief 通过 hub 按路由方式 + CmdID 调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param routing 路由描述
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return R 返回的结果（若 R=void 则无返回）
     * @throw std::invalid_argument cmd 为 0
     * @throw std::runtime_error 发送失败、超时、或服务端返回非 0 错误码
     */
    template <typename R, typename... Args>
    R callRoutedWithTimeout(uint64_t timeout_ms, const RpcRouting& routing, uint32_t cmd, Args... args) {
        Protocol::ptr req = buildCmdRequest(cmd, args...);
        applyRouting(req, routing);

        Protocol::ptr resp = callInternal(req, timeout_ms);
        return ParseCallResult<R>(resp);
    }

    /**
     * @brief 单向通知（fire-and-forget）：通过 hub 按路由方式发送，不等回包
     * @tparam Args 参数类型包
     * @param routing 路由描述
     * @param method 远程方法名
     * @param args 传入的参数
     * @return bool 发送是否成功（只保证写入连接，不保证目标处理结果）
     */
    template <typename... Args>
    bool notifyRouted(const RpcRouting& routing, const std::string& method, Args... args) {
        Protocol::ptr req = buildNameRequest(method, args...);
        req->type = MessageType::NOTIFY;
        applyRouting(req, routing);
        return sendProtocol(req);
    }

    /**
     * @brief 单向通知（fire-and-forget）：通过 hub 按路由方式 + CmdID 发送，不等回包
     * @tparam Args 参数类型包
     * @param routing 路由描述
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return bool 发送是否成功（只保证写入连接，不保证目标处理结果）
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename... Args>
    bool notifyRouted(const RpcRouting& routing, uint32_t cmd, Args... args) {
        Protocol::ptr req = buildCmdRequest(cmd, args...);
        req->type = MessageType::NOTIFY;
        applyRouting(req, routing);
        return sendProtocol(req);
    }

    // ─── 异步调用（回调式）：发起后立即返回，完成时回调被调度为独立协程执行 ───

    /**
     * @brief 异步调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param method 远程方法名
     * @param cb 完成回调，参数为 RpcResult<R>
     * @param args 传入的参数
     * @throw std::invalid_argument 方法名为空（编程错误，与同步调用一致）
     */
    template <typename R, typename... Args>
    void callAsync(const std::string& method, RpcCallback<R> cb, Args... args) {
        callAsyncWithTimeout<R>(3000, method, std::move(cb), args...);
    }

    /**
     * @brief 按 CmdID 异步调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param cmd 命令 ID（非 0）
     * @param cb 完成回调，参数为 RpcResult<R>
     * @param args 传入的参数
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename R, typename... Args>
    void callAsync(uint32_t cmd, RpcCallback<R> cb, Args... args) {
        callAsyncWithTimeout<R>(3000, cmd, std::move(cb), args...);
    }

    /**
     * @brief 异步调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param method 远程方法名
     * @param cb 完成回调
     * @param args 传入的参数
     */
    template <typename R, typename... Args>
    void callAsyncWithTimeout(uint64_t timeout_ms, const std::string& method
            , RpcCallback<R> cb, Args... args) {
        dispatchAsync<R>(buildNameRequest(method, args...), timeout_ms, std::move(cb));
    }

    /**
     * @brief 按 CmdID 异步调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param cmd 命令 ID（非 0）
     * @param cb 完成回调
     * @param args 传入的参数
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename R, typename... Args>
    void callAsyncWithTimeout(uint64_t timeout_ms, uint32_t cmd
            , RpcCallback<R> cb, Args... args) {
        dispatchAsync<R>(buildCmdRequest(cmd, args...), timeout_ms, std::move(cb));
    }

    /**
     * @brief 通过 hub 按路由方式异步调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param routing 路由描述
     * @param method 远程方法名
     * @param cb 完成回调
     * @param args 传入的参数
     */
    template <typename R, typename... Args>
    void callRoutedAsync(const RpcRouting& routing, const std::string& method
            , RpcCallback<R> cb, Args... args) {
        callRoutedAsyncWithTimeout<R>(3000, routing, method, std::move(cb), args...);
    }

    /**
     * @brief 通过 hub 按路由方式 + CmdID 异步调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param routing 路由描述
     * @param cmd 命令 ID（非 0）
     * @param cb 完成回调
     * @param args 传入的参数
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename R, typename... Args>
    void callRoutedAsync(const RpcRouting& routing, uint32_t cmd
            , RpcCallback<R> cb, Args... args) {
        callRoutedAsyncWithTimeout<R>(3000, routing, cmd, std::move(cb), args...);
    }

    /**
     * @brief 通过 hub 按路由方式异步调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param routing 路由描述
     * @param method 远程方法名
     * @param cb 完成回调
     * @param args 传入的参数
     */
    template <typename R, typename... Args>
    void callRoutedAsyncWithTimeout(uint64_t timeout_ms, const RpcRouting& routing
            , const std::string& method, RpcCallback<R> cb, Args... args) {
        Protocol::ptr req = buildNameRequest(method, args...);
        applyRouting(req, routing);

        dispatchAsync<R>(req, timeout_ms, std::move(cb));
    }

    /**
     * @brief 通过 hub 按路由方式 + CmdID 异步调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param routing 路由描述
     * @param cmd 命令 ID（非 0）
     * @param cb 完成回调
     * @param args 传入的参数
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename R, typename... Args>
    void callRoutedAsyncWithTimeout(uint64_t timeout_ms, const RpcRouting& routing
            , uint32_t cmd, RpcCallback<R> cb, Args... args) {
        Protocol::ptr req = buildCmdRequest(cmd, args...);
        applyRouting(req, routing);

        dispatchAsync<R>(req, timeout_ms, std::move(cb));
    }

    // ─── Future 调用：发起后立即返回句柄，get() 时挂起当前协程等待结果 ───

    /**
     * @brief Future 调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param method 远程方法名
     * @param args 传入的参数
     * @return RpcFuture<R> 句柄，get() 挂起当前协程等待结果
     * @throw std::invalid_argument 方法名为空
     */
    template <typename R, typename... Args>
    RpcFuture<R> callFuture(const std::string& method, Args... args) {
        return callFutureWithTimeout<R>(3000, method, args...);
    }

    /**
     * @brief 按 CmdID Future 调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return RpcFuture<R> 句柄，get() 挂起当前协程等待结果
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename R, typename... Args>
    RpcFuture<R> callFuture(uint32_t cmd, Args... args) {
        return callFutureWithTimeout<R>(3000, cmd, args...);
    }

    /**
     * @brief Future 调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param method 远程方法名
     * @param args 传入的参数
     * @return RpcFuture<R> 句柄
     */
    template <typename R, typename... Args>
    RpcFuture<R> callFutureWithTimeout(uint64_t timeout_ms, const std::string& method, Args... args) {
        return dispatchFuture<R>(buildNameRequest(method, args...), timeout_ms);
    }

    /**
     * @brief 按 CmdID Future 调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return RpcFuture<R> 句柄
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename R, typename... Args>
    RpcFuture<R> callFutureWithTimeout(uint64_t timeout_ms, uint32_t cmd, Args... args) {
        return dispatchFuture<R>(buildCmdRequest(cmd, args...), timeout_ms);
    }

    /**
     * @brief 通过 hub 按路由方式 Future 调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param routing 路由描述
     * @param method 远程方法名
     * @param args 传入的参数
     * @return RpcFuture<R> 句柄
     */
    template <typename R, typename... Args>
    RpcFuture<R> callRoutedFuture(const RpcRouting& routing, const std::string& method, Args... args) {
        return callRoutedFutureWithTimeout<R>(3000, routing, method, args...);
    }

    /**
     * @brief 通过 hub 按路由方式 + CmdID Future 调用，默认超时 3000ms
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param routing 路由描述
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return RpcFuture<R> 句柄
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename R, typename... Args>
    RpcFuture<R> callRoutedFuture(const RpcRouting& routing, uint32_t cmd, Args... args) {
        return callRoutedFutureWithTimeout<R>(3000, routing, cmd, args...);
    }

    /**
     * @brief 通过 hub 按路由方式 Future 调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param routing 路由描述
     * @param method 远程方法名
     * @param args 传入的参数
     * @return RpcFuture<R> 句柄
     */
    template <typename R, typename... Args>
    RpcFuture<R> callRoutedFutureWithTimeout(uint64_t timeout_ms, const RpcRouting& routing
            , const std::string& method, Args... args) {
        Protocol::ptr req = buildNameRequest(method, args...);
        applyRouting(req, routing);

        return dispatchFuture<R>(req, timeout_ms);
    }

    /**
     * @brief 通过 hub 按路由方式 + CmdID Future 调用（自定义超时）
     * @tparam R 返回值类型（可为 void）
     * @tparam Args 参数类型包
     * @param timeout_ms 请求级超时（毫秒），<=0 表示不设置定时器
     * @param routing 路由描述
     * @param cmd 命令 ID（非 0）
     * @param args 传入的参数
     * @return RpcFuture<R> 句柄
     * @throw std::invalid_argument cmd 为 0
     */
    template <typename R, typename... Args>
    RpcFuture<R> callRoutedFutureWithTimeout(uint64_t timeout_ms, const RpcRouting& routing
            , uint32_t cmd, Args... args) {
        Protocol::ptr req = buildCmdRequest(cmd, args...);
        applyRouting(req, routing);

        return dispatchFuture<R>(req, timeout_ms);
    }

    /**
     * @brief 按方法名注册本地方法处理器（接收 hub 转发来的请求）
     * @tparam Func 可调用目标类型（支持函数指针、std::function、lambda）
     * @param name 方法名（调用方以该方法名调用）
     * @param func 本地实现函数，签名形如 R(Args...)
     * @details 服务既作为调用方又作为被调方时使用：hub 会把其它服务转发的
     *          请求通过本连接送达，recvLoop 收到 REQUEST 后按方法名分发。
     */
    template <typename Func>
    void registerMethod(const std::string& name, Func func) {
        std::lock_guard lock(m_mutex);
        m_requestHandlers[HandlerKey{name}] = MakeRpcHandler(func);
    }

    /**
     * @brief 按 CmdID 注册本地方法处理器（body 直接为参数，无方法名字符串）
     * @tparam Func 可调用目标类型
     * @param cmd 命令 ID（非 0）
     * @param func 本地实现函数，签名形如 R(Args...)
     * @details 与按方法名注册共用同一张分发表，variant key 保证互不冲突。
     */
    template <typename Func>
    void registerMethod(uint32_t cmd, Func func) {
        std::lock_guard lock(m_mutex);
        m_requestHandlers[HandlerKey{cmd}] = MakeRpcHandler(func);
    }

    /**
     * @brief 注销本地方法处理器（热重载时避免旧 .so 的 lambda 残留）
     * @param name 方法名
     */
    void unregisterMethod(const std::string& name) {
        std::lock_guard lock(m_mutex);
        m_requestHandlers.erase(HandlerKey{name});
    }

    /**
     * @brief 按 CmdID 注销本地方法处理器
     * @param cmd 命令 ID
     */
    void unregisterMethod(uint32_t cmd) {
        std::lock_guard lock(m_mutex);
        m_requestHandlers.erase(HandlerKey{cmd});
    }

    /**
     * @brief 线程安全发送协议帧（按连接串行化写，防止不同 fiber 并发写同一 socket）
     */
    bool sendProtocol(Protocol::ptr pkt);

private:
    struct ResponseContext {
        typedef std::shared_ptr<ResponseContext> ptr;
        /** 收到的响应帧（可能为空，表示超时/断开） */
        Protocol::ptr rsp;
        /** 等待当前请求的协程（同步调用路径） */
        Fiber::ptr fiber;
        /** 协程所属的调度器（用于唤醒/调度回调） */
        Scheduler* scheduler;
        /** 异步完成回调（非空即为异步请求）；参数为传输层状态与响应帧 */
        std::function<void(RpcStatus, Protocol::ptr)> callback;
        /** 本请求的超时定时器（异步路径用于完成时取消，避免定时器堆积） */
        Timer::ptr timer;
    };

    /**
     * @brief 组一个方法名分派的请求帧（cmd=0，body=[method_vint][args...]）
     * @tparam Args 参数类型包
     * @param method 远程方法名（为空抛 invalid_argument）
     * @param args 传入的参数
     * @return Protocol::ptr 请求帧（type=REQUEST，序列号已递增）
     */
    template <typename... Args>
    Protocol::ptr buildNameRequest(const std::string& method, Args... args) {
        if (method.empty()) {
            throw std::invalid_argument("RPC method name cannot be empty");
        }

        Protocol::ptr req(new Protocol);
        req->type = MessageType::REQUEST;
        req->sequence = ++m_seq;
        req->body = Serializer::EncodeRequest(method, args...);
        return req;
    }

    /**
     * @brief 组一个 CmdID 分派的请求帧（body 直接为参数，无方法名字符串）
     * @tparam Args 参数类型包
     * @param cmd 命令 ID（为 0 抛 invalid_argument，0 保留给方法名分派）
     * @param args 传入的参数
     * @return Protocol::ptr 请求帧（type=REQUEST，序列号已递增）
     */
    template <typename... Args>
    Protocol::ptr buildCmdRequest(uint32_t cmd, Args... args) {
        if (cmd == 0) {
            throw std::invalid_argument("RPC cmd cannot be 0");
        }

        Protocol::ptr req(new Protocol);
        req->type = MessageType::REQUEST;
        req->cmd = cmd;
        req->sequence = ++m_seq;
        req->body = Serializer::SerializeArgs(args...);
        return req;
    }

    /**
     * @brief 将路由描述应用到请求帧头
     */
    static void applyRouting(Protocol::ptr req, const RpcRouting& routing) {
        req->routing = static_cast<uint8_t>(routing.method);
        req->dst_peer_id = routing.dst_peer_id;
        req->func_id = routing.func_id;
        req->group_id = routing.group_id;
        req->bind_id = routing.bind_id;
        req->real_random = routing.real_random ? 1 : 0;
    }

    /**
     * @brief 解析响应帧，返回业务结果或抛出错误
     * @tparam R 返回值类型
     */
    template <typename R>
    static R ParseCallResult(const Protocol::ptr& resp) {
        ByteArray::ptr rba(new ByteArray);
        rba->writeStringWithoutLength(resp->body);
        rba->setPosition(0);

        int32_t code = rba->readFint32();
        if (code != 0) {
            throw std::runtime_error(rba->readStringVint());
        }
        if constexpr (std::is_void_v<R>) {
            return;
        } else {
            R result;
            Serializer::Read(rba, result);
            return result;
        }
    }
    /**
     * @brief 解析异步响应帧为 RpcResult（不抛异常，错误统一写入结果）
     * @tparam R 返回值类型（可为 void）
     * @param status 传输层状态
     * @param resp 响应帧（status != OK 时可为空）
     * @return RpcResult<R> 调用结果
     */
    template <typename R>
    static RpcResult<R> ParseAsyncResult(RpcStatus status, const Protocol::ptr& resp) {
        RpcResult<R> result;
        result.status = status;
        if (status != RpcStatus::OK) {
            result.error = StatusToString(status);
            return result;
        }
        try {
            ByteArray::ptr rba(new ByteArray);
            rba->writeStringWithoutLength(resp->body);
            rba->setPosition(0);

            int32_t code = rba->readFint32();
            result.code = code;
            if (code != 0) {
                result.error = rba->readStringVint();
                return result;
            }
            if constexpr (!std::is_void_v<R>) {
                Serializer::Read(rba, result.value);
            }
        } catch (std::exception& e) {
            result.status = RpcStatus::DECODE_ERROR;
            result.error = e.what();
        }
        return result;
    }

    /**
     * @brief 异步调用统一入口：编码后的请求 + 超时 + 类型化回调
     * @tparam R 返回值类型
     * @param req 编码好的协议帧（包含序列号与消息体）
     * @param timeout_ms 超时（毫秒），<=0 表示不设置定时器
     * @param cb 完成回调（被调度为独立协程执行）
     */
    template <typename R>
    void dispatchAsync(Protocol::ptr req, uint64_t timeout_ms, RpcCallback<R> cb) {
        callInternalAsync(req, timeout_ms,
            [cb = std::move(cb)](RpcStatus status, Protocol::ptr resp) mutable {
                cb(ParseAsyncResult<R>(status, resp));
            });
    }

    /**
     * @brief Future 调用统一入口：编码后的请求 + 超时，返回可等待的 RpcFuture
     * @tparam R 返回值类型
     * @param req 编码好的协议帧（包含序列号与消息体）
     * @param timeout_ms 超时（毫秒），<=0 表示不设置定时器
     * @return RpcFuture<R> 可调用 get()/getValue()/ready()
     */
    template <typename R>
    RpcFuture<R> dispatchFuture(Protocol::ptr req, uint64_t timeout_ms) {
        auto state = std::make_shared<RpcFutureState<R>>();
        callInternalAsync(req, timeout_ms,
            [state](RpcStatus status, Protocol::ptr resp) {
                state->complete(ParseAsyncResult<R>(status, resp));
            });
        return RpcFuture<R>(state);
    }

    /**
     * @brief 发送请求并阻塞等待响应或超时
     * @param req 编码好的协议帧（包含序列号与消息体）
     * @param timeout_ms 超时（毫秒）
     * @return Protocol::ptr 收到的响应帧
     * @throw std::runtime_error 发送失败或超时
     */
    Protocol::ptr callInternal(Protocol::ptr req, uint64_t timeout_ms);

    /**
     * @brief 发送请求并立即返回，响应/超时/断开时通过回调通知
     * @param req 编码好的协议帧（包含序列号与消息体）
     * @param timeout_ms 超时（毫秒），<=0 表示不设置定时器
     * @param callback 完成回调：参数为传输层状态与响应帧（失败时响应帧为空）
     */
    void callInternalAsync(Protocol::ptr req, uint64_t timeout_ms,
                           std::function<void(RpcStatus, Protocol::ptr)> callback);

    /**
     * @brief 从待响应表中取出并移除指定序列号的上下文
     * @param sequence 请求序列号
     * @return ResponseContext::ptr 命中的上下文；未命中返回 nullptr
     * @details 保证超时/响应/断开三路只有一个能取到上下文，避免重复完成
     */
    ResponseContext::ptr takePending(uint32_t sequence);

    /**
     * @brief 完成一个请求：唤醒等待协程或调度异步回调
     * @param ctx 请求上下文
     * @param status 传输层状态
     * @param rsp 响应帧（失败时可为空）
     */
    void completeRequest(ResponseContext::ptr ctx, RpcStatus status, Protocol::ptr rsp);

    /**
     * @brief 接收循环：后台协程不断读取响应，根据序列号唤醒等待的调用方
     * @param weak_this 弱引用，避免循环引用导致内存泄露
     * @param session 连接会话
     */
    static void recvLoop(std::weak_ptr<RpcClient> weak_this, RpcConnection::ptr session);

private:
    /** 底层 Socket */
    Socket::ptr m_sock;
    /** 封装的连接流（读写协议帧） */
    RpcConnection::ptr m_session;
    /** 是否自动心跳 */
    bool m_auto_heartbeat = true;
    /** 递增的请求序列号（从 1 开始，原子操作支持多协程/多线程并发） */
    std::atomic<uint32_t> m_seq = 0;
    /** 连续心跳丢失计数器 */
    uint32_t m_hbMissed = 0;
    /** 连续丢失多少次心跳后断开连接 */
    static constexpr uint32_t MAX_HB_MISSED = 3;
    /** 最大待响应请求数，防止服务端宕机时内存无限增长 */
    static constexpr uint32_t MAX_PENDING_REQUESTS = 65536;
    /** 保护等待队列与状态的互斥量 */
    std::mutex m_mutex;
    /** 写信号量（callInternal / 心跳 / recvLoop 回包并发写同一 socket，wait 让出协程） */
    FiberSemaphore m_writeSem{1};
    /* 待响应请求表：key 为序列号，value 为等待上下文。调用发出时登记，收到响应或超时后移除。*/
    std::unordered_map<uint32_t, ResponseContext::ptr> m_pending_requests;
    /** 本地请求处理器表（接收 hub 转发来的 REQUEST）：key 为方法名或 CmdID，见 HandlerKey */
    std::unordered_map<HandlerKey, RpcHandler, HandlerKeyHash> m_requestHandlers;
    // 心跳定时器
    Timer::ptr m_hb_timer;
    // 心跳间隔
    uint64_t m_heartbeat_ms = 30000;
};

} // namespace chen::rpc
