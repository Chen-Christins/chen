#include "CompressUtil.hpp"

#include <climits>
#include <cstring>

#include <zlib.h>

namespace chen {

static const size_t kChunkSize = 16384;

namespace {

/// 解压一个完整的 zlib/gzip 流。
/// 注意两个陷阱：
/// 1. 输入被截断时 zlib 会返回 Z_BUF_ERROR；它既不在错误集合里也不等于 Z_STREAM_END，
///    按旧的 `while (ret != Z_STREAM_END)` 写法会永远空转（占满一个调度线程）。
/// 2. 输出必须限量，否则几 KB 的压缩数据可以膨胀出 GB 级内存。
bool InflateStream(z_stream& strm, std::string& result, size_t max_output)
{
    char out[kChunkSize];
    for (;;) {
        strm.next_out = reinterpret_cast<Bytef*>(out);
        strm.avail_out = sizeof(out);
        uInt before_in = strm.avail_in;
        int ret = inflate(&strm, Z_NO_FLUSH);
        size_t produced = sizeof(out) - strm.avail_out;
        if (ret == Z_STREAM_END) {
            if (result.size() + produced > max_output) {
                return false;
            }
            result.append(out, produced);
            return true;
        }
        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            return false;
        }
        if (produced == 0 && strm.avail_in == before_in) {
            // 没有任何进展：输入已耗尽但流没有结束（被截断），再循环只会空转
            return false;
        }
        if (result.size() + produced > max_output) {
            return false;
        }
        result.append(out, produced);
    }
}

} // namespace

std::string CompressUtil::GzipCompress(const std::string& data)
{
    if (data.empty()) {
        return "";
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return "";
    }

    std::string result;
    result.reserve(data.size());

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    strm.avail_in = static_cast<uInt>(data.size());

    char out[kChunkSize];
    int ret;
    do {
        strm.next_out = reinterpret_cast<Bytef*>(out);
        strm.avail_out = sizeof(out);
        ret = deflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&strm);
            return "";
        }
        result.append(out, sizeof(out) - strm.avail_out);
    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    return result;
}

std::string CompressUtil::GzipUncompress(const std::string& data)
{
    return GzipUncompress(data, kDefaultMaxUncompressSize);
}

std::string CompressUtil::GzipUncompress(const std::string& data, size_t max_output)
{
    if (data.empty()) {
        return "";
    }
    if (data.size() > static_cast<size_t>(UINT_MAX)) {
        return "";
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, MAX_WBITS + 16) != Z_OK) {
        return "";
    }

    std::string result;
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    strm.avail_in = static_cast<uInt>(data.size());

    bool ok = InflateStream(strm, result, max_output);
    inflateEnd(&strm);
    return ok ? result : "";
}

std::string CompressUtil::ZlibCompress(const std::string& data)
{
    if (data.empty()) {
        return "";
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
        return "";
    }

    std::string result;
    result.reserve(data.size());

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    strm.avail_in = static_cast<uInt>(data.size());

    char out[kChunkSize];
    int ret;
    do {
        strm.next_out = reinterpret_cast<Bytef*>(out);
        strm.avail_out = sizeof(out);
        ret = deflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&strm);
            return "";
        }
        result.append(out, sizeof(out) - strm.avail_out);
    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    return result;
}

std::string CompressUtil::ZlibUncompress(const std::string& data)
{
    return ZlibUncompress(data, kDefaultMaxUncompressSize);
}

std::string CompressUtil::ZlibUncompress(const std::string& data, size_t max_output)
{
    if (data.empty()) {
        return "";
    }
    if (data.size() > static_cast<size_t>(UINT_MAX)) {
        return "";
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit(&strm) != Z_OK) {
        return "";
    }

    std::string result;
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    strm.avail_in = static_cast<uInt>(data.size());

    bool ok = InflateStream(strm, result, max_output);
    inflateEnd(&strm);
    return ok ? result : "";
}

} // namespace chen
