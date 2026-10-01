/**
 * @file compress_util.h
 * @brief 压缩/解压工具类
 * @author Christins
 * @date 2026-06-26
 */
#pragma once

#include <string>

namespace chen {

class CompressUtil {
public:
    /**
     * @brief Gzip 压缩
     * @param data 原始数据
     * @return 压缩后的数据，失败返回空字符串
     */
    static std::string GzipCompress(const std::string& data);

    /**
     * @brief Gzip 解压
     * @param data 压缩数据
     * @return 解压后的数据，失败返回空字符串
     */
    static std::string GzipUncompress(const std::string& data);

    /**
     * @brief Zlib 压缩
     * @param data 原始数据
     * @return 压缩后的数据，失败返回空字符串
     */
    static std::string ZlibCompress(const std::string& data);

    /**
     * @brief Zlib 解压
     * @param data 压缩数据
     * @return 解压后的数据，失败返回空字符串
     */
    static std::string ZlibUncompress(const std::string& data);
};

} // namespace chen
