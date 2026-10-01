#include "compress_util.h"

#include <cstring>

#include <zlib.h>

namespace chen {

static const size_t kChunkSize = 16384;

std::string CompressUtil::GzipCompress(const std::string& data) {
    if (data.empty()) { return ""; }

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

std::string CompressUtil::GzipUncompress(const std::string& data) {
    if (data.empty()) { return ""; }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, MAX_WBITS + 16) != Z_OK) {
        return "";
    }

    std::string result;
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    strm.avail_in = static_cast<uInt>(data.size());

    char out[kChunkSize];
    int ret;
    do {
        strm.next_out = reinterpret_cast<Bytef*>(out);
        strm.avail_out = sizeof(out);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            return "";
        }
        result.append(out, sizeof(out) - strm.avail_out);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return result;
}

std::string CompressUtil::ZlibCompress(const std::string& data) {
    if (data.empty()) { return ""; }

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

std::string CompressUtil::ZlibUncompress(const std::string& data) {
    if (data.empty()) { return ""; }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit(&strm) != Z_OK) {
        return "";
    }

    std::string result;
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    strm.avail_in = static_cast<uInt>(data.size());

    char out[kChunkSize];
    int ret;
    do {
        strm.next_out = reinterpret_cast<Bytef*>(out);
        strm.avail_out = sizeof(out);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            return "";
        }
        result.append(out, sizeof(out) - strm.avail_out);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return result;
}

} // namespace chen
