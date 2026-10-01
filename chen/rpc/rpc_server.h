/**
 * @file rpc_server.h
 * @brief RPC服务器定义
 * @author Christins
 * @date 2025-12-03
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "../fiber/fiber.h"
#include "../tcp/tcp_server.h"
#include "../timer/timer.h"
#include "../util/mutex.h"
#include "function_traits.h"
#include "handler_map.h"
#include "protocol.h"
#include "rpc_hub_registry.h"
#include "serializer.h"

namespace chen::rpc {

/**
 * @class RpcServer
 * @brief RPC 服务器：继承自 TcpServer，负责接受连接、读取请求、分发到注册的方法并回包。
 *
 * 特性：
 * - 使用 `registerMethod(name, func)` 注册本地函数为 RPC 接口；
 * - 自动根据函数签名做参数反序列化与返回值序列化（基于 FunctionTraits 与 Serializer）；
 * - 将 `Protocol.header.sequence` 原样回填到响应头部，便于客户端按序匹配；
 * - 默认在 `handleClient` 循环内依次读取请求、执行并发送响应，异常转为 `code=500`。
 */
class RpcServer : public TcpServer {
public:
    typedef std::shared_ptr<RpcServer> ptr;

    /**
     * @brief 构造函数
     * @param worker 业务调度线程（默认当前 IOManager）
     * @param io_worker IO 网络事件调度线程（默认当前 IOManager）
     * @param accept_worker accept 线程（默认当前 IOManager）
     */
    RpcServer(IOManager* worker = IOManager::GetThis(), IOManager* io_worker = IOManager::GetThis(),
              IOManager* accept_worker = IOManager::GetThis());

    /**
     * @brief 注册 RPC 方法
     * @tparam Func 可调用目标类型（支持函数指针、std::function）
     * @param name 方法名（客户端以该名称调用）
     * @param func 本地实现函数，签名形如 R(Args...)
     *
     * 语义说明：
     * - 服务端自动将 `RpcRequest.args` 反序列化为 `Args...` 并调用 `func`；
     * - 若返回 `void`，仅设置 `response.code=0`；否则将返回值序列化到 `response.result`；
     * - 发生异常时，填充 `response.code=500` 与 `response.message`；
     * - 响应协议头的 `sequence` 从请求头原样回填，便于客户端按序匹配。
     */
    template <typename Func>
    void registerMethod(const std::string& name, Func func) {
        m_handlers[HandlerKey{name}] = MakeRpcHandler(func);
    }

    /**
     * @brief 按 CmdID 注册 RPC 方法（body 直接为参数，无方法名字符串）
     * @tparam Func 可调用目标类型
     * @param cmd 命令 ID（非 0）
     * @param func 本地实现函数，签名形如 R(Args...)
     * @details 与按方法名注册共用同一张分发表，variant key 保证互不冲突。
     */
    template <typename Func>
    void registerMethod(uint32_t cmd, Func func) {
        m_handlers[HandlerKey{cmd}] = MakeRpcHandler(func);
    }

    /**
     * @brief 注销 RPC 方法（热重载时避免旧 .so 的 lambda 残留导致 SEGV）
     * @param name 方法名
     */
    void unregisterMethod(const std::string& name) { m_handlers.erase(HandlerKey{name}); }

    /**
     * @brief 按 CmdID 注销 RPC 方法
     * @param cmd 命令 ID
     */
    void unregisterMethod(uint32_t cmd) { m_handlers.erase(HandlerKey{cmd}); }

    void prepareDispatch() override;

    void commitDispatch() override;

    /**
     * @brief 开启中心转发（relay）模式
     * @details 开启后 RpcServer 作为中心 hub：服务连接后先调用 @register 注册，
     *          之后带路由字段的请求会被转发到目标连接并中继回包。
     * @param v 是否开启
     */
    void setRelay(bool v) {
        m_relay = v;
        if (v && !m_registry) {
            m_registry = std::make_shared<RpcHubRegistry>();
        }
    }

    bool isRelay() const { return m_relay; }

    /**
     * @brief 设置中继超时时间（毫秒）
     * @param v 超时毫秒数，0 表示不设超时
     */
    void setRelayTimeout(uint64_t v) { m_relayTimeout = v; }

    uint64_t getRelayTimeout() const { return m_relayTimeout; }

