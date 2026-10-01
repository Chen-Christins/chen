#include "string_util.h"

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace chen {

std::string StringUtil::Replace(const std::string &str1, char find, char replaceWith) {
    std::string str = str1;
    size_t index = str.find(find);
    while (index != std::string::npos) {
        str[index] = replaceWith;
        index = str.find(find, index + 1);
    }
    return str;
}

std::string StringUtil::Replace(const std::string &str1, char find, const std::string &replaceWith) {
    std::string str = str1;
    size_t index = str.find(find);
    while (index != std::string::npos) {
        str = str.substr(0, index) + replaceWith + str.substr(index + 1);
        index = str.find(find, index + replaceWith.size());
    }
    return str;
}

std::string StringUtil::Replace(const std::string &str1, const std::string &find, const std::string &replaceWith) {
    std::string str = str1;
    size_t index = str.find(find);
    while (index != std::string::npos) {
        str = str.substr(0, index) + replaceWith + str.substr(index + find.size());
        index = str.find(find, index + replaceWith.size());
    }
    return str;
}

std::string StringUtil::ToUpper(const std::string& v) {
    std::string str = v;
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

std::string StringUtil::ToLower(const std::string& v) {
    std::string str = v;
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

bool StringUtil::EqualsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](char ca, char cb) {
            return std::tolower(ca) == std::tolower(cb);
        });
}

std::string StringUtil::URLEncode(const std::string& str) {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < str.length(); ++i) {
        char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else if (c == ' ') {
            encoded << '+';
        } else {
            encoded << '%' << std::setw(2) << (int)(unsigned char)c;
        }
    }
    return encoded.str();
}

std::string StringUtil::URLDecode(const std::string& str) {
    std::ostringstream decoded;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            std::istringstream iss(str.substr(i + 1, 2));
            int hex = 0;
            if (iss >> std::hex >> hex) {
                decoded << static_cast<char>(hex);
                i += 2;
            } else {
                decoded << '%';
            }
        } else if (str[i] == '+') {
            decoded << ' ';
        } else {
            decoded << str[i];
        }
    }
    return decoded.str();
}

std::string StringUtil::Format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto v = Formatv(fmt, ap);
    va_end(ap);
    return v;
}

std::string StringUtil::Formatv(const char* fmt, va_list ap) {
    char* buf = nullptr;
    auto len = vasprintf(&buf, fmt, ap);
    if(len == -1) {
        return "";
    }
    std::string ret(buf, len);
    free(buf);
    return ret;
}

