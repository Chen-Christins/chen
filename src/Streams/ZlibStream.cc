#include "ZlibStream.hpp"

namespace chen {

ZlibStream::ptr ZlibStream::CreateGzip(bool encode, uint32_t buff_size)
{
    return Create(encode, buff_size, GZIP);
}

ZlibStream::ptr ZlibStream::CreateZlib(bool encode, uint32_t buff_size)
{
    return Create(encode, buff_size, ZLIB);
}

ZlibStream::ptr ZlibStream::CreateDeflate(bool encode, uint32_t buff_size)
{
    return Create(encode, buff_size, DEFLATE);
}

ZlibStream::ptr ZlibStream::Create(bool encode, uint32_t buff_size, Type type, int level, int window_bits, int memlevel, Strategy strategy)
{
    ZlibStream::ptr rt(new ZlibStream(encode, buff_size));
    if (rt->init(type, level, window_bits, memlevel, strategy) == Z_OK) {
        return rt;
    }
    return nullptr;
}

ZlibStream::ZlibStream(bool encode, uint32_t buff_size)
    : buff_size_(buff_size)
    , encode_(encode)
    , free_(true)
{
}

ZlibStream::~ZlibStream()
{
    if (free_) {
        for (auto& i : buffs_) {
            free(i.iov_base);
        }
    }

    if (encode_) {
        deflateEnd(&zstream_);
    } else {
        inflateEnd(&zstream_);
    }
}

int ZlibStream::read(void* buffer, size_t length)
{
    throw std::logic_error("ZlibStream::read is invalid");
}

int ZlibStream::read(ByteArray::ptr ba, size_t length)
{
    throw std::logic_error("ZlibStream::read is invalid");
}

int ZlibStream::write(const void* buffer, size_t length)
{
    iovec ivc;
    ivc.iov_base = (void*)buffer;
    ivc.iov_len = length;
    if (encode_) {
        return encode(&ivc, 1, false);
    } else {
        return decode(&ivc, 1, false);
    }
}

int ZlibStream::write(ByteArray::ptr ba, size_t length)
{
    std::vector<iovec> buffers;
    ba->getReadBuffers(buffers, length);
    if (encode_) {
        return encode(&buffers[0], buffers.size(), false);
    } else {
        return decode(&buffers[0], buffers.size(), false);
    }
}

void ZlibStream::close()
{
    flush();
}

int ZlibStream::init(Type type, int level, int window_bits, int memlevel, Strategy strategy)
{
    ASSERT((level >= 0 && level <= 9) || level == DEFAULT_COMPRESSION);
    ASSERT((window_bits >= 8 && window_bits <= 15));
    ASSERT((memlevel >= 1 && memlevel <= 9));

    memset(&zstream_, 0, sizeof(zstream_));

    zstream_.zalloc = Z_NULL;
    zstream_.zfree = Z_NULL;
    zstream_.opaque = Z_NULL;

    switch (type) {
    case DEFLATE:
        window_bits = -window_bits;
        break;
    case GZIP:
        window_bits += 16;
        break;
    case ZLIB:
    default:
        break;
    }

    if (encode_) {
        return deflateInit2(&zstream_, level, Z_DEFLATED, window_bits, memlevel, (int)strategy);
    } else {
        return inflateInit2(&zstream_, window_bits);
    }
}

int ZlibStream::encode(const iovec* v, const uint64_t& size, bool finish)
{
    int ret = 0;
    int flush = 0;
    for (uint64_t i = 0; i < size; ++i) {
        zstream_.avail_in = v[i].iov_len;
        zstream_.next_in = (Bytef*)v[i].iov_base;

        flush = finish ? (i == size - 1 ? Z_FINISH : Z_NO_FLUSH) : Z_NO_FLUSH;

        iovec* ivc = nullptr;
        do {
            if (!buffs_.empty() && buffs_.back().iov_len != buff_size_) {
                ivc = &buffs_.back();
            } else {
                iovec vc;
                vc.iov_base = malloc(buff_size_);
                vc.iov_len = 0;
                buffs_.push_back(vc);
                ivc = &buffs_.back();
            }

            zstream_.avail_out = buff_size_ - ivc->iov_len;
            zstream_.next_out = (Bytef*)ivc->iov_base + ivc->iov_len;

            ret = deflate(&zstream_, flush);
            if (ret == Z_STREAM_ERROR) {
                return ret;
            }
            ivc->iov_len = buff_size_ - zstream_.avail_out;
        } while (zstream_.avail_out == 0);
    }
    if (flush == Z_FINISH) {
        deflateEnd(&zstream_);
    }
    return Z_OK;
}

int ZlibStream::decode(const iovec* v, const uint64_t& size, bool finish)
{
    int ret = 0;
    int flush = 0;
    for (uint64_t i = 0; i < size; ++i) {
        zstream_.avail_in = v[i].iov_len;
        zstream_.next_in = (Bytef*)v[i].iov_base;

        flush = finish ? (i == size - 1 ? Z_FINISH : Z_NO_FLUSH) : Z_NO_FLUSH;

        iovec* ivc = nullptr;
        do {
            if (!buffs_.empty() && buffs_.back().iov_len != buff_size_) {
                ivc = &buffs_.back();
            } else {
                iovec vc;
                vc.iov_base = malloc(buff_size_);
                vc.iov_len = 0;
                buffs_.push_back(vc);
                ivc = &buffs_.back();
            }

            zstream_.avail_out = buff_size_ - ivc->iov_len;
            zstream_.next_out = (Bytef*)ivc->iov_base + ivc->iov_len;

            ret = inflate(&zstream_, flush);
            if (ret == Z_STREAM_ERROR) {
                return ret;
            }
            ivc->iov_len = buff_size_ - zstream_.avail_out;
        } while (zstream_.avail_out == 0);
    }

    if (flush == Z_FINISH) {
        inflateEnd(&zstream_);
    }
    return Z_OK;
}

int ZlibStream::flush()
{
    iovec ivc;
    ivc.iov_base = nullptr;
    ivc.iov_len = 0;

    if (encode_) {
        return encode(&ivc, 1, true);
    } else {
        return decode(&ivc, 1, true);
    }
}

std::string ZlibStream::getResult() const
{
    std::string rt;
    for (auto& i : buffs_) {
        rt.append((const char*)i.iov_base, i.iov_len);
    }
    return rt;
}

ByteArray::ptr ZlibStream::getByteArray()
{
    ByteArray::ptr ba(new ByteArray);
    for (auto& i : buffs_) {
        ba->write(i.iov_base, i.iov_len);
    }
    ba->setPosition(0);
    return ba;
}

} // namespace chen
