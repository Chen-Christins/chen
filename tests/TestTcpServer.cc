#include "src/Tcp/TcpServer.hpp"
#include "src/Socket/SocketStream.hpp"
#include "src/Log/Log.hpp"

static chen::Logger::ptr logger = LOG_ROOT();

class MyServer : public chen::TcpServer {
public:
    MyServer(int type)
        : type_(type)
    {
    }
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
            if (type_ == 1) {
                INFO(logger) << "\n" << ba->toString();
            } else {
                INFO(logger) << "\n" << ba->toHexString();
            }
        }
    }
private:
    int type_ = 0;
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