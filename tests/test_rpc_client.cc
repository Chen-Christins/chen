#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/rpc/rpc_client.h"
#include "chen/rpc/rpc_client_pool.h"

static chen::Logger::ptr logger = LOG_ROOT();

struct UserInfo {
    int id;
    std::string name;
    int age;
    std::vector<std::string> tags;
};

template <>
struct chen::rpc::Serialization<UserInfo> {
    static void write(ByteArray::ptr ba, const UserInfo& u) {
        ba->writeFint32(u.id);
        ba->writeStringVint(u.name);
        ba->writeFint32(u.age);
        ba->writeUint32(u.tags.size());
        for (auto& t : u.tags) {
            ba->writeStringVint(t);
        }
    }
    static void read(ByteArray::ptr ba, UserInfo& u) {
        u.id = ba->readFint32();
        u.name = ba->readStringVint();
        u.age = ba->readFint32();
        uint32_t size = ba->readUint32();
        u.tags.clear();
        for (uint32_t i = 0; i < size; ++i) {
            u.tags.push_back(ba->readStringVint());
        }
    }
};

void run() {
    chen::Address::ptr addr = chen::Address::LookupAny("127.0.0.1:8080");
    if (!addr) {
        ERROR(logger) << "get address error";
        return;
    }

    chen::rpc::RpcClient::ptr client(new chen::rpc::RpcClient(false));
    // client->setHeartbeatInterval(1000); // 1s heartbeat

    if (!client->connect(addr)) {
        ERROR(logger) << "connect " << *addr << " fail";
        return;
    }

    try {
        int result = client->call<int>("add", 40, 20);
        INFO(logger) << "add(40, 20) = " << result;

        std::string str = client->call<std::string>("echo", "hello rpc test");
        INFO(logger) << "echo(hello rpc test) = " << str;

        std::vector<std::string> vec = {"one", "two", "three"};
        std::vector<std::string> echoed_vec = client->call<std::vector<std::string>>("echoVec", vec);
        INFO(logger) << "echoVec(...) returned vector of size " << echoed_vec.size();
        for (const auto& s : echoed_vec) {
            INFO(logger) << "  " << s;
        }

        UserInfo u = client->call<UserInfo>("getUserInfo", 100);
        INFO(logger) << "getUserInfo(100) id=" << u.id << " name=" << u.name << " age=" << u.age;
        for(auto& t : u.tags) {
            INFO(logger) << "  tag: " << t;
        }

        // Void return type test
        client->call<void>("echoTest");
        INFO(logger) << "echoTest() void version called successfully";

        // Timeout test: call non-existent method with short timeout
        try {
            std::string nope = client->callWithTimeout<std::string>(2000, "no_such_method", "payload");
            INFO(logger) << "no_such_method returned: " << nope;
        } catch (std::exception& te) {
            INFO(logger) << "timeout/non-existent method handled: " << te.what();
        }
    } catch (std::exception& e) {
        ERROR(logger) << "call error: " << e.what();
    }

    // ── 异步调用（回调式）：发起后立即返回，完成时回调被调度为独立协程 ──
    {
        chen::FiberSemaphore done{0};
        std::atomic<int> ok_count{0};

        client->callAsync<int>("add", [&](chen::rpc::RpcResult<int> r) {
            if (r.ok() && r.value == 60) {
                ++ok_count;
                INFO(logger) << "[async] add(40, 20) = " << r.value;
            } else {
                ERROR(logger) << "[async] add failed: status="
                    << static_cast<int>(r.status) << " code=" << r.code
                    << " err=" << r.error;
            }
            done.notify();
        }, 40, 20);

        client->callAsync<std::string>("echo", [&](chen::rpc::RpcResult<std::string> r) {
            if (r.ok() && r.value == "echo: hello async") {
                ++ok_count;
                INFO(logger) << "[async] echo = " << r.value;
            } else {
                ERROR(logger) << "[async] echo failed: status="
                    << static_cast<int>(r.status) << " code=" << r.code
                    << " err=" << r.error << " value=" << r.value;
            }
            done.notify();
        }, "hello async");

        client->callAsync<void>("echoTest", [&](chen::rpc::RpcResult<void> r) {
            if (r.ok()) {
                ++ok_count;
                INFO(logger) << "[async] echoTest void called";
            } else {
                ERROR(logger) << "[async] echoTest failed: " << r.error;
            }
            done.notify();
        });

        // 服务端不存在的错误路径：回调应收到业务错误码而非抛异常
        client->callAsyncWithTimeout<std::string>(2000, "no_such_method"
                , [&](chen::rpc::RpcResult<std::string> r) {
            if (!r.ok() && r.code != 0) {
                ++ok_count;
                INFO(logger) << "[async] no_such_method error propagated, code="
                    << r.code << " err=" << r.error;
            } else {
                ERROR(logger) << "[async] no_such_method unexpected ok=" << r.ok();
            }
            done.notify();
        }, "payload");

        for (int i = 0; i < 4; ++i) {
            done.wait();
        }
        if (ok_count.load() == 4) {
            INFO(logger) << "[async] PASS, completed " << ok_count << "/4";
        } else {
            ERROR(logger) << "[async] FAIL, completed " << ok_count << "/4";
        }
    }

    // ── Future 调用：并发发起，之后在协程内按顺序等待结果 ──
    {
        std::atomic<int> ok_count{0};

        auto f_add = client->callFuture<int>("add", 40, 20);
        auto f_echo = client->callFuture<std::string>("echo", "hi future");
        auto f_void = client->callFuture<void>("echoTest");
        auto f_err = client->callFutureWithTimeout<std::string>(2000, "no_such_method", "payload");

        // getValue：成功返回 value，失败抛异常
        try {
            int sum = f_add.getValue();
            if (sum == 60) {
                ++ok_count;
            }
            INFO(logger) << "[future] add(40, 20) = " << sum;
        } catch (std::exception& e) {
            ERROR(logger) << "[future] add failed: " << e.what();
        }

        // get：返回 RpcResult，不抛异常
        auto r_echo = f_echo.get();
        if (r_echo.ok() && r_echo.value == "echo: hi future") {
            ++ok_count;
        }
        INFO(logger) << "[future] echo = " << r_echo.value;

        auto r_void = f_void.get();
        if (r_void.ok()) {
            ++ok_count;
        }
        INFO(logger) << "[future] echoTest void ok=" << r_void.ok();

        auto r_err = f_err.get();
        if (!r_err.ok() && r_err.code == 404) {
            ++ok_count;
        }
        INFO(logger) << "[future] no_such_method code=" << r_err.code
            << " err=" << r_err.error;

        // getValue 失败应抛异常
        bool threw = false;
        try {
            f_err.getValue();
        } catch (std::exception& e) {
            threw = true;
            INFO(logger) << "[future] getValue throws: " << e.what();
        }
        if (threw) {
            ++ok_count;
        }

        if (ok_count.load() == 5) {
            INFO(logger) << "[future] PASS, completed " << ok_count << "/5";
        } else {
            ERROR(logger) << "[future] FAIL, completed " << ok_count << "/5";
        }
    }

    // RpcClientPool test
    auto pool = std::make_shared<chen::rpc::RpcClientPool>();
    pool->setHeartbeatIntervalMs(1000);
    pool->setIdleTimeoutMs(5000);

    auto pool_client = pool->getClient("127.0.0.1:8080");
    if (!pool_client) {
        ERROR(logger) << "[pool] getClient failed";
    } else {
        try {
            int result = pool_client->call<int>("add", 40, 20);
            INFO(logger) << "[pool] add(40, 20) = " << result;

            std::string str = pool_client->call<std::string>("echo", "hello rpc test");
            INFO(logger) << "[pool] echo(hello rpc test) = " << str;

            std::vector<std::string> vec = {"one", "two", "three"};
            auto echoed_vec = pool_client->call<std::vector<std::string>>("echoVec", vec);
            INFO(logger) << "[pool] echoVec(...) returned vector of size " << echoed_vec.size();

            UserInfo u = pool_client->call<UserInfo>("getUserInfo", 100);
            INFO(logger) << "[pool] getUserInfo(100) id=" << u.id
                << " name=" << u.name << " age=" << u.age;

            pool_client->call<void>("echoTest");
            INFO(logger) << "[pool] echoTest() called successfully";

            auto pool_client2 = pool->getClient("127.0.0.1:8080");
            INFO(logger) << "[pool] reuse client connected=" << pool_client2->isConnected();
        } catch (std::exception& e) {
            ERROR(logger) << "[pool] call error: " << e.what();
        }
    }

    chen::IOManager::GetThis()->addTimer(10 * 1000, [client, pool](){
        INFO(logger) << "Closing clients...";
        client->close();
        pool->closeAll();
    }, false);
}

int main(int argc, char** argv) {
    chen::IOManager iom(1);
    iom.schedule(run);
    return 0;
}
