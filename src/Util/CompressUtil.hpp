/**
 * @file CompressUtil.hpp
 * @brief 压缩/解压工具类
 * @author Christins
 * @date 2026-06-26
 * @copyright GPL-3.0
 */
#pragma once

#include <cstddef>
#include <string>

namespace chen {

class CompressUtil {
public:
    /// 解压输出上限默认值（1 GiB），防止解压炸弹耗尽内存
    static constexpr size_t kDefaultMaxUncompressSize = 1024ull * 1024 * 1024;

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
     * @brief Gzip 解压（带输出上限）
     * @param data 压缩数据
     * @param max_output 输出上限，超过则视为失败
     * @return 解压后的数据，失败返回空字符串
     * @note 用重载而不是默认参数：默认参数会改变符号名，破坏已编译模块的 ABI
     */
    static std::string GzipUncompress(const std::string& data, size_t max_output);

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

    /**
     * @brief Zlib 解压（带输出上限）
     * @param data 压缩数据
     * @param max_output 输出上限，超过则视为失败
     * @return 解压后的数据，失败返回空字符串
     */
    static std::string ZlibUncompress(const std::string& data, size_t max_output);
};

} // namespace chen
