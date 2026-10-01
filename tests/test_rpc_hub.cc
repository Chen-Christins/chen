/**
 * @file test_rpc_hub.cc
 * @brief RpcServer 中心转发（relay 模式）集成测试
 * @details 起一个 hub + 两个 mock 服务 + 一个调用方，验证
 *          DIRECT / GROUPID(固定拓扑) / BIND_ID / BROADCAST 路由与中继回包。
 */
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/rpc/rpc_client.h"
#include "chen/rpc/rpc_server.h"
#include "chen/util/time_util.h"

static chen::Logger::ptr logger = LOG_ROOT();
static std::atomic<uint32_t> g_broadcast_hits{0};
static std::atomic<uint32_t> g_notify_hits{0};
static int g_failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            ERROR(logger) << "CHECK FAILED: " << #cond << " at line " << __LINE__; \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

/**
 * @brief 协程内睡眠（定时器唤醒，不占线程）
 */
static void FiberSleep(uint64_t ms) {
    chen::Fiber::ptr fiber = chen::Fiber::GetThis();
    chen::Scheduler* scheduler = chen::Scheduler::GetThis();
    chen::IOManager::GetThis()->addTimer(ms, [fiber, scheduler]() { scheduler->schedule(fiber); }, false);
    chen::Fiber::YieldToHold();
}

static chen::rpc::RpcRouting RoutingDirect(uint32_t dst_peer_id) {
    chen::rpc::RpcRouting routing;
    routing.method = chen::rpc::RoutingMethod::DIRECT;
    routing.dst_peer_id = dst_peer_id;
    return routing;
}

static chen::rpc::RpcRouting RoutingGroupId(uint32_t func_id, uint32_t group_id) {
    chen::rpc::RpcRouting routing;
    routing.method = chen::rpc::RoutingMethod::GROUPID;
    routing.func_id = func_id;
    routing.group_id = group_id;
    return routing;
}

static chen::rpc::RpcRouting RoutingBindId(uint32_t func_id, uint32_t bind_id) {
    chen::rpc::RpcRouting routing;
    routing.method = chen::rpc::RoutingMethod::BIND_ID;
    routing.func_id = func_id;
    routing.bind_id = bind_id;
    return routing;
}

static chen::rpc::RpcRouting RoutingBroadcast(uint32_t func_id) {
    chen::rpc::RpcRouting routing;
    routing.method = chen::rpc::RoutingMethod::BROADCAST;
    routing.func_id = func_id;
    return routing;
}

static chen::rpc::RpcClient::ptr ConnectService(uint32_t peer_id, uint32_t func_id, uint32_t instance_id,
                                                std::vector<uint32_t> bind_ids) {
    auto client = std::make_shared<chen::rpc::RpcClient>();
    chen::Address::ptr addr = chen::Address::LookupAny("127.0.0.1:18080");
    if (!addr || !client->connect(addr)) {
        ERROR(logger) << "connect hub failed";
        return nullptr;
    }
    client->setHeartbeatInterval(10000);
    if (!client->registerService(peer_id, func_id, instance_id, bind_ids)) {
        ERROR(logger) << "registerService failed peer_id=" << peer_id;
        return nullptr;
    }
    return client;
}

