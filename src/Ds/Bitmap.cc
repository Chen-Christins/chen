#include "Bitmap.hpp"

#include <iostream>
#include <math.h>
#include <sstream>
#include <string.h>

#include "../Util/Macro.hpp"

namespace chen {
namespace ds {

// static uint8_t HEAD[] = {0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF};
// static uint8_t TAIL[] = {0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF};
// static uint8_t POS[] = {0x01, 0x02, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};

Bitmap::base_type Bitmap::POS[sizeof(base_type) * 8];
Bitmap::base_type Bitmap::NPOS[sizeof(base_type) * 8];
Bitmap::base_type Bitmap::MASK[sizeof(base_type) * 8];

bool Bitmap::init()
{
    for (size_t i = 0; i < (sizeof(base_type) * 8); ++i) {
        POS[i] = ((base_type)1) << i;
        NPOS[i] = ~POS[i];
        MASK[i] = POS[i] - 1;
    }
    return true;
}

static bool s_init = Bitmap::init();

Bitmap::Bitmap(uint32_t size, uint8_t def)
    : compress_(false)
    , size_(size)
    , data_size_(ceil(size * 1.0 / VALUE_SIZE))
    , data_(NULL)
{
    if (data_size_) {
        data_ = (base_type*)malloc(data_size_ * sizeof(base_type));
        memset(data_, def, data_size_ * sizeof(base_type));
    }
}

Bitmap::Bitmap()
    : compress_(true)
    , size_(0)
    , data_size_(0)
    , data_(NULL)
{
}

Bitmap::Bitmap(const Bitmap& b)
{
    compress_ = b.compress_;
    size_ = b.size_;
    data_size_ = b.data_size_;
    data_ = (base_type*)malloc(data_size_ * sizeof(base_type));
    memcpy(data_, b.data_, data_size_ * sizeof(base_type));
}

Bitmap::~Bitmap()
{
    if (data_) {
        free(data_);
    }
}

void Bitmap::writeTo(ByteArray::ptr ba) const
{
    ba->writeFuint8(compress_);
    ba->writeFuint32(size_);
    ba->writeFuint32(data_size_);
    ba->write((const char*)data_, data_size_ * sizeof(base_type));
}

bool Bitmap::readFrom(ByteArray::ptr ba)
{
    try {
        compress_ = ba->readFuint8();
        size_ = ba->readFuint32();
        data_size_ = ba->readFuint32();
        if (data_) {
            free(data_);
        }
        data_ = (base_type*)malloc(data_size_ * sizeof(base_type));
        ba->read((char*)data_, data_size_ * sizeof(base_type));
        return true;
    } catch (...) {
    }
    return false;
}

Bitmap& Bitmap::operator=(const Bitmap& b)
{
    if (this == &b) {
        return *this;
    }
    compress_ = b.compress_;
    size_ = b.size_;
    data_size_ = b.data_size_;
    if (data_) {
        free(data_);
    }
    data_ = (base_type*)malloc(data_size_ * sizeof(base_type));
    memcpy(data_, b.data_, data_size_ * sizeof(base_type));
    return *this;
}

Bitmap& Bitmap::operator&=(const Bitmap& b)
{
    if (size_ != b.size_) {
        throw std::logic_error("m_size != b.m_size");
    }

    if (!compress_ && !b.compress_) {
        uint32_t max_size = size_ / U64_VALUE_SIZE;
        for (uint32_t i = 0; i < max_size; ++i) {
            ((uint64_t*)data_)[i] &= ((uint64_t*)b.data_)[i];
        }
        for (uint32_t i = max_size * U64_DIV_BASE; i < data_size_; ++i) {
            data_[i] &= b.data_[i];
        }
    } else if (compress_ != b.compress_) {
        if (compress_) {
            throw std::logic_error("compress &= uncompress not support");
        }
        uint32_t cur_pos = 0;
        for (uint32_t i = 0; i < b.data_size_; ++i) {
            base_type cur = b.data_[i];
            if (cur & COMPRESS_MASK) {
                uint32_t count = cur & COUNT_MASK;
                bool v = cur & kValueMask;
                uint32_t tmp_i = i;
                for (uint32_t n = i + 1; n < b.data_size_; ++n) {
                    if ((b.data_[n] & COMPRESS_MASK) && (v == (bool)(b.data_[n] & kValueMask))) {
                        count = (((uint32_t)(b.data_[n] & COUNT_MASK)) << (VALUE_SIZE * (n - tmp_i))) | count;
                        ++i;
                    } else {
                        break;
                    }
                }
                if (!v) {
                    set(cur_pos, count, 0);
                    cur_pos += count;
                } else {
                    cur_pos += count;
                }
            } else {
                base_type count = cur & COUNT_MASK;
                data_[cur_pos / VALUE_SIZE] &= count;
                cur_pos += VALUE_SIZE;
            }
        }
    } else {
        throw std::logic_error("compress &= compress not support");
    }
    return *this;
}

Bitmap& Bitmap::operator~()
{
    if (!compress_) {
        uint32_t max_size = size_ / U64_VALUE_SIZE;
        for (uint32_t i = 0; i < max_size; ++i) {
            ((uint64_t*)data_)[i] = ~(((uint64_t*)data_)[i]);
        }
        for (uint32_t i = max_size * U64_DIV_BASE; i < data_size_; ++i) {
            data_[i] = ~(data_[i]);
        }
        return *this;
    } else {
        for (uint32_t i = 0; i < data_size_; ++i) {
            base_type cur = data_[i];
            if (cur & COMPRESS_MASK) {
                if (cur & kValueMask) {
                    data_[i] = cur & kNotValueMask;
                } else {
                    data_[i] = cur | kValueMask;
                }
            } else {
                data_[i] = (~cur) & COUNT_MASK;
            }
        }
        return *this;
    }
}

Bitmap& Bitmap::operator|=(const Bitmap& b)
{
    if (size_ != b.size_) {
        throw std::logic_error("m_size != b.m_size");
    }

    if (!compress_ && !b.compress_) {
        uint32_t max_size = size_ / U64_VALUE_SIZE;
        for (uint32_t i = 0; i < max_size; ++i) {
            ((uint64_t*)data_)[i] |= ((uint64_t*)b.data_)[i];
        }
        for (uint32_t i = max_size * U64_DIV_BASE; i < data_size_; ++i) {
            data_[i] |= b.data_[i];
        }
    } else if (compress_ != b.compress_) {
        if (compress_) {
            throw std::logic_error("compress |= uncompress not support");
        }
        uint32_t cur_pos = 0;
        for (uint32_t i = 0; i < b.data_size_; ++i) {
            base_type cur = b.data_[i];
            if (cur & COMPRESS_MASK) {
                uint32_t count = cur & COUNT_MASK;
                bool v = cur & kValueMask;
                uint32_t tmp_i = i;
                for (uint32_t n = i + 1; n < b.data_size_; ++n) {
                    if ((b.data_[n] & COMPRESS_MASK) && (v == (bool)(b.data_[n] & kValueMask))) {
                        count = (((uint32_t)(b.data_[n] & COUNT_MASK)) << (VALUE_SIZE * (n - tmp_i))) | count;
                        ++i;
                    } else {
                        break;
                    }
                }
                if (v) {
                    set(cur_pos, count, 1);
                    cur_pos += count;
                } else {
                    cur_pos += count;
                }
            } else {
                base_type count = cur & COUNT_MASK;
                data_[cur_pos / VALUE_SIZE] |= count;
                cur_pos += VALUE_SIZE;
            }
        }
    } else {
        throw std::logic_error("compress |= compress not support");
    }
    return *this;
}

bool Bitmap::operator==(const Bitmap& b) const
{
    if (this == &b) {
        return true;
    }
    if (compress_ != b.compress_ || size_ != b.size_ || data_size_ != b.data_size_) {
        return false;
    }
    if (memcmp(data_, b.data_, data_size_ * sizeof(base_type))) {
        return false;
    }
    uint32_t left = size_ % VALUE_SIZE;
    if (left) {
        // base_type mask = MASK[left];//((base_type)1 << left) - 1;
        return (data_[data_size_ - 1] & MASK[left]) == (b.data_[b.data_size_ - 1] & MASK[left]);
    }
    return true;
}

bool Bitmap::operator!=(const Bitmap& b) const
{
    return !(*this == b);
}

Bitmap Bitmap::operator&(const Bitmap& b)
{
    Bitmap t(*this);
    return t &= b;
}

Bitmap Bitmap::operator|(const Bitmap& b)
{
    Bitmap t(*this);
    return t |= b;
}

std::string Bitmap::toString() const
{
    std::stringstream ss;
    ss << "[Bitmap compress=" << compress_ << " size=" << size_ << " data_size=" << data_size_ << " data=";
    for (size_t i = 0; i < data_size_; ++i) {
        if (i) {
            ss << ",";
        }
        ss << data_[i];
    }
    // if(!compress_) {
    //     uint32_t cur_pos = 0;
    //     for(uint32_t i = 0; i < data_size_; ++i) {
    //         base_type cur = data_[i];
    //         for(uint32_t n = 0; n < VALUE_SIZE && cur_pos < size_; ++n) {
    //             ++cur_pos;
    //             ss << (bool)(cur & ((base_type)1 << n));
    //         }
    //     }
    // } else {
    //     for(uint32_t i = 0; i < data_size_; ++i) {
    //         if(i > 0) {
    //             ss << ",";
    //         }
    //         base_type cur = data_[i];
    //         for(uint32_t n = 0; n < VALUE_SIZE; ++n) {
    //             ss << (bool)(cur & ((base_type)1 << n));
    //         }
    //     }
    // }
    ss << "]";
    return ss.str();
}

bool Bitmap::get(uint32_t idx) const
{
    uint32_t cur = idx / VALUE_SIZE;
    uint32_t pos = idx % VALUE_SIZE;
    return data_[cur] & POS[pos];
}

void Bitmap::set(uint32_t idx, bool v)
{
    uint32_t cur = idx / VALUE_SIZE;
    uint32_t pos = idx % VALUE_SIZE;
    if (v) {
        data_[cur] |= POS[pos];
    } else {
        data_[cur] &= NPOS[pos];
    }
}

template <class T>
T count_bits(const T& v)
{
    return __builtin_popcount(v);
}

template <>
uint64_t count_bits(const uint64_t& v)
{
    return __builtin_popcount(v & 0xFFFFFFFF) + __builtin_popcount(v >> 32);
}

Bitmap::ptr Bitmap::compress() const
{
    if (compress_) {
        return ptr(new Bitmap(*this));
    }

    base_type* data = (base_type*)malloc(data_size_ * sizeof(base_type));
    bool cur_value = false;
    uint64_t cur_count = 0;
    uint32_t dst_cur_pos = 0;
    for (uint32_t i = 0; i < data_size_; ++i) {
        base_type cur = data_[i];
        auto c = count_bits((cur & COUNT_MASK));
        if (c == 0) {
            if (cur_value == false) {
                cur_count += VALUE_SIZE;
                continue;
            }
        } else if (c == VALUE_SIZE) {
            if (cur_value == true) {
                cur_count += VALUE_SIZE;
                continue;
            }
        }

        if ((cur_count / VALUE_SIZE) > 1) {
            ASSERT(cur_count % VALUE_SIZE == 0);
            // ASSERT(cur_count < (1ul << VALUE_SIZE));
            while (cur_count) {
                data[dst_cur_pos++] = cur_value ? (COMPRESS_MASK | kValueMask | (cur_count & COUNT_MASK))
                                                : (COMPRESS_MASK | (cur_count & COUNT_MASK));
                cur_count >>= VALUE_SIZE;
            }
        } else {
            if (cur_count) {
                data[dst_cur_pos++] = cur_value ? COUNT_MASK : 0;
            }
        }

        if (c == 0 || c == VALUE_SIZE) {
            cur_value = (c == VALUE_SIZE);
            cur_count = VALUE_SIZE;
        } else {
            data[dst_cur_pos++] = cur;
            cur_count = 0;
        }
    }

    if (cur_count > 0) {
        if ((cur_count / VALUE_SIZE) > 1) {
            while (cur_count) {
                data[dst_cur_pos++] = cur_value ? (COMPRESS_MASK | kValueMask | (cur_count & COUNT_MASK))
                                                : (COMPRESS_MASK | (cur_count & COUNT_MASK));
                cur_count >>= VALUE_SIZE;
            }
        } else {
            data[dst_cur_pos++] = cur_value ? (COUNT_MASK) : (0);
        }
    }

    Bitmap::ptr b(new Bitmap);
    b->compress_ = true;
    b->size_ = size_;
    b->data_size_ = dst_cur_pos;
    b->data_ = (base_type*)malloc(dst_cur_pos * sizeof(base_type));
    memcpy(b->data_, data, dst_cur_pos * sizeof(base_type));
    free(data);
    return b;
}

Bitmap::ptr Bitmap::uncompress() const
{
    if (!compress_) {
        return ptr(new Bitmap(*this));
    }
    Bitmap::ptr b(new Bitmap(size_));
    uint32_t cur_pos = 0;
    for (uint32_t i = 0; i < data_size_;) {
        base_type cur = data_[i];
        if (cur & COMPRESS_MASK) {
            uint32_t count = cur & COUNT_MASK;
            bool v = cur & kValueMask;
            uint32_t tmp_i = i;
            for (uint32_t n = i + 1; n < data_size_; ++n) {
                if ((data_[n] & COMPRESS_MASK) && (v == (bool)(data_[n] & kValueMask))) {
                    count = (((uint32_t)(data_[n] & COUNT_MASK)) << (VALUE_SIZE * (n - tmp_i))) | count;
                    ++i;
                } else {
                    break;
                }
            }
            b->set(cur_pos, count, v);
            cur_pos += count;
            ++i;
        } else {
            base_type count = cur & COUNT_MASK;
            b->data_[cur_pos / VALUE_SIZE] = count;
            cur_pos += VALUE_SIZE;
            ++i;
        }
    }
    return b;
}

bool Bitmap::any() const
{
    if (!data_) {
        return false;
    }
    if (!compress_) {
        for (uint32_t i = 0; i < data_size_ - 1; ++i) {
            if (data_[i]) {
                return true;
            }
        }
        return data_[data_size_ - 1] & MASK[size_ % VALUE_SIZE]; //(((base_type)1 << (size_ % VALUE_SIZE)) - 1);
    } else {
        for (uint32_t i = 0; i < data_size_; ++i) {
            uint8_t cur = data_[i];
            if (cur & COMPRESS_MASK) {
                if (cur & kValueMask) {
                    return true;
                }
            } else {
                if (cur) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Bitmap::resize(uint32_t size, bool v)
{
    if (compress_) {
        throw std::logic_error("compress bitmap not support resize");
    }
    uint32_t len = ceil(size * 1.0 / VALUE_SIZE);
    if (len > data_size_) {
        base_type* new_data = (base_type*)malloc(len * sizeof(base_type));
        memcpy(new_data, data_, data_size_ * sizeof(base_type));
        if (v) {
            memset(new_data + data_size_, 0xFF, (len - data_size_) * sizeof(base_type));
        } else {
            memset(new_data + data_size_, 0, (len - data_size_) * sizeof(base_type));
        }

        if (data_) {
            free(data_);
        }
        data_ = new_data;

        uint32_t left = size_ % VALUE_SIZE;
        if (v) {
            new_data[data_size_ - 1] |= MASK[left]; //((base_type)1 << left) - 1;
        } else {
            new_data[data_size_ - 1] &= MASK[left]; //((base_type)1 << left) - 1;
        }
    }
    size_ = size;
    data_size_ = len;
}

void Bitmap::foreach(std::function<bool(uint32_t)> cb)
{
    uint32_t max_size = size_ / U64_VALUE_SIZE;
    uint32_t cur_pos = 0;
    uint64_t tmp = 0;
    for (uint32_t i = 0; i < max_size; ++i) {
        tmp = ((uint64_t*)data_)[i];
        if (!tmp) {
            cur_pos += U64_VALUE_SIZE;
        } else {
            for (uint32_t n = 0; n < U64_DIV_BASE; ++n) {
                tmp = data_[i * U64_DIV_BASE + n];
                for (uint32_t x = 0; x < VALUE_SIZE && cur_pos < size_; ++x, ++cur_pos) {
                    if (tmp & POS[x]) { //((base_type)1 << x)) {
                        if (!cb(cur_pos)) {
                            return;
                        }
                    }
                }
            }
        }
    }
    for (uint32_t i = max_size * U64_DIV_BASE; i < data_size_; ++i) {
        tmp = data_[i];
        if (!tmp) {
            cur_pos += VALUE_SIZE;
        } else {
            for (uint32_t n = 0; n < VALUE_SIZE && cur_pos < size_; ++n, ++cur_pos) {
                if (tmp & POS[n]) { //(1UL << n)) {
                    if (!cb(cur_pos)) {
                        return;
                    }
                }
            }
        }
    }
}

void Bitmap::rforeach(std::function<bool(uint32_t)> cb)
{
    uint32_t max_size = size_ / U64_VALUE_SIZE;
    uint32_t cur_pos = size_ - 1;
    uint64_t tmp = 0;

    if (size_ % U64_VALUE_SIZE) {
        int32_t start = data_size_ - 1;
        for (int32_t i = start; i >= (int32_t)(max_size * U64_DIV_BASE); --i) {
            tmp = data_[i];
            if (!tmp) {
                if (i == start) {
                    if (size_ % VALUE_SIZE == 0) {
                        cur_pos -= VALUE_SIZE;
                    } else {
                        cur_pos -= size_ % VALUE_SIZE;
                    }
                } else {
                    cur_pos -= VALUE_SIZE;
                }
            } else {
                if (i == start) {
                    if (size_ % VALUE_SIZE == 0) {
                        for (int32_t n = VALUE_SIZE - 1; n >= 0; --n, --cur_pos) {
                            if (tmp & POS[n]) { //((base_type)1 << n)) {
                                if (!cb(cur_pos)) {
                                    return;
                                }
                            }
                        }
                    } else {
                        cur_pos += 1;
                        for (int32_t n = size_ % VALUE_SIZE; n >= 0; --n, --cur_pos) {
                            if (tmp & POS[n]) { //((base_type)1 << n)) {
                                if (!cb(cur_pos)) {
                                    return;
                                }
                            }
                        }
                    }
                } else {
                    for (int32_t n = VALUE_SIZE - 1; n >= 0; --n, --cur_pos) {
                        if (tmp & POS[n]) { //((base_type)1 << n)) {
                            if (!cb(cur_pos)) {
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    // for(int32_t i = begin;
    //         i >= 0; --i){
    //     tmp = data_[i];
    //     for(int32_t x = VALUE_SIZE - 1; x >= 0; --x, --cur_pos) {
    //         if(tmp & ((base_type)1 << x)) {
    //             if(!cb(cur_pos)) {
    //                 return;
    //             }
    //         }
    //     }
    // }
    // return;

    for (int32_t i = max_size - 1; i >= 0; --i) {
        tmp = ((uint64_t*)data_)[i];
        if (!tmp) {
            cur_pos -= U64_VALUE_SIZE;
        } else {
            for (int32_t n = 0; n < (int32_t)U64_DIV_BASE; ++n) {
                tmp = data_[(i + 1) * U64_DIV_BASE - 1 - n];
                for (int32_t x = VALUE_SIZE - 1; x >= 0; --x, --cur_pos) {
                    if (tmp & POS[x]) { //((base_type)1 << x)) {
                        if (!cb(cur_pos)) {
                            return;
                        }
                    }
                }
            }
        }
    }
}

void Bitmap::listPosAsc(std::vector<uint32_t>& pos)
{
    uint32_t max_size = size_ / U64_VALUE_SIZE;
    uint32_t cur_pos = 0;
    uint64_t tmp = 0;
    for (uint32_t i = 0; i < max_size; ++i) {
        tmp = ((uint64_t*)data_)[i];
        if (!tmp) {
            cur_pos += U64_VALUE_SIZE;
        } else {
            for (size_t x = 0; x < U64_DIV_BASE; ++x) {
                tmp = data_[i * U64_DIV_BASE + x];
                for (uint32_t n = 0; n < VALUE_SIZE && cur_pos < size_; ++n, ++cur_pos) {
                    if (tmp & POS[n]) { //(1UL << n)) {
                        pos.push_back(cur_pos);
                    }
                }
            }
        }
    }
    for (uint32_t i = max_size * U64_DIV_BASE; i < data_size_; ++i) {
        tmp = data_[i];
        if (!tmp) {
            cur_pos += VALUE_SIZE;
        } else {
            for (uint32_t n = 0; n < VALUE_SIZE && cur_pos < size_; ++n, ++cur_pos) {
                if (tmp & POS[n]) { //(1UL << n)) {
                    pos.push_back(cur_pos);
                }
            }
        }
    }
}

bool Bitmap::cross(const Bitmap& b) const
{
    if (!compress_ && b.compress_) {
        return compressCross(b);
    } else if (!compress_ && !b.compress_) {
        return normalCross(b);
    } else {
        // LOG_ERROR() << "Bitmap cross invalid type";
    }
    return false;
}

bool Bitmap::compressCross(const Bitmap& b) const
{
    uint32_t cur_pos = 0;
    for (uint32_t i = 0; i < b.data_size_; ++i) {
        base_type cur = b.data_[i];
        if (cur & COMPRESS_MASK) {
            uint32_t count = cur & COUNT_MASK;
            bool v = cur & kValueMask;
            uint32_t tmp_i = i;
            for (uint32_t n = i + 1; n < b.data_size_; ++n) {
                if ((b.data_[n] & COMPRESS_MASK) && (v == (bool)(b.data_[n] & kValueMask))) {
                    count = (((uint32_t)(b.data_[n] & COUNT_MASK)) << (VALUE_SIZE * (n - tmp_i))) | count;
                    ++i;
                } else {
                    break;
                }
            }
            if (v) {
                if (get(cur_pos, count, 1)) {
                    return true;
                }
                cur_pos += count;
            } else {
                cur_pos += count;
            }
        } else {
            base_type count = cur & COUNT_MASK;
            if (data_[cur_pos / VALUE_SIZE] & count) {
                return true;
            }
            cur_pos += VALUE_SIZE;
        }
    }
    return false;
}

bool Bitmap::normalCross(const Bitmap& b) const
{
    uint32_t max_size = size_ / U64_VALUE_SIZE;
    for (uint32_t i = 0; i < max_size; ++i) {
        if ((((uint64_t*)data_)[i]) != 0) {
            if ((((uint64_t*)data_)[i]) & (((uint64_t*)b.data_)[i])) {
                return true;
            }
        }
    }
    for (uint32_t i = max_size * U64_DIV_BASE; i < data_size_; ++i) {
        if (data_[i] & b.data_[i]) {
            return true;
        }
    }
    return false;
}

Bitmap::iterator_base::iterator_base()
    : pos_(-1)
    , size_(0)
    , data_size_(0)
    , compress_(false)
    , data_(NULL)
{
}

Bitmap::iterator_base::~iterator_base()
{
}

bool Bitmap::iterator_base::operator!()
{
    if (pos_ >= size_ || pos_ < 0) {
        return false;
    }
    return true;
}

int32_t Bitmap::iterator_base::operator*()
{
    return pos_;
}

Bitmap::iterator::iterator()
{
}

Bitmap::iterator::iterator(Bitmap* b)
{
    pos_ = -1;
    size_ = b->size_;
    data_size_ = b->data_size_;
    data_ = b->data_;
    compress_ = b->compress_;

    iterator::next();
}

void Bitmap::iterator::next()
{
    ++pos_;
    uint32_t pos = pos_ / VALUE_SIZE;
    uint32_t left = pos_ % VALUE_SIZE;
    base_type v = 0;
    if (left) {
        v = data_[pos];
        for (int32_t i = left; i < (int32_t)VALUE_SIZE && pos_ < size_; ++i, ++pos_) {
            if (v & POS[i]) { //((base_type)1 << i)) {
                return;
            }
        }
        ++pos;
    }
    for (int32_t i = pos_ / VALUE_SIZE; i < data_size_ && pos_ < size_; ++i) {
        v = data_[i];
        if (!v) {
            pos_ += VALUE_SIZE;
            continue;
        }
        for (int32_t n = 0; n < (int32_t)VALUE_SIZE && pos_ < size_; ++n, ++pos_) {
            if (v & POS[n]) { //((base_type)1 << n)) {
                return;
            }
        }
    }
}

Bitmap::iterator_reverse::iterator_reverse()
{
}

Bitmap::iterator_reverse::iterator_reverse(Bitmap* b)
{
    pos_ = b->size_;
    size_ = b->size_;
    data_size_ = b->data_size_;
    data_ = b->data_;
    compress_ = b->compress_;

    iterator_reverse::next();
}

void Bitmap::iterator_reverse::next()
{
    if (pos_ <= 0) {
        pos_ = -1;
        return;
    }

    --pos_;
    int32_t left = (pos_) % VALUE_SIZE;
    int32_t pos = (pos_) / VALUE_SIZE;
    base_type v = 0;
    if (left != (VALUE_SIZE - 1)) {
        v = data_[pos];
        for (int32_t i = left; i >= 0 && pos_ >= 0; --i, --pos_) {
            if (v & POS[i]) { //((base_type)1 << i)) {
                return;
            }
        }
        --pos;
    }
    for (int32_t i = pos; i >= 0 && pos_ >= 0; --i) {
        v = data_[i];
        if (!v) {
            pos_ -= VALUE_SIZE;
            continue;
        }
        for (int32_t n = VALUE_SIZE - 1; n >= 0 && pos_ >= 0; --n, --pos_) {
            if (v & POS[n]) { //((base_type)1 << n)) {
                return;
            }
        }
    }
}

void Bitmap::set(uint32_t from, uint32_t size, bool v)
{
    if (size == 0) {
        return;
    }
    uint32_t cur_from = from / VALUE_SIZE;
    uint32_t pos_from = from % VALUE_SIZE;
    uint32_t cur_to = (from + size) / VALUE_SIZE;
    uint32_t pos_to = (from + size) % VALUE_SIZE;

    ASSERT(pos_from == 0);
    ASSERT(pos_to == 0);

    for (uint32_t i = cur_from; i < cur_to; ++i) {
        data_[i] = v ? (COUNT_MASK) : (0);
    }
}

bool Bitmap::get(uint32_t from, uint32_t size, bool v) const
{
    if (size == 0) {
        return false;
    }
    uint32_t cur_from = from / VALUE_SIZE;
    uint32_t pos_from = from % VALUE_SIZE;
    uint32_t cur_to = (from + size) / VALUE_SIZE;
    uint32_t pos_to = (from + size) % VALUE_SIZE;

    ASSERT(pos_from == 0);
    ASSERT(pos_to == 0);

    for (uint32_t i = cur_from; i < cur_to; ++i) {
        if (v) {
            if (data_[i] & COUNT_MASK) {
                return true;
            }
        } else {
            if (!(data_[i] & COUNT_MASK)) {
                return true;
            }
        }
    }
    return false;
}

float Bitmap::getCompressRate() const
{
    if (!compress_) {
        return 100;
    } else {
        return data_size_ * 100.0 / ceil(size_ * 1.0 / VALUE_SIZE);
    }
}

Bitmap::iterator_base::ptr Bitmap::begin_new()
{
    return iterator_base::ptr(new iterator(this));
}

Bitmap::iterator_base::ptr Bitmap::rbegin_new()
{
    return iterator_base::ptr(new iterator_reverse(this));
}

uint32_t Bitmap::getCount() const
{
    if (!compress_) {
        uint32_t len = data_size_ / U64_DIV_BASE; // - (data_size_ % 8 == 0 ? 1 : 0);
        uint32_t count = 0;
        uint32_t cur_pos = 0;
        for (uint32_t i = 0; i < len; ++i) {
            uint64_t tmp = ((uint64_t*)(data_))[i];
            count += count_bits(tmp);
            cur_pos += U64_VALUE_SIZE;
        }
        for (uint32_t i = len * U64_DIV_BASE; i < data_size_; ++i) {
            base_type tmp = data_[i];
            if (tmp) {
                for (uint32_t n = 0; n < VALUE_SIZE && cur_pos < size_; ++n, ++cur_pos) {
                    if (tmp & POS[n]) { //(1UL << n)) {
                        ++count;
                    }
                }
            } else {
                cur_pos += VALUE_SIZE;
            }
        }
        return count;
    } else {
        uint32_t count = 0;
        for (uint32_t i = 0; i < data_size_; ++i) {
            base_type tmp = data_[i];
            if (tmp & COMPRESS_MASK) {
                if (tmp & kValueMask) {
                    count += tmp & COUNT_MASK;
                }
            } else {
                count += count_bits(tmp);
            }
        }
        return count;
    }
}

} // namespace ds
} // namespace chen
