#include "chen/tcp/tcp_server.h"
#include "chen/socket/socket_stream.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_ROOT();

class MyServer : public chen::TcpServer {
public:
    MyServer(int type) : m_type(type) {}
    void handleClient(chen::Socket::ptr client) override {
        INFO(logger) << "handleClient";
        chen::SocketStream::ptr tcp(new chen::SocketStream(client));
        chen::ByteArray::ptr ba(new chen::ByteArray);

        while (true) {
            INFO(logger) << "==== While ====";
            ba->clear();
            int rt = tcp->read(ba, 1024);
            INFO(logger) << "read rt=" << rt;

            if (rt == 0) {
                INFO(logger) << "client close:" << *client
                    << " errno=" << errno << " strerr=" << strerror(errno);
                break;
            }
            ba->setPosition(0);
            rt = tcp->write(ba, ba->getSize());
            ba->setPosition(0);
            INFO(logger) << "write rt=" << rt;
            if (rt == 0) {
                INFO(logger) << "client close:" << *client
                    << " errno=" << errno << " strerr=" << strerror(errno);
                break;
            }
            ba->setPosition(0);
            if (m_type == 1) {
                INFO(logger) << "\n" << ba->toString();
            } else {
                INFO(logger) << "\n" << ba->toHexString();
            }
        }
    }
private:
    int m_type = 0;
};

void run() {
    int type = 1;
    MyServer::ptr es(new MyServer(type));
    auto addr = chen::Address::LookupAny("0.0.0.0:8033");

    while (!es->bind(addr)) {
        sleep(2);
    }
    es->start();
}

int main(int argc, char** argv) {
    chen::IOManager iom;
    iom.schedule(run);
    return 0;
}