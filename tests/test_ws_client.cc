#include "chen/http/ws_connection.h"
#include "chen/iomanager/iomanager.h"
#include "chen/util/random_util.h"
#include <iostream>

void run() {
    auto rt = chen::http::WSConnection::Create("http://127.0.0.1:8020/chen", 1000);
    if (!rt.second) {
        std::cout << rt.first->toString() << std::endl;
        return;
    }
    
    auto conn = rt.second;
    while (true) {
        for (int i = 0; i < 1; ++i) {
            conn->sendMessage(chen::RandomUtil::RandString(60), chen::http::WSFrameHead::TEXT_FRAME, false);
        }
        conn->sendMessage(chen::RandomUtil::RandString(65), chen::http::WSFrameHead::TEXT_FRAME, true);
        auto msg = conn->recvMessage();
        if (!msg) {
            break;
        }
        std::cout << "opcode=" << msg->getOpcode()
            << " data=" << msg->getData() << std::endl;
        sleep(10);
    }
}

int main(int argc, char** argv) {
    chen::IOManager iom(1);
    srand(time(0));

    iom.schedule(run);

    return 0;
}