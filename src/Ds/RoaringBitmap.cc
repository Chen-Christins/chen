#include "RoaringBitmap.hpp"

#include <sstream>

namespace chen {
namespace ds {

RoaringBitmap::RoaringBitmap()
{
}

RoaringBitmap::RoaringBitmap(uint32_t size)
{
    bitmap_.addRange(0, size);
}

RoaringBitmap::RoaringBitmap(const RoaringBitmap& b)
{
    bitmap_ = b.bitmap_;
}

RoaringBitmap::RoaringBitmap(const Roaring& bitmap)
    : bitmap_(bitmap)
{
}

RoaringBitmap::~RoaringBitmap()
{
}

RoaringBitmap& RoaringBitmap::operator=(const RoaringBitmap& oth)
{
    if (this == &oth) {
        return *this;
    }
    bitmap_ = oth.bitmap_;
    return *this;
}

std::string RoaringBitmap::toString() const
{
    std::stringstream ss;
    ss << "[RoaringBitmap count=" << getCount() << " size=" << bitmap_.getSizeInBytes() << "]";
    return ss.str();
}

void RoaringBitmap::set(uint32_t idx, bool v)
{
    if (v) {
        bitmap_.add(idx);
    } else {
        bitmap_.remove(idx);
    }
}

void RoaringBitmap::set(uint32_t from, uint32_t size, bool v)
{
    bitmap_.addRange(from, from + size);
}

bool RoaringBitmap::get(uint32_t idx) const
{
    return bitmap_.contains(idx);
}

bool RoaringBitmap::get(uint32_t from, uint32_t size, bool v) const
{
    return false;
}

RoaringBitmap& RoaringBitmap::operator&=(const RoaringBitmap& oth)
{
    bitmap_ &= oth.bitmap_;
    return *this;
}

RoaringBitmap& RoaringBitmap::operator|=(const RoaringBitmap& oth)
{
    bitmap_ |= oth.bitmap_;
    return *this;
}

RoaringBitmap& RoaringBitmap::operator-=(const RoaringBitmap& oth)
{
    bitmap_ -= oth.bitmap_;
    return *this;
}

RoaringBitmap& RoaringBitmap::operator^=(const RoaringBitmap& oth)
{
    bitmap_ ^= oth.bitmap_;
    return *this;
}

RoaringBitmap RoaringBitmap::operator&(const RoaringBitmap& oth)
{
    return RoaringBitmap(bitmap_ & oth.bitmap_);
}

RoaringBitmap RoaringBitmap::operator|(const RoaringBitmap& oth)
{
    return RoaringBitmap(bitmap_ | oth.bitmap_);
}

RoaringBitmap RoaringBitmap::operator-(const RoaringBitmap& oth)
{
    return RoaringBitmap(bitmap_ - oth.bitmap_);
}

RoaringBitmap RoaringBitmap::operator^(const RoaringBitmap& oth)
{
    return RoaringBitmap(bitmap_ ^ oth.bitmap_);
}

bool RoaringBitmap::operator==(const RoaringBitmap& oth) const
{
    if (this == &oth) {
        return true;
    }
    return bitmap_ == oth.bitmap_;
}

bool RoaringBitmap::operator!=(const RoaringBitmap& oth) const
{
    return !(*this == oth);
}

RoaringBitmap::ptr RoaringBitmap::compress() const
{
    RoaringBitmap::ptr rt(new RoaringBitmap(*this));
    rt->bitmap_.shrinkToFit();
    rt->bitmap_.runOptimize();
    return rt;
}

RoaringBitmap::ptr RoaringBitmap::uncompress() const
{
    RoaringBitmap::ptr rt(new RoaringBitmap(*this));
    rt->bitmap_.removeRunCompression();
    return rt;
}

bool RoaringBitmap::any() const
{
    return bitmap_.begin() != bitmap_.end();
}

void RoaringBitmap::listPosAsc(std::vector<uint32_t>& pos)
{
    for (auto it = bitmap_.rbegin(); it != bitmap_.rend(); ++it) {
        pos.push_back(*it);
    }
}

void RoaringBitmap::foreach(std::function<bool(uint32_t)> cb)
{
    for (auto it = bitmap_.begin(); it != bitmap_.end(); ++it) {
        if (!cb(*it)) {
            break;
        }
    }
}

void RoaringBitmap::rforeach(std::function<bool(uint32_t)> cb)
{
    for (auto it = bitmap_.rbegin(); it != bitmap_.rend(); ++it) {
        if (!cb(*it)) {
            break;
        }
    }
}

void RoaringBitmap::writeTo(ByteArray::ptr ba) const
{
    size_t size = bitmap_.getSizeInBytes(false);
    ba->writeFuint32(size);
    std::vector<char> buffer(size);
    bitmap_.write(buffer.data(), false);
    ba->write(buffer.data(), size);
}

bool RoaringBitmap::writeFrom(ByteArray::ptr ba)
{
    try {
        size_t size = ba->readFuint32();
        std::vector<char> buffer(size);
        ba->read(buffer.data(), size);
        bitmap_ = Roaring::read(buffer.data(), false);
        return true;
    } catch (...) {
    }
    return false;
}

bool RoaringBitmap::cross(const RoaringBitmap& b) const
{
    return bitmap_.intersect(b.bitmap_);
}

float RoaringBitmap::getCompressRate() const
{
    return 100;
}

uint32_t RoaringBitmap::getCount() const
{
    return bitmap_.cardinality();
}

} // namespace ds
} // namespace chen
