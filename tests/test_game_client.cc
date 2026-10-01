#include "chen/socket/socket.h"
#include "chen/socket/address.h"
#include "chen/log/log.h"

#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using chen::Address;
using chen::Socket;

static chen::Logger::ptr logger = LOG_ROOT();

namespace {
int readExact(Socket::ptr sock, void* buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        int n = sock->recv(static_cast<char*>(buffer) + offset, length - offset);
        if (n <= 0) {
            return n;
        }
        offset += static_cast<size_t>(n);
    }
    return static_cast<int>(offset);
}
}

int main(int argc, char** argv) {
    const char* addr_str = (argc > 1) ? argv[1] : "127.0.0.1:9001";
    uint32_t magic = (argc > 2) ? static_cast<uint32_t>(strtoul(argv[2], nullptr, 0)) : 0xC0DECAFE;
    uint32_t cmdId = (argc > 3) ? static_cast<uint32_t>(strtoul(argv[3], nullptr, 0)) : 1;
    uint32_t seq = (argc > 4) ? static_cast<uint32_t>(strtoul(argv[4], nullptr, 0)) : 1;
    std::string payload = (argc > 5) ? argv[5] : std::string("hello-game");

    Address::ptr addr = Address::LookupAny(addr_str);
    if (!addr) {
        std::cerr << "invalid address: " << addr_str << std::endl;
        return 1;
    }

    Socket::ptr sock = Socket::CreateTCP(addr);
    if (!sock->connect(addr, 3000)) {
        std::cerr << "connect fail: " << *addr << std::endl;
        return 1;
    }

    std::vector<uint8_t> packet;
    packet.resize(16 + payload.size());
    uint32_t magic_n = htonl(magic);
    uint32_t cmd_n = htonl(cmdId);
    uint32_t seq_n = htonl(seq);
    uint32_t len_n = htonl(static_cast<uint32_t>(payload.size()));
    std::memcpy(packet.data(), &magic_n, 4);
    std::memcpy(packet.data() + 4, &cmd_n, 4);
    std::memcpy(packet.data() + 8, &seq_n, 4);
    std::memcpy(packet.data() + 12, &len_n, 4);
    if (!payload.empty()) {
        std::memcpy(packet.data() + 16, payload.data(), payload.size());
    }

    int wn = sock->send(packet.data(), packet.size());
    if (wn != static_cast<int>(packet.size())) {
        std::cerr << "send fail wn=" << wn << std::endl;
        return 1;
    }
    std::cout << "sent cmd=" << cmdId << " seq=" << seq << " body_len=" << payload.size() << std::endl;

    // 尝试读取响应（如果服务端有回包）。若服务端未实现回包，可忽略。
    uint8_t rspHeader[16];
    int rn = sock->recv(rspHeader, sizeof(rspHeader));
    if (rn == 0) {
        std::cout << "server closed after send (no response)" << std::endl;
        return 0;
    }
    if (rn < 0) {
        std::cerr << "read header failed rn=" << rn << std::endl;
        return 1;
    }
    if (rn < 16) {
        int remain = readExact(sock, rspHeader + rn, 16 - rn);
        if (remain <= 0) {
            std::cout << "no full response header; server likely does not echo" << std::endl;
            return 0;
        }
    }
    uint32_t rspMagic = ntohl(*reinterpret_cast<uint32_t*>(rspHeader));
    uint32_t rspCmd = ntohl(*reinterpret_cast<uint32_t*>(rspHeader + 4));
    uint32_t rspSeq = ntohl(*reinterpret_cast<uint32_t*>(rspHeader + 8));
    uint32_t rspLen = ntohl(*reinterpret_cast<uint32_t*>(rspHeader + 12));

    std::vector<uint8_t> rspBody(rspLen);
    if (rspLen > 0) {
        if (readExact(sock, rspBody.data(), rspLen) <= 0) {
            std::cerr << "read body failed" << std::endl;
            return 1;
        }
    }

    std::cout << "recv rsp magic=0x" << std::hex << rspMagic << std::dec
              << " cmd=" << rspCmd << " seq=" << rspSeq << " body_len=" << rspLen << std::endl;
    if (!rspBody.empty()) {
        std::cout << "body(hex): ";
        for (auto b : rspBody) {
            std::cout << std::hex << (int)b << " ";
        }
        std::cout << std::dec << std::endl;
        std::string s(reinterpret_cast<char*>(rspBody.data()), rspBody.size());
        std::cout << "body(str): " << s << std::endl;
    }

    sock->close();
    return 0;
}