static const char uri_chars[256] = {
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 1, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

static const char xdigit_chars[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

#define CHAR_IS_UNRESERVED(c)           \
    (uri_chars[(unsigned char)(c)])

std::string StringUtil::UrlEncode(const std::string& str, bool space_as_plus) {
    static const char *hexdigits = "0123456789ABCDEF";
    std::string* ss = nullptr;
    const char* end = str.c_str() + str.length();
    for(const char* c = str.c_str() ; c < end; ++c) {
        if(!CHAR_IS_UNRESERVED(*c)) {
            if(!ss) {
                ss = new std::string;
                ss->reserve(str.size() * 1.2);
                ss->append(str.c_str(), c - str.c_str());
            }
            if(*c == ' ' && space_as_plus) {
                ss->append(1, '+');
            } else {
                ss->append(1, '%');
                ss->append(1, hexdigits[(uint8_t)*c >> 4]);
                ss->append(1, hexdigits[*c & 0xf]);
            }
        } else if(ss) {
            ss->append(1, *c);
        }
    }
    if(!ss) {
        return str;
    } else {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

std::string StringUtil::UrlDecode(const std::string& str, bool space_as_plus) {
    std::string* ss = nullptr;
    const char* end = str.c_str() + str.length();
    for(const char* c = str.c_str(); c < end; ++c) {
        if(*c == '+' && space_as_plus) {
            if(!ss) {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            ss->append(1, ' ');
        } else if(*c == '%' && (c + 2) < end
                    && isxdigit(*(c + 1)) && isxdigit(*(c + 2))){
            if(!ss) {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            ss->append(1, (char)(xdigit_chars[(int)*(c + 1)] << 4 | xdigit_chars[(int)*(c + 2)]));
            c += 2;
        } else if(ss) {
            ss->append(1, *c);
        }
    }
    if(!ss) {
        return str;
    } else {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

std::string StringUtil::Trim(const std::string& str, const std::string& delimit) {
    auto begin = str.find_first_not_of(delimit);
    if(begin == std::string::npos) {
        return "";
    }
    auto end = str.find_last_not_of(delimit);
    return str.substr(begin, end - begin + 1);
}

std::string StringUtil::TrimLeft(const std::string& str, const std::string& delimit) {
    auto begin = str.find_first_not_of(delimit);
    if(begin == std::string::npos) {
        return "";
    }
    return str.substr(begin);
}

std::string StringUtil::TrimRight(const std::string& str, const std::string& delimit) {
    auto end = str.find_last_not_of(delimit);
    if(end == std::string::npos) {
        return "";
    }
    return str.substr(0, end + 1);
}

std::string StringUtil::WStringToString(const std::wstring& ws) {
    std::string str_locale = setlocale(LC_ALL, "");
    const wchar_t* wch_src = ws.c_str();
    size_t n_dest_size = wcstombs(NULL, wch_src, 0) + 1;
    char *ch_dest = new char[n_dest_size];
    memset(ch_dest, 0, n_dest_size);
    wcstombs(ch_dest, wch_src, n_dest_size);
    std::string str_result = ch_dest;
    delete []ch_dest;
    setlocale(LC_ALL, str_locale.c_str());
    return str_result;
}

std::wstring StringUtil::StringToWString(const std::string& s) {
    std::string str_locale = setlocale(LC_ALL, "");
    const char* chSrc = s.c_str();
    size_t n_dest_size = mbstowcs(NULL, chSrc, 0) + 1;
    wchar_t* wch_dest = new wchar_t[n_dest_size];
    wmemset(wch_dest, 0, n_dest_size);
    mbstowcs(wch_dest, chSrc, n_dest_size);
    std::wstring wstr_result = wch_dest;
    delete []wch_dest;
    setlocale(LC_ALL, str_locale.c_str());
    return wstr_result;
}

// ===== Split =====

std::vector<std::string> StringUtil::Split(const std::string& str, const std::string& delimiter, bool skip_empty) {
    std::vector<std::string> result;
    if (delimiter.empty()) {
        result.push_back(str);
        return result;
    }
    size_t start = 0;
    while (true) {
        size_t pos = str.find(delimiter, start);
        if (pos == std::string::npos) {
            break;
        }
        if (!skip_empty || pos > start) {
            result.push_back(str.substr(start, pos - start));
        }
        start = pos + delimiter.size();
    }
    if (!skip_empty || start < str.size()) {
        result.push_back(str.substr(start));
    }
    return result;
}

std::vector<std::string> StringUtil::Split(const std::string& str, char delimiter, bool skip_empty) {
    std::vector<std::string> result;
    size_t start = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == delimiter) {
            if (!skip_empty || i > start) {
                result.push_back(str.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    if (!skip_empty || start < str.size()) {
        result.push_back(str.substr(start));
    }
    return result;
}

std::string StringUtil::Join(const std::vector<std::string>& parts, const std::string& delimiter) {
    if (parts.empty()) {
        return "";
    }
    std::string result;
    result.reserve(parts.size() * 64);
    result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        result += delimiter;
        result += parts[i];
    }
    return result;
}

// ===== Base64 =====

static const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const unsigned char kBase64DecodeTable[256] = {
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x3e,0x80,0x80,0x80,0x3f,
    0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,
    0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x80,0x80,0x80,0x80,0x80,
    0x80,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
};

std::string StringUtil::Base64Encode(const std::string& str) {
    const unsigned char* data = reinterpret_cast<const unsigned char*>(str.data());
    size_t len = str.size();
    std::string result;
    result.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        unsigned int triple = (static_cast<unsigned int>(data[i]) << 16)
                           | (i + 1 < len ? static_cast<unsigned int>(data[i + 1]) << 8 : 0)
                           | (i + 2 < len ? static_cast<unsigned int>(data[i + 2]) : 0);

        result += kBase64Alphabet[(triple >> 18) & 0x3f];
        result += kBase64Alphabet[(triple >> 12) & 0x3f];
        result += (i + 1 < len) ? kBase64Alphabet[(triple >> 6) & 0x3f] : '=';
        result += (i + 2 < len) ? kBase64Alphabet[triple & 0x3f] : '=';
    }
    return result;
}

std::string StringUtil::Base64UrlEncode(const std::string& input) {
    std::string encoded = Base64Encode(input);
    // URL-safe: 替换 +/ 为 -_
    for (auto& c : encoded) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    // 去掉末尾的 = 填充
    auto pos = encoded.find('=');
    if (pos != std::string::npos) {
        encoded.resize(pos);
    }
    return encoded;
}

std::string StringUtil::Base64UrlDecode(const std::string& input) {
    if (input.empty()) {
        return "";
    }
    // 还原 URL-safe 字符
    std::string normalized = input;
    for (auto& c : normalized) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
    }
    // 补 = padding 到 4 的倍数
    switch (normalized.size() % 4) {
    case 1: {
        return "";  // 长度不合法
    }
    case 2: {
        normalized += "==";
        break;
    }
    case 3: {
        normalized += "=";
        break;
    }
    default: break;     // 0: 正好
    }

    // Base64Decode 遇到 = 会 break，手动解码处理 padding
    std::string result;
    result.reserve(normalized.size() / 4 * 3);

    for (size_t i = 0; i < normalized.size(); i += 4) {
        // 计算这组有几个有效字节（排除 padding）
        int validBytes = 3;
        if (normalized[i + 2] == '=') {
            validBytes = 1;
        } else if (normalized[i + 3] == '=') {
            validBytes = 2;
        }

        if (validBytes < 1) continue;

        unsigned char c0 = kBase64DecodeTable[static_cast<unsigned char>(normalized[i])];
        unsigned char c1 = kBase64DecodeTable[static_cast<unsigned char>(normalized[i + 1])];
        unsigned char c2 = validBytes >= 2
            ? kBase64DecodeTable[static_cast<unsigned char>(normalized[i + 2])] : 0;
        unsigned char c3 = validBytes >= 3
            ? kBase64DecodeTable[static_cast<unsigned char>(normalized[i + 3])] : 0;

        // 检查无效字符
        if (c0 == 0x80 || c1 == 0x80
                || (validBytes >= 2 && c2 == 0x80)
                || (validBytes >= 3 && c3 == 0x80)) {
            return "";
        }

        unsigned int triple = (static_cast<unsigned int>(c0) << 18)
                            | (static_cast<unsigned int>(c1) << 12)
                            | (static_cast<unsigned int>(c2) << 6)
                            | static_cast<unsigned int>(c3);

        result += static_cast<char>((triple >> 16) & 0xff);
        if (validBytes >= 2) {
            result += static_cast<char>((triple >> 8) & 0xff);
        }
        if (validBytes >= 3) {
            result += static_cast<char>(triple & 0xff);
        }
    }
    return result;
}

std::string StringUtil::Base64Decode(const std::string& str) {
    if (str.empty()) {
        return "";
    }
    std::string result;
    result.reserve(str.size() / 4 * 3);

    for (size_t i = 0; i < str.size(); i += 4) {
        // 前两个字符必须是有效 Base64 字符（不能是 =）
        unsigned char c0 = kBase64DecodeTable[static_cast<unsigned char>(str[i])];
        unsigned char c1 = kBase64DecodeTable[static_cast<unsigned char>(str[i + 1])];

        if (c0 == 0x80 || c1 == 0x80) {
            break;
        }

        unsigned int triple = (static_cast<unsigned int>(c0) << 18)
                            | (static_cast<unsigned int>(c1) << 12);

        // 第3个字符：可能是有效 Base64 字符或 '=' padding
        if (i + 2 < str.size() && str[i + 2] != '=') {
            unsigned char c2 = kBase64DecodeTable[static_cast<unsigned char>(str[i + 2])];
            if (c2 == 0x80) {
                break;
            }
            triple |= (static_cast<unsigned int>(c2) << 6);
        }

        // 第4个字符：可能是有效 Base64 字符或 '=' padding
        if (i + 3 < str.size() && str[i + 3] != '=') {
            unsigned char c3 = kBase64DecodeTable[static_cast<unsigned char>(str[i + 3])];
            if (c3 == 0x80) {
                break;
            }
            triple |= static_cast<unsigned int>(c3);
        }

        result += static_cast<char>((triple >> 16) & 0xff);
        if (i + 2 < str.size() && str[i + 2] != '=') {
            result += static_cast<char>((triple >> 8) & 0xff);
        }
        if (i + 3 < str.size() && str[i + 3] != '=') {
            result += static_cast<char>(triple & 0xff);
        }
    }
    return result;
}

// ===== Base32 =====

static const char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string StringUtil::Base32Encode(const std::string& data) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
    size_t len = data.size();
    std::string result;
    result.reserve((len * 8 + 4) / 5);

    int bits = 0;
    uint32_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        value = (value << 8) | bytes[i];
        bits += 8;
        while (bits >= 5) {
            result += kBase32Alphabet[(value >> (bits - 5)) & 0x1F];
            bits -= 5;
        }
    }
    if (bits > 0) {
        result += kBase32Alphabet[(value << (5 - bits)) & 0x1F];
    }
    while (result.size() % 8 != 0) {
        result += '=';
    }
    return result;
}

std::string StringUtil::Base32Decode(const std::string& encoded) {
    if (encoded.empty()) {
        return "";
    }
    std::string result;
    int bits = 0;
    uint32_t value = 0;

    for (char c : encoded) {
        if (c == '=' || c == ' ') {
            continue;
        }
        int idx = -1;
        if (c >= 'A' && c <= 'Z') {
            idx = c - 'A';
        } else if (c >= '2' && c <= '7') {
            idx = c - '2' + 26;
        } else if (c >= 'a' && c <= 'z') {
            idx = c - 'a';
        }
        if (idx < 0) {
            continue;
        }
        value = (value << 5) | idx;
        bits += 5;
        if (bits >= 8) {
            result += static_cast<char>((value >> (bits - 8)) & 0xFF);
            bits -= 8;
        }
    }
    return result;
}

// ===== Hex =====

std::string StringUtil::HexEncode(const std::string& data, bool uppercase) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    if (uppercase) {
        oss << std::uppercase;
    }
    for (unsigned char c : data) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    return oss.str();
}

std::string StringUtil::HexDecode(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return "";
    }
    std::string result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        char hi = std::tolower(hex[i]);
        char lo = std::tolower(hex[i + 1]);
        auto hexval = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
            if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(c - 'a' + 10); }
            return 0;
        };
        result += static_cast<char>((hexval(hi) << 4) | hexval(lo));
    }
    return result;
}

} // namespace chen
