#include "chen/iomanager/iomanager.h"
#include "chen/timer/timer.h"
#include "chen/log/log.h"
#include <arpa/inet.h>
#include <fcntl.h>

static chen::Logger::ptr logger = LOG_ROOT();
static int count = 0;

chen::Timer::ptr s_timer;

void test_timer() {
    chen::IOManager iom(2);
    s_timer = iom.addTimer(1000, []() {
        ++count;
        INFO(logger) << "hello count=" << count;
    }, true);
}

int sock = 0;

void test_fiber() {
    INFO(logger) << "test_fiber sock=" << sock;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "36.155.132.3", &addr.sin_addr.s_addr);
    
    if (!connect(sock, (const sockaddr*)&addr, sizeof(addr))) {
    } else if (errno == EINPROGRESS) {
        INFO(logger) << "add event errno=" << errno << " " << strerror(errno);

        chen::IOManager::GetThis()->addEvent(sock, chen::IOManager::READ, [sock]() {
            INFO(logger) << "read callback";
            char temp[1000];
            int rt = read(sock, temp, 1000);
            if (rt >= 0) {
                std::string ans(temp, rt);
                INFO(logger) << "read:[" << ans << "]";
            } else {
                INFO(logger) << "read rt = " << rt;
            }
        });

        chen::IOManager::GetThis()->addEvent(sock, chen::IOManager::WRITE, [sock]() {
            INFO(logger) << "write callback";
            int rt = write(sock, "GET / HTTP/1.1\r\ncontent-length: 0\r\n\r\n", 38);
            INFO(logger) << "write it = " << rt;
        });
    } else {
        INFO(logger) << "else " << errno << " " << strerror(errno);
    }
}

void test1() {
    chen::IOManager iom(4, true, "test1");
    iom.schedule(&test_fiber);
}

int main(int argc, char** argv) {
    test1();
    // test_timer();
    return 0;
}