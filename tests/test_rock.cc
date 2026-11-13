/**
 * @file test_rock.cc
 * @brief Rock 协议端到端测试
 * @author Christins
 * @date 2026-11-13
 * @copyright GPL-3.0
 */

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

#include "chen/config/config.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/module/module.h"
#include "chen/rock/rock_server.h"
#include "chen/rock/rock_stream.h"
#include "chen/socket/address.h"
#include "chen/socket/socket.h"
#include "chen/streams/static_service_discovery.h"

static chen::Logger::ptr logger = LOG_ROOT();
static std::atomic<int> g_notify_cmd{0};
static int g_failed = 0;

#define CHECK(cond, msg)                       \
    do {                                       \
        if (cond) {                            \
            INFO(logger) << "[PASS] " << msg;  \
        } else {                               \
            ERROR(logger) << "[FAIL] " << msg; \
            ++g_failed;                        \
        }                                      \
    } while (0)

enum {
    CMD_ECHO = 100,
    CMD_TIMEOUT = 200,
    CMD_CLOSE = 300,
};

/**
 * @brief 测试模块：覆盖回显、延迟响应、返回 false 关闭连接、通知
 */
class TestRockModule : public chen::RockModule {
public:
    TestRockModule()
        : RockModule("test_rock", "1.0", "")
    {
    }

    bool handleRockRequest(chen::RockRequest::ptr request, chen::RockResponse::ptr response, chen::RockStream::ptr stream) override
    {
        switch (request->getCmd()) {
        case CMD_ECHO:
            response->setResult(0);
            response->setResultStr("ok");
            response->setBody("echo:" + request->getBody());
            return true;
        case CMD_TIMEOUT:
            // 比客户端超时更晚响应，用于验证 TIMEOUT 分支
            ::sleep(1);
            response->setResult(0);
            response->setBody("late");
            return true;
        case CMD_CLOSE:
            response->setResult(0);
            return false;
        default:
            response->setResult(404);
            response->setResultStr("unknown cmd");
            return false;
        }
    }

    bool handleRockNotify(chen::RockNotify::ptr notify, chen::RockStream::ptr stream) override
    {
        g_notify_cmd = notify->getNotify();
        return true;
    }
};

/**
 * @brief 创建一个不自动重连的 Rock 客户端流
 */
static chen::RockStream::ptr create_client(const chen::Address::ptr& addr)
{
    auto sock = chen::Socket::CreateTCP(addr);
    if (!sock || !sock->connect(addr)) {
        ERROR(logger) << "connect fail, addr=" << *addr;
        return nullptr;
    }
    chen::RockStream::ptr stream(new chen::RockStream(sock));
    stream->start();
    return stream;
}

static void test_static_service_discovery()
{
    INFO(logger) << "=== test_static_service_discovery ===";

    chen::StaticServiceDiscovery sd;
    chen::StaticServiceDiscovery::Conf conf;
    conf["astartes"]["rock"] = {{"rock-1", "127.0.0.1:19001", 100}};
    sd.setConf(conf);

    int cb_count = 0;
    std::size_t last_size = 0;
    uint32_t last_weight = 0;
    std::string last_domain;
    std::string last_service;
    sd.setServiceCallback([&](const std::string& domain, const std::string& service,
                              const std::unordered_map<uint64_t, chen::ServiceItemInfo::ptr>& old_value,
                              const std::unordered_map<uint64_t, chen::ServiceItemInfo::ptr>& new_value) {
        ++cb_count;
        last_size = new_value.size();
        last_domain = domain;
        last_service = service;
        for (auto& [id, info] : new_value) {
            last_weight = info->getWeight();
        }
    });

    sd.start();
    CHECK(sd.isStarted(), "static discovery started");
    CHECK(cb_count == 1 && last_size == 1, "static discovery publish conf");
    CHECK(last_domain == "astartes" && last_service == "rock", "static discovery domain/service");
    CHECK(last_weight == 100, "static discovery weight");

    // 配置热更新
    auto conf_var = chen::Config::Lookup<chen::StaticServiceDiscovery::Conf>("service_discovery.domains");
    CHECK(conf_var != nullptr, "service_discovery.domains config exists");
    if (conf_var) {
        chen::StaticServiceDiscovery::Conf hot_conf;
        hot_conf["astartes"]["rock"] = {{"rock-1", "127.0.0.1:19001", 100}, {"rock-2", "127.0.0.1:19002", 300}};
        conf_var->setValue(hot_conf);
        CHECK(last_size == 2, "static discovery hot reload publish");
    }

    CHECK(sd.addServer("astartes", "rock", "127.0.0.1:19003"), "static discovery add server");
    CHECK(last_size == 3, "static discovery add published");

    CHECK(sd.delServer("astartes", "rock", "127.0.0.1:19001"), "static discovery del server");
    CHECK(last_size == 2, "static discovery del published");

    sd.stop();
    CHECK(!sd.isStarted(), "static discovery stopped");
}

