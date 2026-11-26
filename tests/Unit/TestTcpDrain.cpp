/**
 * @file TestTcpDrain.cpp
 * @brief TcpServer 协作式 drain 的簿记逻辑单测：在途计数、空闲连接判定与回收
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @note 这里只测「计数 / 判定 / 回收」这类纯簿记逻辑，用未连接的 socket 充当连接对象，
 *       不启动 IOManager 线程：真实的 accept → handleClient → drain 主链路由
 *       integration_drain_reload / integration_h2_drain_reload 覆盖。
 */
#include "../TestHarness.hpp"

#include "src/Socket/Socket.hpp"
#include "src/Tcp/TcpServer.hpp"

#include <functional>
#include <memory>

using chen::Socket;
using chen::TcpServer;

namespace {

/**
 * @brief 把 TcpServer 的受保护簿记接口暴露出来，便于直接驱动
 */
class DrainBookkeepingServer : public TcpServer {
public:
    DrainBookkeepingServer()
        : TcpServer(nullptr, nullptr, nullptr)
    {
        setName("drain_unit_server");
    }

    bool supportsGracefulDrain() const override { return true; }

    using TcpServer::addActiveClient;
    using TcpServer::beginRequest;
    using TcpServer::closeIdleClients;
    using TcpServer::endRequest;
    using TcpServer::isClientIdle;

    /// RequestGuard 是 protected 嵌套类型，只对子类可见，这里包一层给测试用
    void runGuarded(const Socket::ptr& conn, const std::function<void()>& fn)
    {
        RequestGuard guard(this, conn);
        fn();
    }
};

/// 一个仍然打开、但从未连接/注册过事件的 socket，用来充当连接对象
Socket::ptr makeIdleSocket()
{
    return Socket::CreateTCPSocket();
}

} // namespace

CHEN_TEST(tcp_drain, counts_in_flight_per_connection)
{
    DrainBookkeepingServer server;
    auto conn = makeIdleSocket();
    CHEN_CHECK(conn != nullptr);

    CHEN_CHECK_EQ(server.getActiveClientCount(), 0u);
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 0u);

    // 正常链路里由 startAccept() 登记，这里手工登记
    server.addActiveClient(conn);
    CHEN_CHECK_EQ(server.getActiveClientCount(), 1u);
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 0u);
    CHEN_CHECK(server.isClientIdle(conn));

    // 同一连接上的两个请求都要计数
    server.beginRequest(conn);
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 1u);
    server.beginRequest(conn);
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 1u); // 按连接计数，不是按请求
    CHEN_CHECK_EQ(server.getActiveClientCount(), 1u);

    server.endRequest(conn);
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 1u); // 还剩一个请求
    CHEN_CHECK(!server.isClientIdle(conn));

    server.endRequest(conn);
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 0u);
    CHEN_CHECK(server.isClientIdle(conn));

    // 未 draining 时连接不被回收，仍算活跃
    CHEN_CHECK_EQ(server.getActiveClientCount(), 1u);
}

CHEN_TEST(tcp_drain, closes_only_idle_connections)
{
    DrainBookkeepingServer server;
    auto busy = makeIdleSocket();
    auto idle = makeIdleSocket();
    CHEN_CHECK(busy != nullptr);
    CHEN_CHECK(idle != nullptr);

    server.addActiveClient(busy);
    server.addActiveClient(idle);
    server.beginRequest(busy);
    server.beginRequest(idle);
    server.endRequest(idle); // idle 连接上已无在途请求
    CHEN_CHECK_EQ(server.getActiveClientCount(), 2u);
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 1u);

    // 只回收空闲连接，busy 那条保留
    CHEN_CHECK_EQ(server.closeIdleClients(), 1u);
    CHEN_CHECK_EQ(server.getActiveClientCount(), 1u);
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 1u);
    CHEN_CHECK(!server.isClientIdle(busy));

    // 在途请求结束后再回收一次
    server.endRequest(busy);
    CHEN_CHECK_EQ(server.closeIdleClients(), 1u);
    CHEN_CHECK_EQ(server.getActiveClientCount(), 0u);

    // 已经摘掉的连接视为空闲，重复回收是空操作
    CHEN_CHECK(server.isClientIdle(idle));
    CHEN_CHECK_EQ(server.closeIdleClients(), 0u);
}

CHEN_TEST(tcp_drain, request_guard_balances_count)
{
    DrainBookkeepingServer server;
    auto conn = makeIdleSocket();
    CHEN_CHECK(conn != nullptr);

    server.addActiveClient(conn);

    size_t in_flight_in_scope = 0;
    bool idle_in_scope = true;
    server.runGuarded(conn, [&]() {
        in_flight_in_scope = server.getInFlightClientCount();
        idle_in_scope = server.isClientIdle(conn);
    });
    CHEN_CHECK_EQ(in_flight_in_scope, 1u);
    CHEN_CHECK(!idle_in_scope);
    // guard 析构后计数归零
    CHEN_CHECK_EQ(server.getInFlightClientCount(), 0u);
    CHEN_CHECK(server.isClientIdle(conn));
    CHEN_CHECK(!server.isDraining());
}

CHEN_TEST(tcp_drain, supports_graceful_drain_defaults_to_false)
{
    // 基类默认不支持 drain：未覆写的自定义 server 仍走 closeAllClients() 的旧路径
    TcpServer server(nullptr, nullptr, nullptr);
    CHEN_CHECK(!server.supportsGracefulDrain());
    CHEN_CHECK(!server.isDraining());
}

CHEN_TEST_MAIN()
