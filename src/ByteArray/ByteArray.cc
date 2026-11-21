#include "ByteArray.hpp"

#include <cmath>
#include <cstring>
#include <iomanip>

#include "../Log/Log.hpp"
#include "../Util/Endian.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

ByteArray::Node::Node(size_t s)
    : ptr(new char[s])
    , next(nullptr)
    , size(s)
{
}

ByteArray::Node::Node()
    : ptr(nullptr)
    , next(nullptr)
    , size(0)
{
}

ByteArray::Node::~Node()
{
    if (ptr) {
        delete[] ptr;
    }
}

ByteArray::ByteArray(size_t base_size)
    : base_size_(base_size)
    , position_(0)
    , capacity_(base_size)
    , size_(0)
    , endian_(std::endian::big)
    , root_(new Node(base_size))
    , cur_(root_)
{
}

ByteArray::~ByteArray()
{
    Node* tmp = root_;
    while (tmp) {
        cur_ = tmp;
        tmp = tmp->next;
        delete cur_;
    }
}

static uint32_t EncodeZigzag32(const int32_t& v)
{
    if (v < 0) {
        return ((uint32_t)(-v)) * 2 - 1;
    } else {
        return v * 2;
    }
}

static uint64_t EncodeZigzag64(const int64_t& v)
{
    if (v < 0) {
        return ((uint64_t)(-v)) * 2 - 1;
    } else {
        return v * 2;
    }
}

static int32_t DecodeZigzag32(const uint32_t& v)
{
    return (v >> 1) ^ -(v & 1);
}

static int64_t DecodeZigzag64(const uint64_t& v)
{
    return (v >> 1) ^ -(v & 1);
}

void ByteArray::writeFint8(int8_t value)
{
    write(&value, sizeof(value));
}

void ByteArray::writeFuint8(uint8_t value)
{
    write(&value, sizeof(value));
}