static void test_rock_request(const chen::Address::ptr& addr)
{
    INFO(logger) << "=== test_rock_request ===";

    auto stream = create_client(addr);
    CHECK(stream != nullptr, "connect rock server");
    if (!stream) {
        return;
    }

    chen::RockRequest::ptr req(new chen::RockRequest);
    req->setSn(1);
    req->setCmd(CMD_ECHO);
    req->setBody("hello rock");

    auto result = stream->request(req, 2000);
    CHECK(result && result->result == 0, "request result=0");
    CHECK(result && result->response != nullptr, "request got response");
    if (result && result->response) {
        CHECK(result->response->getResult() == 0, "response result=0");
        CHECK(result->response->getResultStr() == "ok", "response result_str=ok");
        CHECK(result->response->getBody() == "echo:hello rock", "response body echo");
        CHECK(result->response->getSn() == req->getSn(), "response sn match");
        CHECK(result->response->getCmd() == req->getCmd(), "response cmd match");
    }

    stream->close();
}

static void test_rock_gzip(const chen::Address::ptr& addr)
{
    INFO(logger) << "=== test_rock_gzip ===";

    auto stream = create_client(addr);
    CHECK(stream != nullptr, "connect rock server for gzip");
    if (!stream) {
        return;
    }

    // 超过 gzip_min_length(4KB)，触发压缩分支
    std::string body(8 * 1024, 'a');
    chen::RockRequest::ptr req(new chen::RockRequest);
    req->setSn(2);
    req->setCmd(CMD_ECHO);
    req->setBody(body);

    auto result = stream->request(req, 3000);
    CHECK(result && result->result == 0, "gzip request result=0");
    CHECK(result && result->response != nullptr, "gzip request got response");
    if (result && result->response) {
        CHECK(result->response->getBody() == "echo:" + body, "gzip body round-trip");
    }

    stream->close();
}

static void test_rock_notify(const chen::Address::ptr& addr)
{
    INFO(logger) << "=== test_rock_notify ===";

    auto stream = create_client(addr);
    CHECK(stream != nullptr, "connect rock server for notify");
    if (!stream) {
        return;
    }

    g_notify_cmd = 0;
    chen::RockNotify::ptr notify(new chen::RockNotify);
    notify->setNotify(7);
    notify->setBody("notify body");
    CHECK(stream->sendMessage(notify) > 0, "send notify");

    for (int i = 0; i < 20 && g_notify_cmd != 7; ++i) {
        usleep(50 * 1000);
    }
    CHECK(g_notify_cmd == 7, "notify handled by module");

    stream->close();
}

static void test_rock_timeout(const chen::Address::ptr& addr)
{
    INFO(logger) << "=== test_rock_timeout ===";

    auto stream = create_client(addr);
    CHECK(stream != nullptr, "connect rock server for timeout");
    if (!stream) {
        return;
    }

    chen::RockRequest::ptr req(new chen::RockRequest);
    req->setSn(3);
    req->setCmd(CMD_TIMEOUT);

    auto result = stream->request(req, 100);
    CHECK(result && result->result == chen::AsyncSocketStream::TIMEOUT, "request timeout");

    stream->close();
}

static void test_rock_bad_magic(const chen::Address::ptr& addr)
{
    INFO(logger) << "=== test_rock_bad_magic ===";

    auto sock = chen::Socket::CreateTCP(addr);
    CHECK(sock != nullptr, "create raw socket");
    if (!sock) {
        return;
    }
    CHECK(sock->connect(addr), "raw socket connect");
    sock->setRecvTimeout(1000);

    // magic/version/length 全错，服务端应解析失败并关闭连接
    uint8_t bad[8] = {0};
    CHECK(sock->send(bad, sizeof(bad)) == (int)sizeof(bad), "send bad frame");

    char buffer[16];
    int n = sock->recv(buffer, sizeof(buffer));
    CHECK(n <= 0, "server closed connection on bad magic");

    sock->close();
}

static void test_rock_close(const chen::Address::ptr& addr)
{
    INFO(logger) << "=== test_rock_close ===";

    auto stream = create_client(addr);
    CHECK(stream != nullptr, "connect rock server for close");
    if (!stream) {
        return;
    }

    chen::RockRequest::ptr req(new chen::RockRequest);
    req->setSn(4);
    req->setCmd(CMD_CLOSE);
    stream->request(req, 500);

    bool closed = false;
    for (int i = 0; i < 20; ++i) {
        if (!stream->isConnected()) {
            closed = true;
            break;
        }
        chen::RockRequest::ptr req2(new chen::RockRequest);
        req2->setSn(100 + i);
        req2->setCmd(CMD_ECHO);
        auto result = stream->request(req2, 200);
        if (result && result->result == chen::AsyncSocketStream::NOT_CONNECT) {
            closed = true;
            break;
        }
        usleep(50 * 1000);
    }
    CHECK(closed, "server closed connection when handler returns false");

    stream->close();
}

