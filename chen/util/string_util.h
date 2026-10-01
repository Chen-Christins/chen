/**
 * @file string_util.h
 * @brief 字符串工具函数
 * @author Christins
 * @date 2026-06-26
 */
#pragma once

#include <cstdarg>
#include <string>
#include <vector>

namespace chen {

/**
 * @brief 字符串工具类
 */
class StringUtil {
public:
    /**
     * @brief 格式化字符串（printf 风格）
     * @param fmt 格式化模板
     * @return 格式化后的字符串
     */
    static std::string Format(const char* fmt, ...);

    /**
     * @brief 格式化字符串，使用 va_list 参数
     * @param fmt 格式化模板
     * @param ap 变参列表
     * @return 格式化后的字符串
     */
    static std::string Formatv(const char* fmt, va_list ap);

    /**
     * @brief URL 编码（RFC 3986）
     * @param str 要编码的字符串
     * @param space_as_plus 是否将空格编码为 +（form 风格），默认 true
     * @return URL 编码后的字符串
     */
    static std::string UrlEncode(const std::string& str, bool space_as_plus = true);

    /**
     * @brief URL 解码（RFC 3986）
     * @param str 要解码的字符串
     * @param space_as_plus 是否将 + 解码为空格（form 风格），默认 true
     * @return URL 解码后的字符串
     */
    static std::string UrlDecode(const std::string& str, bool space_as_plus = true);

    /**
     * @brief 去除字符串两端的指定字符
     * @param str 输入字符串
     * @param delimit 要去除的字符集，默认空白字符
     * @return 去除两端字符后的字符串
     */
    static std::string Trim(const std::string& str, const std::string& delimit = " \t\r\n");

    /**
     * @brief 去除字符串左侧的指定字符
     * @param str 输入字符串
     * @param delimit 要去除的字符集，默认空白字符
     * @return 去除左侧字符后的字符串
     */
    static std::string TrimLeft(const std::string& str, const std::string& delimit = " \t\r\n");

    /**
     * @brief 去除字符串右侧的指定字符
     * @param str 输入字符串
     * @param delimit 要去除的字符集，默认空白字符
     * @return 去除右侧字符后的字符串
     */
    static std::string TrimRight(const std::string& str, const std::string& delimit = " \t\r\n");

    /**
     * @brief 将宽字符串（wstring）转换为多字节字符串
     * @param ws 宽字符串
     * @return 转换后的多字节字符串
     */
    static std::string WStringToString(const std::wstring& ws);

    /**
     * @brief 将多字节字符串转换为宽字符串（wstring）
     * @param s 多字节字符串
     * @return 转换后的宽字符串
     */
    static std::wstring StringToWString(const std::string& s);

    /**
     * @brief 将字符串中的指定字符替换为另一个字符
     * @param str 目标字符串
     * @param find 需要替换的字符
     * @param replaceWith 替换的字符
     * @return 替换后的字符串
     */
    static std::string Replace(const std::string &str, char find, char replaceWith);

    /**
     * @brief 将字符串中的指定字符替换为字符串
     * @param str 目标字符串
     * @param find 需要替换的字符
     * @param replaceWith 替换的字符串
     * @return 替换后的字符串
     */
    static std::string Replace(const std::string &str, char find, const std::string &replaceWith);

    /**
     * @brief 将字符串中的指定子串替换为另一个字符串
     * @param str 目标字符串
     * @param find 需要替换的子串
     * @param replaceWith 替换的字符串
     * @return 替换后的字符串
     */
    static std::string Replace(const std::string &str, const std::string &find, const std::string &replaceWith);

    /**
     * @brief 将字符串全部转换为大写
     * @param v 输入字符串
     * @return 全大写字符串
     */
    static std::string ToUpper(const std::string& v);

    /**
     * @brief 将字符串全部转换为小写
     * @param v 输入字符串
     * @return 全小写字符串
     */
    static std::string ToLower(const std::string& v);

    /**
     * @brief 不区分大小写比较两个字符串是否相等
     * @param a 字符串 a
     * @param b 字符串 b
     * @return bool 是否相等
     */
    static bool EqualsIgnoreCase(const std::string& a, const std::string& b);

    /**
     * @brief URL 编码（RFC 3986 百分号编码）
     * @param str 要编码的字符串
     * @return 编码后的字符串
     */
    static std::string URLEncode(const std::string& str);

    /**
     * @brief URL 解码（RFC 3986 百分号解码）
     * @param str 要解码的字符串
     * @return 解码后的字符串
     */
    static std::string URLDecode(const std::string& str);

    /**
     * @brief 按分隔符切分字符串
     * @param str 输入字符串
     * @param delimiter 分隔符
     * @param skip_empty 是否跳过空段，默认 true
     * @return 切分后的字符串数组
     */
    static std::vector<std::string> Split(const std::string& str, const std::string& delimiter, bool skip_empty = true);

    /**
     * @brief 按分隔符切分字符串（单字符分隔，性能更优）
     */
    static std::vector<std::string> Split(const std::string& str, char delimiter, bool skip_empty = true);

    /**
     * @brief 将字符串数组拼接为字符串
     * @param parts 字符串数组
     * @param delimiter 分隔符
     * @return 拼接后的字符串
     */
    static std::string Join(const std::vector<std::string>& parts, const std::string& delimiter);

    /**
     * @brief Base64编码
     * @param str 输入字符串
     * @return Base64编码后的字符串
     */
    static std::string Base64Encode(const std::string& str);
    
    /**
     * @brief Base64解码
     * @param str Base64编码的字符串
     * @return 解码后的字符串
     */
    static std::string Base64Decode(const std::string& str);

    /**
     * @brief Base64 URL 安全编码（RFC 4648 §5）
     * @param input 输入数据
     * @return URL 安全编码字符串（替换 +/ 为 -_，去掉末尾 = 填充）
     */
    static std::string Base64UrlEncode(const std::string& input);

    /**
     * @brief Base64 URL 安全解码（RFC 4648 §5），兼容标准 Base64
     * @param input URL 安全编码的 Base64 字符串
     * @return 解码后的原始数据
     */
    static std::string Base64UrlDecode(const std::string& input);

    /**
     * @brief Base32 编码（RFC 4648，大写字母 A-Z + 2-7 + '=' padding）
     * @param data 输入二进制数据
     * @return Base32 编码字符串
     */
    static std::string Base32Encode(const std::string& data);

    /**
     * @brief Base32 解码
     * @param encoded Base32 编码字符串
     * @return 解码后的二进制数据，无效输入返回空串
     */
    static std::string Base32Decode(const std::string& encoded);

    /**
     * @brief 二进制数据转十六进制字符串
     * @param data 二进制数据
     * @param uppercase 是否使用大写字母，默认 true
     * @return 十六进制字符串
     */
    static std::string HexEncode(const std::string& data, bool uppercase = true);

    /**
     * @brief 十六进制字符串转二进制数据
     * @param hex 十六进制字符串
     * @return 二进制数据
     */
    static std::string HexDecode(const std::string& hex);
};

} // namespace chen