void ByteArray::writeFint16(int16_t value)
{
    if (endian_ != std::endian::native) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFuint16(uint16_t value)
{
    if (endian_ != std::endian::native) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFint32(int32_t value)
{
    if (endian_ != std::endian::native) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFuint32(uint32_t value)
{
    if (endian_ != std::endian::native) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFint64(int64_t value)
{
    if (endian_ != std::endian::native) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeFuint64(uint64_t value)
{
    if (endian_ != std::endian::native) {
        value = byteswap(value);
    }
    write(&value, sizeof(value));
}

void ByteArray::writeInt32(int32_t value)
{
    writeUint32(EncodeZigzag32(value));
}

void ByteArray::writeUint32(uint32_t value)
{
    uint8_t tmp[5];
    uint8_t i = 0;
    /* 0x80 = 128, 0x7f = 127*/
    while (value >= 0x80) {
        tmp[i++] = (value & 0x7F) | 0x80; /* 需要保存的数据 */
        value >>= 7;
    }
    tmp[i++] = value; /* 最后小于128的数据存入 */
    write(tmp, i);
}

void ByteArray::writeInt64(int64_t value)
{
    writeUint64(EncodeZigzag64(value));
}

void ByteArray::writeUint64(uint64_t value)
{
    uint8_t tmp[10];
    uint8_t i = 0;
    while (value >= 0x80) {
        tmp[i++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    tmp[i++] = value;
    write(tmp, i);
}

void ByteArray::writeFloat(float value)
{
    uint32_t v;
    memcpy(&v, &value, sizeof(value));
    writeFuint32(v);
}

void ByteArray::writeDouble(double value)
{
    uint64_t v;
    memcpy(&v, &value, sizeof(value));
    writeFuint64(v);
}

void ByteArray::writeStringF16(const std::string& value)
{
    writeFuint16(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringF32(const std::string& value)
{
    writeFuint32(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringF64(const std::string& value)
{
    writeFuint64(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringVint(const std::string& value)
{
    writeUint64(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringWithoutLength(const std::string& value)
{
    write(value.c_str(), value.size());
}

int8_t ByteArray::readFint8()
{
    int8_t v;
    read(&v, sizeof(v));
    return v;
}

uint8_t ByteArray::readFuint8()
{
    uint8_t v;
    read(&v, sizeof(v));
    return v;
}

#define XX(type)                               \
    type v;                                    \
    read(&v, sizeof(v));                                                                                               \
    if (endian_ == std::endian::native) {                                                                              \
        return v;                              \
    } else {                                   \
        return byteswap(v);                    \
    }

int16_t ByteArray::readFint16()
{
    XX(int16_t);
}

uint16_t ByteArray::readFuint16()
{
    XX(uint16_t);
}

int32_t ByteArray::readFint32()
{
    XX(int32_t);
}

uint32_t ByteArray::readFuint32()
{
    XX(uint32_t);
}

int64_t ByteArray::readFint64()
{
    XX(int64_t);
}

uint64_t ByteArray::readFuint64()
{
    XX(uint64_t);
}

#undef XX

int32_t ByteArray::readInt32()
{
    return DecodeZigzag32(readUint32());
}

uint32_t ByteArray::readUint32()
{
    uint32_t result = 0;
    for (int i = 0; i < 32; i += 7) {
        uint8_t b = readFuint8();
        if (b < 0x80) {
            result |= ((uint32_t)b) << i;
            break;
        } else {
            result |= (((uint32_t)(b & 0x7f)) << i);
        }
    }
    return result;
}

int64_t ByteArray::readInt64()
{
    return DecodeZigzag64(readUint64());
}

uint64_t ByteArray::readUint64()
{
    uint64_t result = 0;
    for (int i = 0; i < 64; i += 7) {
        uint8_t b = readFuint8();
        if (b < 0x80) {
            result |= ((uint64_t)b) << i;
            break;
        } else {
            result |= (((uint64_t)(b & 0x7f)) << i);
        }
    }
    return result;
}

float ByteArray::readFloat()
{
    uint32_t v = readFuint32();
    float value;
    memcpy(&value, &v, sizeof(v));
    return value;
}

double ByteArray::readDouble()
{
    uint64_t v = readFuint64();
    double value;
    memcpy(&value, &v, sizeof(v));
    return value;
}

std::string ByteArray::readStringF16()
{
    uint16_t len = readFuint16();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}

std::string ByteArray::readStringF32()
{
    uint32_t len = readFuint32();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}

std::string ByteArray::readStringF64()
{
    uint64_t len = readFuint64();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}

std::string ByteArray::readStringVint()
{
    // Length is encoded as Varint (writeUint64). Decode accordingly.
    uint64_t len = readUint64();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}

void ByteArray::clear()
{
    position_ = size_ = 0;
    capacity_ = base_size_;
    Node* tmp = root_->next;
    while (tmp) {
        cur_ = tmp;
        tmp = tmp->next;
        delete cur_;
    }
    cur_ = root_;
    root_->next = nullptr;
}

// 改变操作位置
void ByteArray::write(const void* buf, size_t size)
{
    if (size == 0) {
        return;
    }
    addCapacity(size);

    // 开始写入的地方
    size_t npos = position_ % base_size_;
    // 第一个可以写入的内存块的容量
    size_t ncap = cur_->size - npos;
    // 缓冲区读的起始位置
    size_t bpos = 0;

    while (size > 0) {
        // 如果当前数据块大于要写入的数据，直接写入
        if (ncap >= size) {
            memcpy(cur_->ptr + npos, (const char*)buf + bpos, size);
            if (cur_->size == (npos + size)) {
                cur_ = cur_->next;
            }
            position_ += size;
            bpos += size;
            size = 0;
        } else {
            // 第一次是先将[buf, buf + ncp] 这个部分写入存储块，第一块满了就下一个块
            // 然后下一个写就是[buf + ncp, ncap]的部分
            memcpy(cur_->ptr + npos, (const char*)buf + bpos, ncap);
            position_ += ncap;
            bpos += ncap;
            size -= ncap;
            cur_ = cur_->next;
            ncap = cur_->size;
            npos = 0;
        }
    }

    if (position_ > size_) {
        size_ = position_;
    }
}
// 这个是改变操作位置的
void ByteArray::read(void* buf, size_t size)
{
    if (size > getReadSize()) {
        throw std::out_of_range("not enough len");
    }
    // 第一个开始读的数据块里开始读的位置
    size_t npos = position_ % base_size_;
    // 第一个开始读的数据块的剩余空间
    size_t ncap = cur_->size - npos;
    // 缓冲区开始写的位置
    size_t bpos = 0;

    while (size > 0) {
        // 如果当前数据块大于要写入的数据，直接写入
        if (ncap >= size) {
            memcpy((char*)buf + bpos, cur_->ptr + npos, size);
            if (cur_->size == (npos + size)) {
                cur_ = cur_->next;
            }
            position_ += size;
            bpos += size;
            size = 0;
        } else {
            // 第一次是先将[cur_, cur_ + ncap] 这个部分写入存储块，第一块满了就下一个块
            // 然后下一个写就是[buf + ncp, ncap]的部分
            memcpy((char*)buf + bpos, cur_->ptr + npos, ncap);
            position_ += ncap;
            bpos += ncap;
            size -= ncap;
            cur_ = cur_->next;
            ncap = cur_->size;
            npos = 0;
        }
    }
}

// 这个是不改变操作位置的
void ByteArray::read(void* buf, size_t size, size_t position) const
{
    if (size > size_ - position) {
        throw std::out_of_range("not enough len");
    }

    // 第一个开始读的数据块里开始读的位置
    size_t npos = position % base_size_;
    // 第一个开始读的数据块的剩余空间
    size_t ncap = cur_->size - npos;
    // 缓冲区开始写的位置
    size_t bpos = 0;
    Node* cur = cur_;

    while (size > 0) {
        // 如果当前数据块大于要写入的数据，直接写入
        if (ncap >= size) {
            memcpy((char*)buf + bpos, cur->ptr + npos, size);
            if (cur->size == (npos + size)) {
                cur = cur->next;
            }
            position += size;
            bpos += size;
            size = 0;
        } else {
            // 第一次是先将[cur, cur + ncap] 这个部分写入存储块，第一块满了就下一个块
            // 然后下一个写就是[buf + ncp, ncap]的部分
            memcpy((char*)buf + bpos, cur->ptr + npos, ncap);
            position += ncap;
            bpos += ncap;
            size -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
    }
}

// 这里还将读取之前链表的位置给处理了
void ByteArray::setPosition(size_t v)
{
    if (v > capacity_) {
        throw std::out_of_range("set_position out of range");
    }
    // position_ 是开始读的位置
    position_ = v;
    // 如果当前位置比数据大小还大
    if (position_ > size_) {
        size_ = position_;
    }
    // 链表遍历到当前的位置
    cur_ = root_;
    while (v > cur_->size) {
        v -= cur_->size;
        cur_ = cur_->next;
    }
    if (v == cur_->size) {
        cur_ = cur_->next;
    }
}

bool ByteArray::writeToFile(const std::string& name) const
{
    std::ofstream ofs;
    ofs.open(name, std::ios::trunc | std::ios::binary);
    if (!ofs) {
        ERROR(logger) << "writeToFile name=" << name << " error, errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }

    // 取出可以读的数据大小
    int64_t read_size = getReadSize();
    // 获取开始读的位置
    int64_t pos = position_;
    // 拿当前的数据块
    Node* cur = cur_;

    while (read_size > 0) {
        // 开始读的位置对单个数据块的大小取余可以得到开始写的数据块的起始位置
        int diff = pos % base_size_;
        int64_t len = (read_size > (int64_t)base_size_ ? base_size_ : read_size) - diff;
        ofs.write(cur->ptr + diff, len);
        cur = cur->next;
        pos += len;
        read_size -= len;
    }

    return true;
}

bool ByteArray::readFromFile(const std::string& name)
{
    std::ifstream ifs;
    ifs.open(name, std::ios::binary);
    if (!ifs) {
        ERROR(logger) << "readfromFile name=" << name << " error, errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }

    std::shared_ptr<char> buff(new char[base_size_], [](char* ptr) {
        delete[] ptr;
    });
    // 只要还能读
    while (!ifs.eof()) {
        // 每次读一个小于等于 m_baseSize大小的数据
        ifs.read(buff.get(), base_size_);
        write(buff.get(), ifs.gcount());
    }
    return true;
}

bool ByteArray::isLittleEndian() const
{
    return endian_ == std::endian::little;
}

void ByteArray::setIsLittleEndian(bool val)
{
    if (val) {
        endian_ = std::endian::little;
    } else {
        endian_ = std::endian::big;
    }
}

std::string ByteArray::toString() const
{
    std::string str;
    str.resize(getReadSize());
    if (str.empty()) {
        return str;
    }
    read(&str[0], str.size(), position_);
    return str;
}

std::string ByteArray::toHexString() const
{
    std::string str = toString();
    std::stringstream ss;

    for (size_t i = 0; i < str.size(); ++i) {
        if (i > 0 && i % 32 == 0) {
            ss << std::endl;
        }
        ss << std::setw(2) << std::setfill('0') << std::hex << (int)(uint8_t)str[i] << " ";
    }
    return ss.str();
}

uint64_t ByteArray::getWriteBuffers(std::vector<iovec>& buffers, uint64_t len)
{
    if (len == 0) {
        return 0;
    }

    addCapacity(len);
    uint64_t size = len;

    size_t npos = position_ % base_size_;
    size_t ncap = cur_->size - npos;
    struct iovec iov;
    Node* cur = cur_;

    while (len > 0) {
        if (len <= ncap) {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

uint64_t ByteArray::getReadBuffers(std::vector<iovec>& buffers, uint64_t len) const
{
    len = len > getReadSize() ? getReadSize() : len;
    if (len == 0) {
        return 0;
    }

    uint64_t size = len;
    size_t npos = position_ % base_size_;
    size_t ncap = cur_->size - npos;

    struct iovec iov;
    Node* cur = cur_;

    while (len > 0) {
        if (len <= ncap) {
            // 直接把npos后的全读了就行
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

uint64_t ByteArray::getReadBuffers(std::vector<iovec>& buffers, uint64_t len, uint64_t position) const
{
    len = len > getReadSize() ? getReadSize() : len;
    if (len == 0) {
        return 0;
    }

    uint64_t size = len;

    size_t npos = position % base_size_;
    // 需要读多少个 node
    size_t count = position / base_size_;
    Node* cur = root_;
    while (count > 0) {
        cur = cur->next;
        --count;
    }
    size_t ncap = cur->size - npos;

    struct iovec iov;
    while (len > 0) {
        if (len <= ncap) {
            // 直接把npos后的全读了就行
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

void ByteArray::addCapacity(size_t size)
{
    if (size == 0) {
        return;
    }
    // 获取以前的容量
    size_t old_cap = getCapacity();
    // 如果以前的容量够
    if (old_cap >= size) {
        return;
    }
    // 获取需要补充的容量大小
    size = size - old_cap;
    // 获取需要增加数据块的数量
    size_t count = ceil(1.0 * size / base_size_);
    Node* tmp = root_;
    // 直接移到最后一个位置
    while (tmp->next) {
        tmp = tmp->next;
    }

    Node* first = nullptr;
    for (size_t i = 0; i < count; ++i) {
        tmp->next = new Node(base_size_);
        if (first == nullptr) {
            first = tmp->next;
        }
        tmp = tmp->next;
        // 扩容
        capacity_ += base_size_;
    }
    // 如果最初没空间了
    if (old_cap == 0) {
        cur_ = first;
    }
}

} // namespace chen
