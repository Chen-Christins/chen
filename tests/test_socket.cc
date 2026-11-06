#include "chen/socket/socket.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"

#include <thread>

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

/**
 * @brief 没有 IOManager 的线程里调用 cancel* 不应空指针崩溃
 */
void test_cancel_without_iom()
{
    INFO(logger) << "=== test_cancel_without_iom ===";
    std::thread thr([]() {
        chen::IPAddress::ptr addr = chen::Address::LookupAnyIPAddress("127.0.0.1:0");
        if (!addr) {
            ERROR(logger) << "get address fail";
            return;
        }
        chen::Socket::ptr sock = chen::Socket::CreateTCP(addr);
        if (!sock->bind(addr)) {
            WARN(logger) << "bind fail, fd may be invalid";
        }
        INFO(logger) << "plain thread socket isValid=" << sock->isValid();
        // 普通线程没有 IOManager，cancel* 必须安全返回 false
        sock->cancelAll();
        sock->cancelRead();
        sock->cancelWrite();
        sock->cancelAccept();
    });
    thr.join();
    INFO(logger) << "test_cancel_without_iom ok";
}

/**
 * @brief IOManager 接口必须拒绝负 fd（回归 m_fdContexts[-1] 越界）
 */
void test_negative_fd()
{
    INFO(logger) << "=== test_negative_fd ===";
    chen::IOManager* iom = chen::IOManager::GetThis();
    if (!iom) {
        ERROR(logger) << "no IOManager";
        return;
    }
    if (iom->addEvent(-1, chen::IOManager::READ) != -1) {
        ERROR(logger) << "addEvent(-1) should return -1";
    }
    if (iom->delEvent(-1, chen::IOManager::READ)) {
        ERROR(logger) << "delEvent(-1) should return false";
    }
    if (iom->cancelEvent(-1, chen::IOManager::READ)) {
        ERROR(logger) << "cancelEvent(-1) should return false";
    }
    if (iom->cancelAll(-1)) {
        ERROR(logger) << "cancelAll(-1) should return false";
    }
    INFO(logger) << "test_negative_fd ok";
}

/**
 * @brief 已关闭的 socket（fd = -1）上调用 cancel* 不应越界崩溃
 * @details 复现路径：connect 失败 → Socket::close() 把 fd 置 -1 → 析构里 cancelAll()
 */
void test_cancel_closed_socket()
{
    INFO(logger) << "=== test_cancel_closed_socket ===";
    chen::IPAddress::ptr addr = chen::Address::LookupAnyIPAddress("127.0.0.1:1");
    if (!addr) {
        ERROR(logger) << "get address fail";
        return;
    }
    chen::Socket::ptr sock = chen::Socket::CreateTCP(addr);
    // 端口 1 没人监听，connect 失败后 fd 会被 close 成 -1
    sock->connect(addr);
    INFO(logger) << "closed socket isValid=" << sock->isValid();
    if (sock->cancelAll() || sock->cancelRead() || sock->cancelWrite() || sock->cancelAccept()) {
        ERROR(logger) << "cancel* on closed socket should return false";
    }
    INFO(logger) << "test_cancel_closed_socket ok";
}

int main(int argc, char** argv) {
    logger->setLevel(chen::LogLevel::TRACE);
    // 必须在创建 IOManager 之前：普通线程没有调度器
    test_cancel_without_iom();

    chen::IOManager iom;
    iom.schedule(&test_scoket);
    iom.schedule(&test_negative_fd);
    iom.schedule(&test_cancel_closed_socket);
    return 0;
}