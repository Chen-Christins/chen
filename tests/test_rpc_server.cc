#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/rpc/rpc_server.h"

static chen::Logger::ptr logger = LOG_ROOT();

int add(int a, int b) {
    INFO(logger) << "call add(" << a << ", " << b << ")";
    return a + b;
}

std::string echo(std::string str) {
    INFO(logger) << "call echo(" << str << ")";
    return "echo: " + str;
}

std::vector<std::string> echoVec(const std::vector<std::string>& vec) {
    INFO(logger) << "call echoVec(...) size=" << vec.size();
    std::vector<std::string> res;
    for (auto& s : vec) {
        res.push_back("echo: " + s);
    }
    return res;
}

void echoTest() {
    INFO(logger) << "call echoTest() void version";
}

struct UserInfo {
    int id;
    std::string name;
    int age;
    std::vector<std::string> tags;
};

namespace chen::rpc {

template<>
struct Serialization<UserInfo> {
    static void write(ByteArray::ptr ba, const UserInfo& u) {
        ba->writeFint32(u.id);
        ba->writeStringVint(u.name);
        ba->writeFint32(u.age);
        ba->writeUint32(u.tags.size());
        for(auto& t : u.tags) {
            ba->writeStringVint(t);
        }
    }
    static void read(ByteArray::ptr ba, UserInfo& u) {
        u.id = ba->readFint32();
        u.name = ba->readStringVint();
        u.age = ba->readFint32();
        uint32_t size = ba->readUint32();
        u.tags.clear();
        for(uint32_t i=0; i<size; ++i) {
            u.tags.push_back(ba->readStringVint());
        }
    }
};

} // namespace chen::rpc

UserInfo getUserInfo(int id) {
    INFO(logger) << "call getUserInfo(" << id << ")";
    UserInfo u;
    u.id = id;
    u.name = "User" + std::to_string(id);
    u.age = 18 + (id % 20);
    u.tags = {"tag1", "tag2"};
    return u;
}

void run() {
    chen::Address::ptr addr = chen::Address::LookupAny("0.0.0.0:8080");
    if (!addr) {
        ERROR(logger) << "get address error";
        return;
    }

    chen::rpc::RpcServer::ptr server(new chen::rpc::RpcServer);
    while (!server->bind(addr)) {
        ERROR(logger) << "bind " << *addr << " fail";
        sleep(1);
    }

    server->registerMethod("add", add);
    server->registerMethod("echo", echo);
    server->registerMethod("echoVec", echoVec);
    server->registerMethod("getUserInfo", getUserInfo);
    server->registerMethod("echoTest", echoTest);

    INFO(logger) << "RpcServer start on " << *addr;
    server->start();
}

int main(int argc, char** argv) {
    chen::IOManager iom(1);
    iom.schedule(run);
    return 0;
}