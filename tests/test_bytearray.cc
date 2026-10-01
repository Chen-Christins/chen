#include "chen/bytearray/bytearray.h"
#include "chen/log/log.h"
#include "chen/util/macro.h"

static chen::Logger::ptr logger = LOG_ROOT();

void test() {
#define XX(type, len, write_fun, read_fun, base_len) {\
    std::vector<type> vec; \
    for(int i = 0; i < len; ++i) { \
        vec.push_back(rand()); \
    } \
    chen::ByteArray::ptr ba(new chen::ByteArray(base_len)); \
    for(auto& i : vec) { \
        ba->write_fun(i); \
    } \
    ba->setPosition(0); \
    for(size_t i = 0; i < vec.size(); ++i) { \
        type v = ba->read_fun(); \
        ASSERT(v == vec[i]); \
    } \
    ASSERT(ba->getReadSize() == 0); \
    INFO(logger) << #write_fun "/" #read_fun \
                    " (" #type " ) len=" << len \
                    << " base_len=" << base_len \
                    << " size=" << ba->getSize(); \
}

    XX(int8_t,  100, writeFint8, readFint8, 1);
#undef XX
}

int main(int argc, char** argv) {
    test();
    return 0;
}