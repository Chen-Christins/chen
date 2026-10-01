#include "chen/socket/address.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

void test() {
    std::vector<chen::Address::ptr> addrs;

    INFO(logger) << "begin";
    bool rt = chen::Address::Lookup(addrs, "www.baidu.com", AF_INET);
    // bool rt = chen::Address::Lookup(addrs, "localhost");
    if (!rt) {
        ERROR(logger) << "lookup fail";
        return ;
    }
    INFO(logger) << "end";
    for (size_t i = 0; i < addrs.size(); ++i) {
        INFO(logger) << i << " - " << addrs[i]->toString();
    }

    auto addr = chen::Address::LookupAny("localhost:4080");
    if (addr) {
        INFO(logger) << *addr;
    } else {
        ERROR(logger) << "Error";
    }
}

void test_iface() {
    std::multimap<std::string, std::pair<chen::Address::ptr, uint32_t>> results;

    bool v = chen::Address::GetInterfaceAddresses(results);
    if (!v) {
        ERROR(logger) << "GetInterfaceAddresses fail";
    }
    for (auto& [x, y] : results) {
        INFO(logger) << x << " - " << y.first->toString() << " - " << y.second;
    }
}

void test_ipv4() {
    auto addr = chen::IPv4Address::Create("192.168.88.111", 80);
    auto saddr = addr->subnetMask(24);
    auto baddr = addr->broadcastAddress(24);
    auto naddr = addr->networkAddress(24);
    if (addr) {
        INFO(logger) << addr->toString();
    }
    if (saddr) {
        INFO(logger) << saddr->toString();
    }
    if (baddr) {
        INFO(logger) << baddr->toString();
    }
    if (naddr) {
        INFO(logger) << naddr->toString();
    }
}

void test_ipv6() {
    auto addr = chen::IPv6Address::Create("fe80:0000:0001:0000:0440:44ff:1233:5678", 80);
    auto saddr = addr->subnetMask(64);
    auto baddr = addr->broadcastAddress(64);
    auto naddr = addr->networkAddress(64);
    if (addr) {
        INFO(logger) << addr->toString();
    }
    if (saddr) {
        INFO(logger) << saddr->toString();
    }
    if (baddr) {
        INFO(logger) << baddr->toString();
    }
    if (naddr) {
        INFO(logger) << naddr->toString();
    }
}

int main(int argc, char** argv) {
    test();
    // test_iface();
    // test_ipv4();
    test_ipv6();
    return 0;
}