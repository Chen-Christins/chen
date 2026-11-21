/**
 * @file ZlibStream.hpp
 * @brief zlib stream
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-05-26
 * @copyright GPL-3.0
 */
#pragma once

#include <zlib.h>

#include "../Socket/SocketStream.hpp"

namespace chen {

class ZlibStream : public Stream {
public:
    typedef std::shared_ptr<ZlibStream> ptr;

    enum Type {
        ZLIB,
        DEFLATE,
        GZIP
    };

    enum Strategy {
        DEFAULT = Z_DEFAULT_STRATEGY,
        FILTERED = Z_FILTERED,
        HUFFMAN = Z_HUFFMAN_ONLY,
        FIXED = Z_FIXED,
        RLE = Z_RLE
    };

    enum CompressLevel {
        NO_COMPRESSION = Z_NO_COMPRESSION,
        BEST_SPEED = Z_BEST_SPEED,
        BEST_COMPRESSION = Z_BEST_COMPRESSION,
        DEFAULT_COMPRESSION = Z_DEFAULT_COMPRESSION
    };

    static ZlibStream::ptr CreateGzip(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr CreateZlib(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr CreateDeflate(bool encode, uint32_t buff_size = 4096);
    static ZlibStream::ptr Create(
        bool encode,
        uint32_t buff_size = 4096,
        Type type = DEFLATE,
        int level = DEFAULT_COMPRESSION,
        int window_bits = 15,
        int memlevel = 8,
        Strategy strategy = DEFAULT);

    ZlibStream(bool encode, uint32_t buff_size = 4096);
    ~ZlibStream();

    virtual int read(void* buffer, size_t length) override;
    virtual int read(ByteArray::ptr ba, size_t length) override;
    virtual int write(const void* buffer, size_t length) override;
    virtual int write(ByteArray::ptr ba, size_t length) override;
    virtual void close() override;

    int flush();

    bool isFree() const { return free_; }
    void setFree(bool v) { free_ = v; }

    bool isEncode() const { return encode_; }
    void setEndcode(bool v) { encode_ = v; }

    std::vector<iovec>& getBuffers() { return buffs_; }
    std::string getResult() const;
    ByteArray::ptr getByteArray();

private:
    int init(Type type = DEFLATE, int level = DEFAULT_COMPRESSION, int window_bits = 15, int memlevel = 8, Strategy strategy = DEFAULT);

    int encode(const iovec* v, const uint64_t& size, bool finish);
    int decode(const iovec* v, const uint64_t& size, bool finish);

private:
    z_stream zstream_;
    uint32_t buff_size_;
    bool encode_;
    bool free_;
    std::vector<iovec> buffs_;
};

} // namespace chen