    /**
     * @brief 获取服务注册表（relay 模式）
     */
    RpcHubRegistry::ptr getRegistry() const { return m_registry; }

protected:
    /**
     * @brief 处理客户端请求主循环（覆盖 TcpServer 虚函数）
     * @param client 已建立的客户端 Socket
     */
    void handleClient(Socket::ptr client) override;

private:
    struct ConnRelayState; ///< 前置声明（见下方定义）

    /**
     * @brief relay 模式下处理客户端连接的收包分发循环
     * @param client 客户端 Socket
     */
    void handleRelayClient(Socket::ptr client);

    /**
     * @brief 处理 @register 注册请求
     */
    void handleRegister(Protocol::ptr frame, RpcConnection::ptr conn);

    /**
     * @brief 转发带路由的请求并中继回包（非阻塞）
     * @details 不等待目标回包：中继表登记后立即返回，读循环继续收包，
     *          同一连接可同时有多个 in-flight 转发。目标回包由目标连接的
     *          收包循环走 handleRelayResponse 直接发回调用方。
     */
    void forward(Protocol::ptr req, RpcConnection::ptr caller);

    /**
     * @brief 按路由方式解析目标连接
     */
    RpcConnection::ptr resolveTarget(Protocol::ptr req, RoutingMethod rm);

    /**
     * @brief 构造转发帧（清空路由字段，来源 peer_id 以注册表为准）
     */
    Protocol::ptr buildForwardFrame(Protocol::ptr req, RpcConnection::ptr caller);

    /**
     * @brief 处理目标服务返回的中继响应
     */
    void handleRelayResponse(Protocol::ptr frame, RpcConnection::ptr conn);

    /**
     * @brief 连接断开时失败挂在该连接上的未完成中继
     */
    void failPendingRelays(RpcConnection::ptr conn);

    /// 发送错误/结果响应到指定连接
    void sendCode(RpcConnection::ptr conn, uint32_t sequence, int32_t code, const std::string& msg);

    /// relay 模式下线程安全的发送（按连接串行化写，防止不同 fiber 并发写同一 socket）
    bool relaySend(RpcConnection::ptr conn, Protocol::ptr pkt);

    /// 获取（必要时创建）连接的 relay 状态
    std::shared_ptr<ConnRelayState> getRelayState(RpcConnection::ptr conn);

    /// 分配连接的下一个出站 seq（连接内递增，防碰撞）
    uint32_t nextOutSeq(RpcConnection::ptr conn);

private:
    /// 中继上下文（非阻塞：登记后由回包或超时消费，无等待 fiber）
    struct RelayCtx {
        typedef std::shared_ptr<RelayCtx> ptr;
        RpcConnection::ptr caller_conn;
        uint32_t caller_seq;
        Timer::ptr timer; ///< 超时定时器，响应到达时取消
    };

    /// 单连接的中继状态（锁粒度到连接，避免全局竞争）
    struct ConnRelayState {
        std::mutex mtx;                                   ///< 保护 ctxs / out_seq（短临界区，无 yield）
        std::unordered_map<uint32_t, RelayCtx::ptr> ctxs; ///< 出站 seq -> 中继上下文
        uint32_t out_seq = 0;                             ///< 该连接独立的出站 seq 计数器
        std::shared_ptr<FiberSemaphore> write_sem;        ///< 该连接的写信号量
    };

    // k: 方法名或 CmdID（见 HandlerKey） -> v: 接收 (ByteArray, Protocol.sequence) 并返回响应协议帧
    std::unordered_map<HandlerKey, RpcHandler, HandlerKeyHash> m_handlers;
    /// 热重载时的待提交 handler 表
    std::unordered_map<HandlerKey, RpcHandler, HandlerKeyHash> m_pendingHandlers;
    /// 是否中心转发模式
    bool m_relay = false;
    /// 中继超时（毫秒）
    uint64_t m_relayTimeout = 3000;
    /// 服务注册表
    RpcHubRegistry::ptr m_registry;
    /// conn -> 中继状态（出站 seq + 中继子表 + 写信号量）
    std::unordered_map<RpcConnection::ptr, std::shared_ptr<ConnRelayState>> m_connRelay;
    /// 保护 m_connRelay 的互斥锁（仅 map 查找，短临界区）
    std::mutex m_connRelayMutex;
};

} // namespace chen::rpc
