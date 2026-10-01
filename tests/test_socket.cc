#include "chen/socket/socket.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

void test_scoket() {
    chen::IPAddress::ptr addr = chen::Address::LookupAnyIPAddress("www.baidu.com");
    if (addr) {
        INFO(logger) << "get address: " << addr->toString();
    } else {
        ERROR(logger) << "get address fail";
        return ;
    }

    chen::Socket::ptr sock = chen::Socket::CreateTCP(addr);
    addr->setPort(80);
    if (!sock->connect(addr)) {
        ERROR(logger) << "connect " << addr->toString() << " fail";
    } else {
        INFO(logger) << "connect " << addr->toString() << " connected";
    }

    const char buff[] = "GET / HTTP/1.1\r\n\r\n";
    int rt = sock->send(buff, sizeof(buff));
    if (rt <= 0) {
        INFO(logger) << "send fail rt=" << rt;
        return ;
    }

    std::string buffs;
    buffs.resize(4096);
    rt = sock->recv(&buffs[0], buffs.size());

    if (rt <= 0) {
        INFO(logger) << "send fail rt=" << rt;
    }
    buffs.resize(rt);
    INFO(logger) << buffs;
}

int main(int argc, char** argv) {
    chen::IOManager iom;
    iom.schedule(&test_scoket);
    return 0;
}