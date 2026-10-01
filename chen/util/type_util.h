/**
 * @file type_util.h
 * @brief 类型转换工具类
 * @author Christins
 * @date 2026-06-26
 */
#pragma once

#include <cstdint>
#include <sstream>
#include <string>

namespace chen {

class TypeUtil {
public:
    /**
     * @brief 返回字符串的第一个字符，转换为int8_t类型
     * @param str 输入字符串
     * @return int8_t 返回类型
     */
    static int8_t ToChar(const std::string& str);

    /**
     * @brief 将字符串转换为int64_t类型
     * @param str 输入字符串
     * @return int64_t 返回int64_t类型
     */
    static int64_t Atoi(const std::string& str);

    /**
     * @brief 将字符串转换为double类型
     * @param str 输入字符串
     * @return double 返回double类型
     */
    static double Atof(const std::string& str);

    /**
     * @brief 将字符串const char*的首字符转换为int8_t
     * @param str 输入字符串
     * @return int8_t 返回类型
     */
    static int8_t ToChar(const char* str);

    /**
     * @brief 将字符串const char*转换为int64_t类型
     * @param str 输入字符串
     * @return int64_t 返回int64_t类型
     */
    static int64_t Atoi(const char* str);

    /**
     * @brief 将字符串const char*转换为double类型
     * @param str 输入字符串
     * @return double 返回double类型
     */
    static double Atof(const char* str);

    /**
     * @brief 将任意类型转换为字符串
     * @tparam T 要转换的类型
     * @param value 输入值
     * @return std::string 转换后的字符串
     */
    template <class T>
    static std::string ToString(const T& value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    /**
     * @brief bool 特化：输出 "true" / "false"
     * @param value bool 值
     * @return "true" 或 "false"
     */
    static std::string ToString(bool value);
};

} // namespace chen
