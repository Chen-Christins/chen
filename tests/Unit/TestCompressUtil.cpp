/**
 * @file TestCompressUtil.cpp
 * @brief CompressUtil / ZlibStream 解压健壮性单元测试
 * @author Christins
 * @date 2026-11-28
 * @copyright GPL-3.0
 * @details 覆盖两类曾经会导致进程不可用的问题：
 *          1. 输入被截断时 inflate 返回 Z_BUF_ERROR，旧循环条件永远不满足 → 死循环；
 *          2. 解压输出无上限 → 几 KB 的压缩数据可以膨胀出 GB 级内存（解压炸弹）。
 */
#include "../TestHarness.hpp"

#include "src/ByteArray/ByteArray.hpp"
#include "src/Streams/ZlibStream.hpp"
#include "src/Util/CompressUtil.hpp"

#include <cstddef>
#include <string>

using chen::ByteArray;
using chen::CompressUtil;
using chen::ZlibStream;

namespace {

std::string MakeRaw(size_t size)
{
    std::string s(size, '\0');
    for (size_t i = 0; i < size; ++i) {
        s[i] = static_cast<char>('a' + (i % 26));
    }
    return s;
}

} // namespace

CHEN_TEST(compressutil, gzip_roundtrip)
{
    const std::string raw = MakeRaw(64 * 1024);
    CHEN_CHECK_EQ(CompressUtil::GzipUncompress(CompressUtil::GzipCompress(raw)), raw);
}

CHEN_TEST(compressutil, zlib_roundtrip)
{
    const std::string raw = MakeRaw(64 * 1024);
    CHEN_CHECK_EQ(CompressUtil::ZlibUncompress(CompressUtil::ZlibCompress(raw)), raw);
}

CHEN_TEST(compressutil, truncated_input_returns_empty)
{
    const std::string gzip_data = CompressUtil::GzipCompress(MakeRaw(64 * 1024));
    CHEN_CHECK(!gzip_data.empty());
    // 旧实现会在这里无限空转，必须快速失败返回空串
    CHEN_CHECK(CompressUtil::GzipUncompress(gzip_data.substr(0, gzip_data.size() / 2)).empty());

    const std::string zlib_data = CompressUtil::ZlibCompress(MakeRaw(64 * 1024));
    CHEN_CHECK(!zlib_data.empty());
    CHEN_CHECK(CompressUtil::ZlibUncompress(zlib_data.substr(0, zlib_data.size() / 2)).empty());
}

CHEN_TEST(compressutil, output_limit_is_enforced)
{
    const std::string gzip_data = CompressUtil::GzipCompress(MakeRaw(64 * 1024));
    CHEN_CHECK(CompressUtil::GzipUncompress(gzip_data, 1024).empty());

    const std::string zlib_data = CompressUtil::ZlibCompress(MakeRaw(64 * 1024));
    CHEN_CHECK(CompressUtil::ZlibUncompress(zlib_data, 1024).empty());

    // 上限足够大时应正常解压
    CHEN_CHECK_EQ(CompressUtil::GzipUncompress(gzip_data, 64 * 1024).size(), 64 * 1024u);
}

CHEN_TEST(zlibstream, decode_output_limit_aborts_midway)
{
    const std::string raw = MakeRaw(64 * 1024);

    auto encoder = ZlibStream::CreateGzip(true, 4096);
    CHEN_CHECK(encoder != nullptr);
    CHEN_CHECK_EQ(encoder->write(raw.data(), raw.size()), Z_OK);
    CHEN_CHECK_EQ(encoder->flush(), Z_OK);

    ByteArray::ptr encoded = encoder->getByteArray();
    CHEN_CHECK(encoded->getSize() > 0);

    auto decoder = ZlibStream::CreateGzip(false, 4096);
    CHEN_CHECK(decoder != nullptr);
    decoder->setMaxOutput(1024);
    // 应在解压过程中就因为超过上限而失败，而不是先解出 64KB 再检查
    CHEN_CHECK_EQ(decoder->write(encoded, static_cast<size_t>(-1)), Z_DATA_ERROR);
    CHEN_CHECK(decoder->getOutputSize() >= 1024);
}

CHEN_TEST_MAIN()
