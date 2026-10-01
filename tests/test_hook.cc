#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include <arpa/inet.h>

static chen::Logger::ptr logger = LOG_ROOT();

void test_sleep() {
    chen::IOManager iom(1);
    iom.schedule([]() {
        sleep(2);
        INFO(logger) << "sleep 2";
    });

    iom.schedule([]() {
        sleep(3);
        INFO(logger) << "sleep 3";
    });
    INFO(logger) << "test_sleep";
}

void test_sock() {
    // YAML::Node root = YAML::LoadFile("/home/chen/workspace/chen/bin/conf/hook.yml");
    // chen::Config::LoadFromYaml(root);

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "36.155.132.3", &addr.sin_addr.s_addr);

    INFO(logger) << "begin connect";
    int rt = connect(sock, (const sockaddr*)&addr, sizeof(addr));
    INFO(logger) << "connect rt=" << rt << " errno=" << errno;

    if (rt) {
        return ;
    }

    const char data[] = "GET / HTTP/1.0\r\n\r\n";
    rt = send(sock, data, sizeof(data), 0);
    INFO(logger) << "send rt=" << rt << " errno=" << errno;

    if (rt <= 0) {
        return ;
    }

    std::string buff;
    buff.resize(4096);

    rt = recv(sock, &buff[0], buff.size(), 0);
    INFO(logger) << "recv rt=" << rt << " errno=" << errno;

    if (rt <= 0) {
        return ;
    }

    buff.resize(rt);
    INFO(logger) << buff;
}

int main(int argc, char** argv) {
    // test_sleep();
    chen::IOManager iom;
    iom.schedule(test_sock);
    return 0;
}