static std::mutex g_blackhole_mutex;
static std::vector<chen::Socket::ptr> g_blackhole_clients;

/**
 * @brief 接受连接但永不回包，模拟“连接在、服务假死”的实例
 */
class BlackholeServer : public chen::TcpServer {
public:
    BlackholeServer(chen::IOManager* worker, chen::IOManager* io_worker, chen::IOManager* accept_worker)
        : TcpServer(worker, io_worker, accept_worker)
    {
        m_type = "blackhole";
    }

    void handleClient(chen::Socket::ptr client) override
    {
        std::lock_guard lock(g_blackhole_mutex);
        g_blackhole_clients.push_back(client);
    }
};

static void test_rock_failover(chen::IOManager* iom)
{
    INFO(logger) << "=== test_rock_failover ===";

    chen::TcpServer::ptr blackhole(new BlackholeServer(iom, iom, iom));
    auto blackhole_addr = chen::Address::LookupAny("127.0.0.1:18081");
    bool bound = false;
    for (int i = 0; i < 50; ++i) {
        if (blackhole->bind(blackhole_addr)) {
            bound = true;
            break;
        }
        usleep(200 * 1000);
    }
    CHECK(bound, "bind blackhole server");
    if (!bound) {
        return;
    }
    blackhole->start();

    auto sd = std::make_shared<chen::StaticServiceDiscovery>();
    chen::StaticServiceDiscovery::Conf conf;
    conf["astartes"]["rock"] = {{"healthy", "127.0.0.1:18080", 100}, {"blackhole", "127.0.0.1:18081", 100}};
    sd->setConf(conf);

    chen::RockSDLoadBalance lb(sd);
    lb.start({{"astartes", {{"rock", "round_robin"}}}});

    // 等待两个实例的客户端连接建立
    int connected = 0;
    for (int i = 0; i < 40; ++i) {
        usleep(50 * 1000);
        auto status = lb.statusString();
        connected = 0;
        std::size_t pos = 0;
        while ((pos = status.find("is_connected=1", pos)) != std::string::npos) {
            ++connected;
            ++pos;
        }
        if (connected >= 2) {
            break;
        }
    }
    CHECK(connected >= 2, "load balance connected to both instances");

    // 一半请求从假死实例开始，靠重试落到健康实例
    int ok = 0;
    for (int i = 0; i < 8; ++i) {
        chen::RockRequest::ptr req(new chen::RockRequest);
        req->setSn(2000 + i);
        req->setCmd(CMD_ECHO);
        req->setBody("failover");
        auto result = lb.request("astartes", "rock", req, 900, i % 2);
        if (result && result->response && result->response->getResult() == 0) {
            ++ok;
        } else {
            ERROR(logger) << "failover request failed: " << (result ? result->toString() : "null");
        }
    }
    CHECK(ok == 8, "request survives blackhole instance via retry");

    lb.stop();
    blackhole->stop();
    {
        std::lock_guard lock(g_blackhole_mutex);
        for (auto& client : g_blackhole_clients) {
            client->close();
        }
        g_blackhole_clients.clear();
    }
}

static void run()
{
    chen::ModuleMgr::GetInstance()->add(chen::Module::ptr(new TestRockModule));

    chen::IOManager* iom = chen::IOManager::GetThis();
    chen::RockServer::ptr server(new chen::RockServer("rock", iom, iom, iom));

    auto addr = chen::Address::LookupAny("127.0.0.1:18080");
    if (!addr) {
        ERROR(logger) << "invalid address";
        ++g_failed;
        return;
    }

    bool bound = false;
    for (int i = 0; i < 50; ++i) {
        if (server->bind(addr)) {
            bound = true;
            break;
        }
        usleep(200 * 1000);
    }
    CHECK(bound, "bind rock server on 127.0.0.1:18080");
    if (!bound) {
        return;
    }
    server->start();

    test_static_service_discovery();
    test_rock_request(addr);
    test_rock_gzip(addr);
    test_rock_notify(addr);
    test_rock_timeout(addr);
    test_rock_bad_magic(addr);
    test_rock_close(addr);
    test_rock_failover(iom);

    server->stop();

    if (g_failed == 0) {
        INFO(logger) << "test_rock all passed";
    } else {
        ERROR(logger) << "test_rock failed count=" << g_failed;
    }
}

int main(int argc, char** argv)
{
    {
        chen::IOManager iom(4);
        iom.schedule(run);
    }
    return g_failed == 0 ? 0 : 1;
}
