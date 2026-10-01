/**
 * @file endian.h
 * @brief 主要提供了一些字节序转换的方法
 * @details 由于本地自己序和网络字节序的差异
 *          这个模块提供了大端序和小端序的转换
 * @author Christins
 * @date 2024-11-20
 */
#pragma once

#include <byteswap.h>
#include <stdint.h>
#include <type_traits>

#define CHEN_LITTLE_ENDIAN 1
#define CHEN_BIG_ENDIAN 2

namespace chen {

template <class T>
typename std::enable_if<sizeof(T) == sizeof(uint64_t), T>::type
byteswap(T value) {
    return (T)bswap_64((uint64_t)value);
}

template <class T>
typename std::enable_if<sizeof(T) == sizeof(uint32_t), T>::type
byteswap(T value) {
    return (T)bswap_32((uint32_t)value);
}

template <class T>
typename std::enable_if<sizeof(T) == sizeof(uint16_t), T>::type
byteswap(T value) {
    return (T)bswap_16((uint16_t)value);
}

#if BYTE_ORDER == BIG_ENDIAN
#define CHEN_BYTE_ORDER CHEN_BIG_ENDIAN
#else
#define CHEN_BYTE_ORDER CHEN_LITTLE_ENDIAN
#endif

// 如果机器是大端序
#if CHEN_BYTE_ORDER == CHEN_BIG_ENDIAN

/**
 * @brief 如果本机是小端序，那么直接返回就行
 */
template <class T>
T byteswapOnLittleEndian(T t) {
    return t;
}

/**
 * @brief 如果本机是大端序，那么转换成小段序返回
 */
template <class T>
T byteswapOnBigEndian(T t) {
    return byteswap(t);
}

#else 

/**
 * @brief 如果本机是小端序，那么转换为大端序返回
 */
template <class T>
T byteswapOnLittleEndian(T t) {
    return byteswap(t);
}

/**
 * @brief 如果本机是大端序，那么直接返回就行
 */
template <class T>
T byteswapOnBigEndian(T t) {
    return t;
}

#endif

/**
 * @brief 从字节流按大端序读取 uint8_t，pos 前进 1 字节
 */
inline uint8_t ReadBigEndianUint8(const char* buf, size_t& pos) {
    return static_cast<uint8_t>(buf[pos++]);
}

/**
 * @brief 从字节流按大端序读取 uint16_t，pos 前进 2 字节
 */
inline uint16_t ReadBigEndianUint16(const char* buf, size_t& pos) {
    uint16_t value = static_cast<uint16_t>(static_cast<uint8_t>(buf[pos]) << 8)
        | static_cast<uint8_t>(buf[pos + 1]);
    pos += 2;
    return value;
}

/**
 * @brief 从字节流按大端序读取 uint32_t，pos 前进 4 字节
 */
inline uint32_t ReadBigEndianUint32(const char* buf, size_t& pos) {
    uint32_t value = static_cast<uint32_t>(static_cast<uint8_t>(buf[pos])) << 24
        | static_cast<uint32_t>(static_cast<uint8_t>(buf[pos + 1])) << 16
        | static_cast<uint32_t>(static_cast<uint8_t>(buf[pos + 2])) << 8
        | static_cast<uint8_t>(buf[pos + 3]);
    pos += 4;
    return value;
}

/**
 * @brief 从字节流按大端序读取 uint64_t，pos 前进 8 字节
 */
inline uint64_t ReadBigEndianUint64(const char* buf, size_t& pos) {
    uint64_t value = static_cast<uint64_t>(static_cast<uint8_t>(buf[pos])) << 56
        | static_cast<uint64_t>(static_cast<uint8_t>(buf[pos + 1])) << 48
        | static_cast<uint64_t>(static_cast<uint8_t>(buf[pos + 2])) << 40
        | static_cast<uint64_t>(static_cast<uint8_t>(buf[pos + 3])) << 32
        | static_cast<uint64_t>(static_cast<uint8_t>(buf[pos + 4])) << 24
        | static_cast<uint64_t>(static_cast<uint8_t>(buf[pos + 5])) << 16
        | static_cast<uint64_t>(static_cast<uint8_t>(buf[pos + 6])) << 8
        | static_cast<uint8_t>(buf[pos + 7]);
    pos += 8;
    return value;
}

/**
 * @brief 按大端序写入 uint8_t，pos 前进 1 字节
 */
inline void WriteBigEndianUint8(char* buf, size_t& pos, uint8_t value) {
    buf[pos++] = static_cast<char>(value);
}

/**
 * @brief 按大端序写入 uint16_t，pos 前进 2 字节
 */
inline void WriteBigEndianUint16(char* buf, size_t& pos, uint16_t value) {
    buf[pos++] = static_cast<char>((value >> 8) & 0xff);
    buf[pos++] = static_cast<char>(value & 0xff);
}

/**
 * @brief 按大端序写入 uint32_t，pos 前进 4 字节
 */
inline void WriteBigEndianUint32(char* buf, size_t& pos, uint32_t value) {
    buf[pos++] = static_cast<char>((value >> 24) & 0xff);
    buf[pos++] = static_cast<char>((value >> 16) & 0xff);
    buf[pos++] = static_cast<char>((value >> 8) & 0xff);
    buf[pos++] = static_cast<char>(value & 0xff);
}

/**
 * @brief 按大端序写入 uint64_t，pos 前进 8 字节
 */
inline void WriteBigEndianUint64(char* buf, size_t& pos, uint64_t value) {
    buf[pos++] = static_cast<char>((value >> 56) & 0xff);
    buf[pos++] = static_cast<char>((value >> 48) & 0xff);
    buf[pos++] = static_cast<char>((value >> 40) & 0xff);
    buf[pos++] = static_cast<char>((value >> 32) & 0xff);
    buf[pos++] = static_cast<char>((value >> 24) & 0xff);
    buf[pos++] = static_cast<char>((value >> 16) & 0xff);
    buf[pos++] = static_cast<char>((value >> 8) & 0xff);
    buf[pos++] = static_cast<char>(value & 0xff);
}

} // namespace chen