void run() {
    // ---- M1: 帧头扩展编解码 round-trip ----
    {
        chen::rpc::Protocol::ptr p(new chen::rpc::Protocol);
        p->routing = static_cast<uint8_t>(chen::rpc::RoutingMethod::GROUPID);
        p->real_random = 1;
        p->sequence = 42;
        p->cmd = 0xABCD;
        p->src_peer_id = 11;
        p->dst_peer_id = 22;
        p->func_id = 7;
        p->group_id = 99;
        p->bind_id = 88;
        p->region = 1;
        p->body = "hello body";
        auto ba = chen::rpc::Protocol::Encode(p);
        auto decoded = chen::rpc::Protocol::Decode(ba);
        CHECK(decoded != nullptr);
        CHECK(decoded->routing == static_cast<uint8_t>(chen::rpc::RoutingMethod::GROUPID));
        CHECK(decoded->real_random == 1);
        CHECK(decoded->sequence == 42);
        CHECK(decoded->cmd == 0xABCD);
        CHECK(decoded->src_peer_id == 11);
        CHECK(decoded->dst_peer_id == 22);
        CHECK(decoded->func_id == 7);
        CHECK(decoded->group_id == 99);
        CHECK(decoded->bind_id == 88);
        CHECK(decoded->region == 1);
        CHECK(decoded->body == "hello body");
        CHECK(decoded->length == p->body.size());
    }

    // ---- 启动 hub（relay 模式） ----
    chen::Address::ptr hub_addr = chen::Address::LookupAny("127.0.0.1:18080");
    if (!hub_addr) {
        ERROR(logger) << "get hub address error";
        return;
    }
    chen::rpc::RpcServer::ptr hub(new chen::rpc::RpcServer);
    hub->setRelay(true);
    hub->setRelayTimeout(2000);
    while (!hub->bind(hub_addr)) {
        ERROR(logger) << "bind hub " << *hub_addr << " fail";
        sleep(1);
    }
    hub->registerMethod("hub_ping", []() { return std::string("pong"); });
    hub->registerMethod(0x10, [](uint32_t v) { return v + 100; });
    hub->start();

    // ---- 两个 mock 服务 ----
    // 服务 A: func 1 instance 1，绑定业务 ID 100
    auto svc_a = ConnectService(1001, 1, 1, {100});
    CHECK(svc_a != nullptr);
    // 服务 B: func 1 instance 2，绑定业务 ID 200
    auto svc_b = ConnectService(2002, 1, 2, {200});
    CHECK(svc_b != nullptr);
    if (!svc_a || !svc_b) {
        return;
    }
    svc_a->registerMethod("whoami", []() { return (uint32_t)1001; });
    svc_a->registerMethod("echo", [](const std::string& s) { return "echo: " + s; });
    svc_a->registerMethod("broadcast_hit", []() { g_broadcast_hits.fetch_add(1); });
    svc_a->registerMethod("notify_hit", []() { g_notify_hits.fetch_add(1); });
    svc_a->registerMethod("slow", []() { FiberSleep(1000); return (uint32_t)1001; });
    svc_a->registerMethod(0x01, [](uint32_t v) { return v * 2; });
    svc_a->registerMethod(0x02, []() { return (uint32_t)1001; });
    svc_a->registerMethod(0x03, []() { g_notify_hits.fetch_add(1); });
    svc_b->registerMethod("whoami", []() { return (uint32_t)2002; });
    svc_b->registerMethod("broadcast_hit", []() { g_broadcast_hits.fetch_add(1); });
    svc_b->registerMethod("notify_hit", []() { g_notify_hits.fetch_add(1); });
    svc_b->registerMethod(0x02, []() { return (uint32_t)2002; });

    // ---- 调用方 ----
    auto caller = ConnectService(9001, 9, 1, {});
    CHECK(caller != nullptr);
    if (!caller) {
        return;
    }

    // relay 模式下 routing=NONE 仍走 hub 本地 handler
    std::string ping = caller->call<std::string>("hub_ping");
    CHECK(ping == "pong");
    INFO(logger) << "hub_ping = " << ping;

    // ---- DIRECT ----
    uint32_t who = caller->callRouted<uint32_t>(RoutingDirect(1001), "whoami");
    CHECK(who == 1001);
    INFO(logger) << "DIRECT 1001 whoami = " << who;

    who = caller->callRouted<uint32_t>(RoutingDirect(2002), "whoami");
    CHECK(who == 2002);
    INFO(logger) << "DIRECT 2002 whoami = " << who;

    std::string echoed = caller->callRouted<std::string>(RoutingDirect(1001), "echo", "hello hub");
    CHECK(echoed == "echo: hello hub");
    INFO(logger) << "DIRECT echo = " << echoed;

    // 未知 peer -> 404
    try {
        caller->callRouted<uint32_t>(RoutingDirect(9999), "whoami");
        CHECK(false);
    } catch (std::exception& e) {
        INFO(logger) << "DIRECT unknown peer throws: " << e.what();
        CHECK(std::string(e.what()).find("no route target") != std::string::npos);
    }

    // 目标服务返回 404，经 hub 中继回调用方
    try {
        caller->callRouted<uint32_t>(RoutingDirect(1001), "no_such_method");
        CHECK(false);
    } catch (std::exception& e) {
        INFO(logger) << "relay 404 throws: " << e.what();
        CHECK(std::string(e.what()).find("Method not found") != std::string::npos);
    }

    // ---- GROUPID 固定拓扑: InstanceID = 1 + group_id % max_instance(2) ----
    who = caller->callRouted<uint32_t>(RoutingGroupId(1, 0), "whoami");
    CHECK(who == 1001); // 1 + 0%2 = 1 -> instance1 -> peer 1001
    INFO(logger) << "GROUPID func=1 gid=0 whoami = " << who;

    who = caller->callRouted<uint32_t>(RoutingGroupId(1, 1), "whoami");
    CHECK(who == 2002); // 1 + 1%2 = 2 -> instance2 -> peer 2002
    INFO(logger) << "GROUPID func=1 gid=1 whoami = " << who;

    // ---- BIND_ID ----
    who = caller->callRouted<uint32_t>(RoutingBindId(1, 100), "whoami");
    CHECK(who == 1001);
    INFO(logger) << "BIND_ID func=1 bind=100 whoami = " << who;

    // ---- A1: hub 非阻塞转发，慢请求(→svc_a 1000ms)在途时快请求(→svc_b)不被串行阻塞 ----
    chen::IOManager::GetThis()->schedule([caller]() {
        try {
            caller->callRouted<uint32_t>(RoutingDirect(1001), "slow");
        } catch (...) {
        }
    });
    FiberSleep(100);  // 让慢请求先到达 hub 并在途
    uint64_t t0 = chen::GetCurrentMs();
    who = caller->callRouted<uint32_t>(RoutingDirect(2002), "whoami");
    uint64_t fast_ms = chen::GetCurrentMs() - t0;
    CHECK(who == 2002);
    CHECK(fast_ms < 500);  // 慢请求 1000ms，快请求应远早于它返回
    INFO(logger) << "A1 fast call elapsed " << fast_ms << "ms (slow=1000ms)";
    FiberSleep(1100);  // 等慢请求完成，释放 svc_a

    // ---- BROADCAST（单向，即时回 code=0，fan-out 异步送达） ----
    g_broadcast_hits = 0;
    caller->callRouted<void>(RoutingBroadcast(1), "broadcast_hit");
    FiberSleep(300);
    INFO(logger) << "broadcast hits = " << g_broadcast_hits.load();
    CHECK(g_broadcast_hits.load() == 2);

    // ---- NOTIFY 单向（fire-and-forget，无回包） ----
    g_notify_hits = 0;
    CHECK(caller->notifyRouted(RoutingDirect(1001), "notify_hit"));
    FiberSleep(200);
    INFO(logger) << "notify direct hits = " << g_notify_hits.load();
    CHECK(g_notify_hits.load() == 1);

    g_notify_hits = 0;
    caller->notifyRouted(RoutingBroadcast(1), "notify_hit");
    FiberSleep(200);
    INFO(logger) << "notify broadcast hits = " << g_notify_hits.load();
    CHECK(g_notify_hits.load() == 2);

    // ---- CmdID 分派（body 直接为参数，无方法名字符串） ----
    // hub 本地 cmd 处理器
    uint32_t cmd_ret = caller->call<uint32_t>(0x10, 1);
    CHECK(cmd_ret == 101);
    INFO(logger) << "call cmd=0x10(1) = " << cmd_ret;

    // 经 hub 路由到目标服务的 cmd 处理器
    cmd_ret = caller->callRouted<uint32_t>(RoutingDirect(1001), 0x01, 21);
    CHECK(cmd_ret == 42);
    INFO(logger) << "callRouted DIRECT 1001 cmd=0x01(21) = " << cmd_ret;

    who = caller->callRouted<uint32_t>(RoutingDirect(1001), 0x02);
    CHECK(who == 1001);
    INFO(logger) << "callRouted DIRECT 1001 cmd=0x02 = " << who;

    who = caller->callRouted<uint32_t>(RoutingGroupId(1, 1), 0x02);
    CHECK(who == 2002);
    INFO(logger) << "callRouted GROUPID func=1 gid=1 cmd=0x02 = " << who;

    // CmdID 单向 NOTIFY
    g_notify_hits = 0;
    CHECK(caller->notifyRouted(RoutingDirect(1001), 0x03));
    FiberSleep(200);
    INFO(logger) << "notify by cmd hits = " << g_notify_hits.load();
    CHECK(g_notify_hits.load() == 1);

    // 未知 CmdID -> 404
    try {
        caller->call<uint32_t>(0xFFFF, 1);
        CHECK(false);
    } catch (std::exception& e) {
        INFO(logger) << "unknown cmd throws: " << e.what();
        CHECK(std::string(e.what()).find("Cmd not found") != std::string::npos);
    }

    // ---- 异步路由调用（回调式）：发起后立即返回，完成时回调被调度为独立协程 ----
    {
        chen::FiberSemaphore done{0};
        std::atomic<int> async_ok{0};

        caller->callRoutedAsync<uint32_t>(RoutingDirect(1001), "whoami"
                , [&](chen::rpc::RpcResult<uint32_t> r) {
            if (r.ok() && r.value == 1001) {
                ++async_ok;
            }
            INFO(logger) << "[async] DIRECT 1001 whoami = " << r.value
                << " status=" << static_cast<int>(r.status) << " code=" << r.code;
            done.notify();
        });

        caller->callRoutedAsync<std::string>(RoutingDirect(1001), "echo"
                , [&](chen::rpc::RpcResult<std::string> r) {
            if (r.ok() && r.value == "echo: hello async") {
                ++async_ok;
            }
            INFO(logger) << "[async] DIRECT echo = " << r.value;
            done.notify();
        }, "hello async");

        caller->callRoutedAsync<uint32_t>(RoutingDirect(1001), 0x01
                , [&](chen::rpc::RpcResult<uint32_t> r) {
            if (r.ok() && r.value == 42) {
                ++async_ok;
            }
            INFO(logger) << "[async] DIRECT cmd=0x01(21) = " << r.value;
            done.notify();
        }, 21);

        caller->callRoutedAsync<uint32_t>(RoutingGroupId(1, 1), 0x02
                , [&](chen::rpc::RpcResult<uint32_t> r) {
            if (r.ok() && r.value == 2002) {
                ++async_ok;
            }
            INFO(logger) << "[async] GROUPID gid=1 cmd=0x02 = " << r.value;
            done.notify();
        });

        // 异步错误路径：未知 peer 经 hub 回 404，回调收到业务错误码而非抛异常
        caller->callRoutedAsyncWithTimeout<uint32_t>(2000, RoutingDirect(9999), "whoami"
                , [&](chen::rpc::RpcResult<uint32_t> r) {
            if (!r.ok() && r.code == 404) {
                ++async_ok;
            }
            INFO(logger) << "[async] unknown peer code=" << r.code
                << " err=" << r.error;
            done.notify();
        });

        for (int i = 0; i < 5; ++i) {
            done.wait();
        }
        INFO(logger) << "[async] routed completed " << async_ok << "/5";
        CHECK(async_ok.load() == 5);
    }

    // ---- Future 路由调用：并发发起，之后按顺序 get() 结果 ----
    {
        auto f_direct = caller->callRoutedFuture<uint32_t>(RoutingDirect(1001), "whoami");
        auto f_echo = caller->callRoutedFuture<std::string>(RoutingDirect(1001), "echo", "hi future");
        auto f_cmd = caller->callRoutedFuture<uint32_t>(RoutingDirect(1001), 0x01, 21);
        auto f_group = caller->callRoutedFuture<uint32_t>(RoutingGroupId(1, 1), 0x02);
        auto f_err = caller->callRoutedFutureWithTimeout<uint32_t>(2000, RoutingDirect(9999), "whoami");

        auto r_direct = f_direct.get();
        CHECK(r_direct.ok() && r_direct.value == 1001);
        INFO(logger) << "[future] DIRECT 1001 whoami = " << r_direct.value;

        auto r_echo = f_echo.get();
        CHECK(r_echo.ok() && r_echo.value == "echo: hi future");
        INFO(logger) << "[future] DIRECT echo = " << r_echo.value;

        auto r_cmd = f_cmd.get();
        CHECK(r_cmd.ok() && r_cmd.value == 42);
        INFO(logger) << "[future] DIRECT cmd=0x01(21) = " << r_cmd.value;

        auto r_group = f_group.get();
        CHECK(r_group.ok() && r_group.value == 2002);
        INFO(logger) << "[future] GROUPID gid=1 cmd=0x02 = " << r_group.value;

        auto r_err = f_err.get();
        CHECK(!r_err.ok() && r_err.code == 404);
        INFO(logger) << "[future] unknown peer code=" << r_err.code << " err=" << r_err.error;
    }

    // ---- 中继超时：目标慢处理时 hub 回 504 ----
    hub->setRelayTimeout(300);
    try {
        caller->callRouted<uint32_t>(RoutingDirect(1001), "slow");
        CHECK(false);
    } catch (std::exception& e) {
        INFO(logger) << "relay timeout throws: " << e.what();
        CHECK(std::string(e.what()).find("relay timeout") != std::string::npos);
    }
    hub->setRelayTimeout(2000);
    FiberSleep(100);

    // ---- 断线清理：svc_a 断开后 DIRECT 1001 应 404 ----
    svc_a->close();
    FiberSleep(200);
    try {
        caller->callRouted<uint32_t>(RoutingDirect(1001), "whoami");
        CHECK(false);
    } catch (std::exception& e) {
        INFO(logger) << "after disconnect throws: " << e.what();
        CHECK(std::string(e.what()).find("no route target") != std::string::npos);
    }

    if (g_failures == 0) {
        INFO(logger) << "ALL HUB TESTS PASSED";
    } else {
        ERROR(logger) << g_failures << " CHECK(S) FAILED";
    }

    svc_b->close();
    caller->close();
    hub->stop();
}

int main(int argc, char** argv) {
    chen::IOManager iom(2);
    std::atomic<bool> done{false};
    iom.schedule([&done]() {
        run();
        done.store(true);
    });
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    iom.stop();
    INFO(logger) << "hub test finished, failures=" << g_failures;
    return g_failures == 0 ? 0 : 1;
}