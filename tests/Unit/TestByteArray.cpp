/**
 * @file TestByteArray.cpp
 * @brief ByteArray 序列化/反序列化单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "../TestHarness.hpp"

#include "src/ByteArray/ByteArray.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using chen::ByteArray;

CHEN_TEST(bytearray, fixed_int_roundtrip)
{
    ByteArray ba(16);

    ba.writeFint8(std::numeric_limits<int8_t>::min());
    ba.writeFuint8(std::numeric_limits<uint8_t>::max());
    ba.writeFint16(std::numeric_limits<int16_t>::min());
    ba.writeFuint16(std::numeric_limits<uint16_t>::max());
    ba.writeFint32(std::numeric_limits<int32_t>::min());
    ba.writeFuint32(std::numeric_limits<uint32_t>::max());
    ba.writeFint64(std::numeric_limits<int64_t>::min());
    ba.writeFuint64(std::numeric_limits<uint64_t>::max());

    const size_t expected_size = 1 + 1 + 2 + 2 + 4 + 4 + 8 + 8;
    CHEN_CHECK_EQ(ba.getSize(), expected_size);
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);

    ba.setPosition(0);
    CHEN_CHECK_EQ(ba.getReadSize(), expected_size);

    CHEN_CHECK_EQ(ba.readFint8(), std::numeric_limits<int8_t>::min());
    CHEN_CHECK_EQ(ba.readFuint8(), std::numeric_limits<uint8_t>::max());
    CHEN_CHECK_EQ(ba.readFint16(), std::numeric_limits<int16_t>::min());
    CHEN_CHECK_EQ(ba.readFuint16(), std::numeric_limits<uint16_t>::max());
    CHEN_CHECK_EQ(ba.readFint32(), std::numeric_limits<int32_t>::min());
    CHEN_CHECK_EQ(ba.readFuint32(), std::numeric_limits<uint32_t>::max());
    CHEN_CHECK_EQ(ba.readFint64(), std::numeric_limits<int64_t>::min());
    CHEN_CHECK_EQ(ba.readFuint64(), std::numeric_limits<uint64_t>::max());

    CHEN_CHECK_EQ(ba.getReadSize(), 0u);
}

CHEN_TEST(bytearray, varint_roundtrip)
{
    const std::vector<int64_t> values = {
        0,
        1,
        -1,
        127,
        -128,
        128,
        -129,
        300,
        -300,
        static_cast<int64_t>(std::numeric_limits<int32_t>::max()),
        static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
        std::numeric_limits<int64_t>::max(),
        std::numeric_limits<int64_t>::min(),
    };

    ByteArray ba(8);
    for (int64_t v : values) {
        ba.writeFint64(v);
    }

    ba.setPosition(0);
    for (int64_t v : values) {
        CHEN_CHECK_EQ(ba.readFint64(), v);
    }
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);

    const std::vector<uint64_t> unsigned_values = {
        0,
        1,
        127,
        128,
        300,
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()),
        std::numeric_limits<uint64_t>::max(),
    };

    ByteArray bau(8);
    for (uint64_t v : unsigned_values) {
        bau.writeFuint64(v);
    }
    bau.setPosition(0);
    for (uint64_t v : unsigned_values) {
        CHEN_CHECK_EQ(bau.readFuint64(), v);
    }
    CHEN_CHECK_EQ(bau.getReadSize(), 0u);
}

CHEN_TEST(bytearray, float_double_roundtrip)
{
    ByteArray ba(16);
    ba.writeFloat(3.1415926f);
    ba.writeDouble(2.718281828459045);

    ba.setPosition(0);
    CHEN_CHECK_EQ(ba.readFloat(), 3.1415926f);
    CHEN_CHECK_EQ(ba.readDouble(), 2.718281828459045);
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);
}

CHEN_TEST(bytearray, string_roundtrip)
{
    const std::vector<std::string> values = {
        "",
        "a",
        "hello world",
        "中文测试",
        std::string(60000, 'x'), ///< F16 长度上限内
    };

    ByteArray ba(64);
    for (const auto& v : values) {
        ba.writeStringF16(v);
    }

    ba.setPosition(0);
    for (const auto& v : values) {
        CHEN_CHECK_EQ(ba.readStringF16(), v);
    }
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);

    const std::string big(100000, 'y');
    ByteArray ba32(64);
    ba32.writeStringF32(big);
    ba32.setPosition(0);
    CHEN_CHECK_EQ(ba32.readStringF32(), big);

    ByteArray ba64(64);
    ba64.writeStringF64(big);
    ba64.setPosition(0);
    CHEN_CHECK_EQ(ba64.readStringF64(), big);

    ByteArray bav(64);
    for (const auto& v : values) {
        bav.writeStringVint(v);
    }
    bav.setPosition(0);
    for (const auto& v : values) {
        CHEN_CHECK_EQ(bav.readStringVint(), v);
    }
    CHEN_CHECK_EQ(bav.getReadSize(), 0u);
}

CHEN_TEST(bytearray, position_and_clear)
{
    ByteArray ba;
    ba.writeFint32(1);
    ba.writeFint32(2);

    CHEN_CHECK_EQ(ba.getSize(), 8u);
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);

    ba.setPosition(0);
    CHEN_CHECK_EQ(ba.getReadSize(), 8u);

    CHEN_CHECK_EQ(ba.readFint32(), 1);
    CHEN_CHECK_EQ(ba.getReadSize(), 4u);
    CHEN_CHECK_EQ(ba.readFint32(), 2);
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);

    ba.clear();
    CHEN_CHECK_EQ(ba.getSize(), 0u);
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);

    // clear 后可继续写入
    ba.writeFint8(7);
    ba.setPosition(0);
    CHEN_CHECK_EQ(ba.readFint8(), 7);
}

CHEN_TEST(bytearray, read_beyond_end_throws)
{
    ByteArray ba;
    ba.writeFint8(1);
    ba.setPosition(0);

    CHEN_CHECK_EQ(ba.readFint8(), 1);
    CHEN_CHECK_THROW(ba.readFint8(), std::out_of_range);
}

CHEN_TEST(bytearray, to_string_reflects_readable_range)
{
    ByteArray ba;
    ba.writeFint32(0x01020304);

    // toString() 返回 [position, size) 区间的原始字节；刚写完 position == size，可读区为空
    CHEN_CHECK(ba.toString().empty());

    ba.setPosition(0);
    // ByteArray 默认大端，0x01020304 落盘为 01 02 03 04
    CHEN_CHECK_EQ(ba.toString(), std::string("\x01\x02\x03\x04", 4));
    CHEN_CHECK(!ba.toHexString().empty());
}

CHEN_TEST(bytearray, read_string_length_beyond_readable_throws)
{
    // 报文里的长度字段不可信：声明 65535 字节但可读区为空时必须抛异常，
    // 而不是先 resize 出 64KB 再让后续 read 抛错。
    ByteArray f16;
    f16.writeFuint16(0xFFFF);
    f16.setPosition(0);
    CHEN_CHECK_THROW(f16.readStringF16(), std::out_of_range);

    ByteArray f32;
    f32.writeFuint32(0xFFFFFFFFu);
    f32.setPosition(0);
    CHEN_CHECK_THROW(f32.readStringF32(), std::out_of_range);

    ByteArray f64;
    f64.writeFuint64(0xFFFFFFFFFFFFFFFFull);
    f64.setPosition(0);
    CHEN_CHECK_THROW(f64.readStringF64(), std::out_of_range);

    ByteArray vint;
    vint.writeUint64(1ull << 40);
    vint.setPosition(0);
    CHEN_CHECK_THROW(vint.readStringVint(), std::out_of_range);
}

CHEN_TEST(bytearray, read_at_bad_position_throws)
{
    ByteArray ba;
    ba.writeFint32(0x01020304);

    char buf[8] = {0};
    // position 超过 size_：旧实现 size_ - position 下溢成极大值，越界读会通过检查
    CHEN_CHECK_THROW(ba.read(buf, sizeof(buf), 100), std::out_of_range);
    // position 合法但长度超出剩余数据
    CHEN_CHECK_THROW(ba.read(buf, 5, 0), std::out_of_range);
}

CHEN_TEST(bytearray, set_position_respects_capacity)
{
    ByteArray ba;
    ba.writeFint8(1);

    // SocketStream::read 依赖该契约：getWriteBuffers 预留容量后，允许定位到 size_ 之外
    // 并把 size_ 推进到新位置（否则 recv 到的数据永远读不到）
    ba.setPosition(2048);
    CHEN_CHECK_EQ(ba.getSize(), 2048u);
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);

    // 但绝不能超过已分配容量，否则 cur_ 链表遍历会走到链表之外
    CHEN_CHECK_THROW(ba.setPosition(4097), std::out_of_range);
}

CHEN_TEST_MAIN()
