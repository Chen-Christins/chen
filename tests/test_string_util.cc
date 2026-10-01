/**
 * @file test_string_util.cc
 * @brief 测试 StringUtil 编码/解码及其他字符串工具的往返正确性
 * @author Christins
 * @date 2026-06-30
 */
#include "chen/util/string_util.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace chen;

// ========== URL 编码/解码 往返测试 ==========

void test_url_encode_decode_roundtrip_basic() {
    std::cout << "=== test_url_encode_decode_roundtrip_basic ===" << std::endl;

    // UrlEncode/UrlDecode（小写，支持 space_as_plus）
    std::string original = "hello world! this is a test & more=value";
    std::string encoded = StringUtil::UrlEncode(original);
    std::string decoded = StringUtil::UrlDecode(encoded);
    assert(decoded == original);

    std::cout << "  original: " << original << std::endl;
    std::cout << "  encoded:  " << encoded << std::endl;
    std::cout << "  decoded:  " << decoded << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_url_encode_decode_roundtrip_various() {
    std::cout << "=== test_url_encode_decode_roundtrip_various ===" << std::endl;

    std::vector<std::string> inputs = {
        "",
        "a",
        "hello world",
        "user@example.com",
        "key1=value1&key2=value2",
        "path/to/file.txt",
        "中文测试",
        "special chars: !@#$%^&*()",
        "/?query=hello world#fragment",
        "spaces   and+++plus",
        "~.-_",  // unreserved chars
        "100% sure",
        "already%20encoded",
        std::string(256, 'x') + " with spaces at end ",
    };

    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];

        // 使用默认参数 space_as_plus=true
        std::string enc1 = StringUtil::UrlEncode(input, true);
        std::string dec1 = StringUtil::UrlDecode(enc1, true);
        assert(dec1 == input);

        // 使用 space_as_plus=false
        std::string enc2 = StringUtil::UrlEncode(input, false);
        std::string dec2 = StringUtil::UrlDecode(enc2, false);
        assert(dec2 == input);

        std::cout << "  input[" << i << "] len=" << input.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_url_encode_decode_space_as_plus() {
    std::cout << "=== test_url_encode_decode_space_as_plus ===" << std::endl;

    std::string text = "hello world foo bar";

    // space_as_plus=true: 空格→+
    std::string enc_plus = StringUtil::UrlEncode(text, true);
    assert(enc_plus.find('+') != std::string::npos);
    assert(enc_plus.find("%20") == std::string::npos);
    assert(StringUtil::UrlDecode(enc_plus, true) == text);

    // space_as_plus=false: 空格→%20
    std::string enc_pct = StringUtil::UrlEncode(text, false);
    assert(enc_pct.find("%20") != std::string::npos);
    assert(StringUtil::UrlDecode(enc_pct, false) == text);

    // 交叉解码：space_as_plus=true 解码 %20 应该还是 %20（不转换）
    // space_as_plus=false 解码 + 也不转换

    std::cout << "  space_as_plus=true:  " << enc_plus << std::endl;
    std::cout << "  space_as_plus=false: " << enc_pct << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_URLEncode_Decode_roundtrip() {
    std::cout << "=== test_URLEncode_Decode_roundtrip ===" << std::endl;

    // URLEncode/URLDecode（大写，固定 space→+）
    std::vector<std::string> inputs = {
        "",
        "hello world",
        "test@example.com?q=foo&bar=baz",
        "中文 URL 测试",
        "~.-_unreserved",
        "!@#$%^&*()[]{}|;':\",./<>?",
        std::string("a\0b\0c", 5),  // 含 null 字节
    };

    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        std::string enc = StringUtil::URLEncode(input);
        std::string dec = StringUtil::URLDecode(enc);
        assert(dec == input);

        std::cout << "  input[" << i << "] len=" << input.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_url_decode_invalid_input() {
    std::cout << "=== test_url_decode_invalid_input ===" << std::endl;

    // 不完整的百分号编码
    std::string incomplete1 = StringUtil::UrlDecode("hello%2", true);
    std::string incomplete2 = StringUtil::UrlDecode("hello%", true);
    // 应该不会崩溃，保留原样或尽量解码
    std::cout << "  incomplete %2: " << incomplete1 << std::endl;
    std::cout << "  incomplete %:  " << incomplete2 << std::endl;

    // 非法十六进制字符
    std::string invalid = StringUtil::UrlDecode("hello%GGworld", true);
    std::cout << "  invalid %GG:   " << invalid << std::endl;

    // 空字符串
    assert(StringUtil::UrlDecode("") == "");
    assert(StringUtil::UrlEncode("") == "");

    std::cout << "PASS" << std::endl;
}

// ========== Base64 编码/解码 往返测试 ==========

void test_base64_roundtrip_basic() {
    std::cout << "=== test_base64_roundtrip_basic ===" << std::endl;

    std::string original = "Hello, World! This is a Base64 test.";
    std::string encoded = StringUtil::Base64Encode(original);
    std::string decoded = StringUtil::Base64Decode(encoded);

    assert(!encoded.empty());
    assert(decoded == original);

    std::cout << "  original: " << original << std::endl;
    std::cout << "  encoded:  " << encoded << std::endl;
    std::cout << "  decoded:  " << decoded << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_base64_roundtrip_various() {
    std::cout << "=== test_base64_roundtrip_various ===" << std::endl;

    std::vector<std::string> inputs = {
        "",                              // 空
        "a",                             // 1 byte: padding ==
        "ab",                            // 2 bytes: padding =
        "abc",                           // 3 bytes: no padding
        "abcd",                          // 4 bytes: padding ==
        "Hello, World!",                 // 普通文本
        std::string(100, 'B'),           // 100 bytes
        std::string(1024, 'C'),          // 1KB
        "中文 Base64 测试",               // UTF-8
        std::string("\x00\x01\x02\x03\xFF\xFE\xFD", 7),  // 二进制
        std::string("\x00" "\x00" "\x00", 3),             // 全零
        std::string("\xFF" "\xFF" "\xFF", 3),             // 全 FF
    };

    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        std::string enc = StringUtil::Base64Encode(input);
        std::string dec = StringUtil::Base64Decode(enc);

        assert(dec == input);
        std::cout << "  input[" << i << "] len=" << input.size()
                  << " enc_len=" << enc.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_base64_known_vectors() {
    std::cout << "=== test_base64_known_vectors ===" << std::endl;

    // RFC 4648 已知测试向量
    assert(StringUtil::Base64Encode("") == "");
    assert(StringUtil::Base64Encode("f") == "Zg==");
    assert(StringUtil::Base64Encode("fo") == "Zm8=");
    assert(StringUtil::Base64Encode("foo") == "Zm9v");
    assert(StringUtil::Base64Encode("foob") == "Zm9vYg==");
    assert(StringUtil::Base64Encode("fooba") == "Zm9vYmE=");
    assert(StringUtil::Base64Encode("foobar") == "Zm9vYmFy");

    // 解码验证
    assert(StringUtil::Base64Decode("Zg==") == "f");
    assert(StringUtil::Base64Decode("Zm8=") == "fo");
    assert(StringUtil::Base64Decode("Zm9v") == "foo");
    assert(StringUtil::Base64Decode("Zm9vYg==") == "foob");
    assert(StringUtil::Base64Decode("Zm9vYmE=") == "fooba");
    assert(StringUtil::Base64Decode("Zm9vYmFy") == "foobar");

    // Base64UrlEncode/Decode 全部向量
    assert(StringUtil::Base64UrlEncode("f") == "Zg");
    assert(StringUtil::Base64UrlEncode("fo") == "Zm8");
    assert(StringUtil::Base64UrlEncode("foo") == "Zm9v");
    assert(StringUtil::Base64UrlDecode("Zg") == "f");
    assert(StringUtil::Base64UrlDecode("Zm8") == "fo");
    assert(StringUtil::Base64UrlDecode("Zm9v") == "foo");

    std::cout << "  all RFC 4648 vectors pass" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_base64_url_roundtrip_basic() {
    std::cout << "=== test_base64_url_roundtrip_basic ===" << std::endl;

    std::string original = "Hello, World! Base64URL test.";
    std::string encoded = StringUtil::Base64UrlEncode(original);
    std::string decoded = StringUtil::Base64UrlDecode(encoded);

    assert(!encoded.empty());
    assert(decoded == original);

    // URL-safe: 不应该包含 + / =
    assert(encoded.find('+') == std::string::npos);
    assert(encoded.find('/') == std::string::npos);
    assert(encoded.find('=') == std::string::npos);

    std::cout << "  original: " << original << std::endl;
    std::cout << "  encoded:  " << encoded << std::endl;
    std::cout << "  decoded:  " << decoded << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_base64_url_roundtrip_various() {
    std::cout << "=== test_base64_url_roundtrip_various ===" << std::endl;

    std::vector<std::string> inputs = {
        "",
        "a",
        "ab",
        "abc",
        "Hello, World!",
        "{\"sub\":\"1234567890\",\"name\":\"John Doe\",\"iat\":1516239022}",
        std::string(256, 'X'),
        std::string("\x00\x01\x02\xFF\xFE\xFD", 6),
    };

    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        std::string enc = StringUtil::Base64UrlEncode(input);
        std::string dec = StringUtil::Base64UrlDecode(enc);

        assert(dec == input);

        // 验证 URL-safe 特征
        assert(enc.find('+') == std::string::npos);
        assert(enc.find('/') == std::string::npos);
        assert(enc.find('=') == std::string::npos);

        std::cout << "  input[" << i << "] len=" << input.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_base64_url_decode_compat() {
    std::cout << "=== test_base64_url_decode_compat ===" << std::endl;

    // Base64UrlDecode 应兼容标准 Base64 格式（含 +/=）
    std::string original = "test compat with standard base64!";
    std::string std_enc = StringUtil::Base64Encode(original);

    // 标准 Base64 可能包含 +/=，Base64UrlDecode 应该能解码
    std::string dec = StringUtil::Base64UrlDecode(std_enc);
    assert(dec == original);

    // 同时，不带 padding 的也应该能解码
    std::string url_enc = StringUtil::Base64UrlEncode(original);
    std::string dec2 = StringUtil::Base64UrlDecode(url_enc);
    assert(dec2 == original);

    std::cout << "  standard base64 decoded by UrlDecode: OK" << std::endl;
    std::cout << "  url-safe base64 decoded by UrlDecode: OK" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_base64_decode_invalid() {
    std::cout << "=== test_base64_decode_invalid ===" << std::endl;

    // 空字符串
    assert(StringUtil::Base64Decode("") == "");

    // 非法字符应该能处理（不崩溃）
    std::string s1 = StringUtil::Base64Decode("!!!!");
    std::cout << "  Base64Decode(\"!!!!\") = len=" << s1.size() << std::endl;

    // Base64UrlDecode 长度不合法
    std::string s2 = StringUtil::Base64UrlDecode("a");  // 余 1 → 非法
    assert(s2 == "");
    std::cout << "  Base64UrlDecode(\"a\") returns empty (invalid len)" << std::endl;

    std::cout << "PASS" << std::endl;
}

// ========== Hex 编码/解码 往返测试 ==========

void test_hex_roundtrip_basic() {
    std::cout << "=== test_hex_roundtrip_basic ===" << std::endl;

    std::string original = "Hello, Hex!";
    std::string encoded = StringUtil::HexEncode(original);
    std::string decoded = StringUtil::HexDecode(encoded);

    assert(!encoded.empty());
    assert(encoded.size() == original.size() * 2);
    assert(decoded == original);

    std::cout << "  original: " << original << std::endl;
    std::cout << "  encoded:  " << encoded << std::endl;
    std::cout << "  decoded:  " << decoded << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_hex_roundtrip_various() {
    std::cout << "=== test_hex_roundtrip_various ===" << std::endl;

    std::vector<std::string> inputs = {
        "",
        "\x00",
        "\xFF",
        "\x00\x01\x02\x03",
        "\xDE\xAD\xBE\xEF",
        "Hello, World!",
        std::string(256, '\xAB'),
        std::string("\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F", 16),
        std::string("\x10\x20\x30\x40\x50\x60\x70\x80\x90\xA0\xB0\xC0\xD0\xE0\xF0", 15),
    };

    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        std::string enc = StringUtil::HexEncode(input);
        assert(enc.size() == input.size() * 2);

        std::string dec = StringUtil::HexDecode(enc);
        assert(dec == input);

        std::cout << "  input[" << i << "] len=" << input.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_hex_encode_case() {
    std::cout << "=== test_hex_encode_case ===" << std::endl;

    std::string data("\xAB\xCD\xEF", 3);

    // 大写（默认）
    std::string upper = StringUtil::HexEncode(data, true);
    assert(upper == "ABCDEF");
    for (char c : upper) {
        assert(!std::islower(c));
    }

    // 小写
    std::string lower = StringUtil::HexEncode(data, false);
    assert(lower == "abcdef");
    for (char c : lower) {
        assert(!std::isupper(c) || !std::isalpha(c));
    }

    // 大小写都应能正确解码
    assert(StringUtil::HexDecode(upper) == data);
    assert(StringUtil::HexDecode(lower) == data);
    assert(StringUtil::HexDecode("AbCdEf") == data);  // 混合大小写

    std::cout << "  uppercase: " << upper << std::endl;
    std::cout << "  lowercase: " << lower << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_hex_decode_invalid() {
    std::cout << "=== test_hex_decode_invalid ===" << std::endl;

    // 奇数长度返回空
    assert(StringUtil::HexDecode("ABC") == "");
    assert(StringUtil::HexDecode("A") == "");

    // 空字符串
    assert(StringUtil::HexDecode("") == "");

    // 非法字符（实现使用 tolower + 范围检查，非法字符当作 0）
    std::string invalid = StringUtil::HexDecode("GG");
    std::cout << "  HexDecode(\"GG\") len=" << invalid.size() << std::endl;

    std::cout << "PASS" << std::endl;
}

// ========== Split / Join 往返测试 ==========

void test_split_join_roundtrip() {
    std::cout << "=== test_split_join_roundtrip ===" << std::endl;

    // Split → Join 往返（字符串分隔符）
    std::vector<std::string> test_cases = {
        "a,b,c",
        "one::two::three",
        "hello world",
        "",
        "single",
        "a,,b,,c",  // 含空段
    };
    std::vector<std::string> delimiters = {",", "::", " ", "|"};

    for (const auto& delim : delimiters) {
        for (size_t i = 0; i < test_cases.size(); ++i) {
            const auto& input = test_cases[i];
            auto parts = StringUtil::Split(input, delim, false);  // 保留空段
            std::string joined = StringUtil::Join(parts, delim);
            assert(joined == input);

            auto parts_char = StringUtil::Split(input, delim[0], false);
            if (delim.size() == 1) {
                std::string joined_char = StringUtil::Join(parts_char, std::string(1, delim[0]));
                assert(joined_char == input);
            }
        }
    }

    std::cout << "  split→join roundtrip works for all test cases" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_split_skip_empty() {
    std::cout << "=== test_split_skip_empty ===" << std::endl;

    // skip_empty=true（默认）
    auto parts1 = StringUtil::Split("a,,b,,c", ",");
    assert(parts1.size() == 3);  // ["a", "b", "c"]
    assert(parts1[0] == "a" && parts1[1] == "b" && parts1[2] == "c");

    // skip_empty=false
    auto parts2 = StringUtil::Split("a,,b,,c", ",", false);
    assert(parts2.size() == 5);  // ["a", "", "b", "", "c"]

    // 首尾空段
    auto parts3 = StringUtil::Split(",leading,trailing,", ",", false);
    assert(parts3.size() == 4);  // ["", "leading", "trailing", ""]

    auto parts4 = StringUtil::Split(",leading,trailing,", ",");
    assert(parts4.size() == 2);  // ["leading", "trailing"]

    std::cout << "  skip_empty=true: " << parts1.size() << " parts" << std::endl;
    std::cout << "  skip_empty=false: " << parts2.size() << " parts" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_split_char_delimiter() {
    std::cout << "=== test_split_char_delimiter ===" << std::endl;

    auto parts = StringUtil::Split("one:two:three", ':');
    assert(parts.size() == 3);
    assert(parts[0] == "one" && parts[1] == "two" && parts[2] == "three");

    auto parts2 = StringUtil::Split("no-delimiter", ':');
    assert(parts2.size() == 1);
    assert(parts2[0] == "no-delimiter");

    std::cout << "PASS" << std::endl;
}

void test_join_empty() {
    std::cout << "=== test_join_empty ===" << std::endl;

    std::vector<std::string> empty;
    assert(StringUtil::Join(empty, ",") == "");

    std::vector<std::string> one = {"only"};
    assert(StringUtil::Join(one, ",") == "only");
    assert(StringUtil::Join(one, "::") == "only");

    std::cout << "PASS" << std::endl;
}

// ========== Trim 测试 ==========

void test_trim_basic() {
    std::cout << "=== test_trim_basic ===" << std::endl;

    assert(StringUtil::Trim("  hello  ") == "hello");
    assert(StringUtil::Trim("\t\n hello \t\n") == "hello");
    assert(StringUtil::Trim("no-whitespace") == "no-whitespace");
    assert(StringUtil::Trim("   ") == "");
    assert(StringUtil::Trim("") == "");

    std::cout << "PASS" << std::endl;
}

void test_trim_left_right() {
    std::cout << "=== test_trim_left_right ===" << std::endl;

    assert(StringUtil::TrimLeft("  hello  ") == "hello  ");
    assert(StringUtil::TrimRight("  hello  ") == "  hello");
    assert(StringUtil::TrimLeft("hello", "he") == "llo");
    assert(StringUtil::TrimRight("hello", "lo") == "he");
    assert(StringUtil::Trim("xxxhelloxxx", "x") == "hello");

    std::cout << "PASS" << std::endl;
}

void test_trim_custom_delimit() {
    std::cout << "=== test_trim_custom_delimit ===" << std::endl;

    assert(StringUtil::Trim("...test...", ".") == "test");
    assert(StringUtil::Trim("abc123abc", "abc") == "123");
    assert(StringUtil::Trim("###header###", "#") == "header");
    assert(StringUtil::Trim("__underscore__", "_") == "underscore");

    std::cout << "PASS" << std::endl;
}

// ========== Replace 测试 ==========

void test_replace_char_char() {
    std::cout << "=== test_replace_char_char ===" << std::endl;

    assert(StringUtil::Replace("hello", 'l', 'x') == "hexxo");
    assert(StringUtil::Replace("aaaa", 'a', 'b') == "bbbb");
    assert(StringUtil::Replace("nothing", 'z', 'x') == "nothing");
    assert(StringUtil::Replace("", 'a', 'b') == "");

    std::cout << "PASS" << std::endl;
}

void test_replace_char_string() {
    std::cout << "=== test_replace_char_string ===" << std::endl;

    assert(StringUtil::Replace("hello", 'l', "LL") == "heLLLLo");
    assert(StringUtil::Replace("a b c", ' ', "_") == "a_b_c");

    std::cout << "PASS" << std::endl;
}

void test_replace_string_string() {
    std::cout << "=== test_replace_string_string ===" << std::endl;

    assert(StringUtil::Replace("hello world", "world", "C++") == "hello C++");
    assert(StringUtil::Replace("aaaa", "aa", "b") == "bb");  // 非重叠替换
    assert(StringUtil::Replace("one two one two", "one", "1") == "1 two 1 two");
    assert(StringUtil::Replace("nothing", "xyz", "abc") == "nothing");
    assert(StringUtil::Replace("", "a", "b") == "");

    std::cout << "PASS" << std::endl;
}

// ========== Case 测试 ==========

void test_to_upper_lower() {
    std::cout << "=== test_to_upper_lower ===" << std::endl;

    assert(StringUtil::ToUpper("hello") == "HELLO");
    assert(StringUtil::ToLower("HELLO") == "hello");
    assert(StringUtil::ToUpper("Hello World") == "HELLO WORLD");
    assert(StringUtil::ToLower("Hello World") == "hello world");
    assert(StringUtil::ToUpper("") == "");
    assert(StringUtil::ToLower("") == "");
    assert(StringUtil::ToUpper("123abc!@#") == "123ABC!@#");

    // ToUpper → ToLower 往返
    std::string mixed = "MiXeD CaSe StRiNg";
    assert(StringUtil::ToLower(StringUtil::ToUpper(mixed)) == StringUtil::ToLower(mixed));

    std::cout << "PASS" << std::endl;
}

void test_equals_ignore_case() {
    std::cout << "=== test_equals_ignore_case ===" << std::endl;

    assert(StringUtil::EqualsIgnoreCase("hello", "HELLO") == true);
    assert(StringUtil::EqualsIgnoreCase("Hello", "hello") == true);
    assert(StringUtil::EqualsIgnoreCase("", "") == true);
    assert(StringUtil::EqualsIgnoreCase("abc", "ABC") == true);
    assert(StringUtil::EqualsIgnoreCase("hello", "world") == false);
    assert(StringUtil::EqualsIgnoreCase("abc", "abcd") == false);
    assert(StringUtil::EqualsIgnoreCase("", "a") == false);

    std::cout << "PASS" << std::endl;
}

// ========== Format 测试 ==========

void test_format_basic() {
    std::cout << "=== test_format_basic ===" << std::endl;

    std::string s1 = StringUtil::Format("Hello, %s!", "World");
    assert(s1 == "Hello, World!");

    std::string s2 = StringUtil::Format("%d + %d = %d", 1, 2, 3);
    assert(s2 == "1 + 2 = 3");

    std::string s3 = StringUtil::Format("float: %.2f", 3.14159);
    // 注意：printf 受 locale 影响，小数点可能是 . 或 ,
    assert(s3.find("float:") == 0);
    assert(s3.size() > 7);  // "float: " + at least 3 chars for the number

    std::string s4 = StringUtil::Format("");
    assert(s4 == "");

    std::cout << "  Format(\"Hello, %s!\", \"World\") = " << s1 << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== WString ↔ String 转换测试 ==========

void test_wstring_string_roundtrip() {
    std::cout << "=== test_wstring_string_roundtrip ===" << std::endl;

    std::string original = "Hello, Wide String!";
    std::wstring ws = StringUtil::StringToWString(original);
    std::string back = StringUtil::WStringToString(ws);
    assert(back == original);

    // 中文
    std::string chinese = "你好，宽字符串测试！";
    std::wstring wc = StringUtil::StringToWString(chinese);
    std::string cb = StringUtil::WStringToString(wc);
    assert(cb == chinese);

    // 空字符串
    std::wstring empty_ws = StringUtil::StringToWString("");
    std::string empty_back = StringUtil::WStringToString(empty_ws);
    assert(empty_back == "");

    std::cout << "  ASCII roundtrip: OK" << std::endl;
    std::cout << "  Chinese roundtrip: OK" << std::endl;
    std::cout << "  Empty string roundtrip: OK" << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== 编码确定性测试 ==========

void test_url_encode_deterministic() {
    std::cout << "=== test_url_encode_deterministic ===" << std::endl;

    std::string input = "test string with spaces & special chars!";

    std::string enc1 = StringUtil::UrlEncode(input);
    std::string enc2 = StringUtil::UrlEncode(input);
    assert(enc1 == enc2);

    std::string uenc1 = StringUtil::URLEncode(input);
    std::string uenc2 = StringUtil::URLEncode(input);
    assert(uenc1 == uenc2);

    std::cout << "  UrlEncode deterministic: OK" << std::endl;
    std::cout << "  URLEncode deterministic: OK" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_base64_encode_deterministic() {
    std::cout << "=== test_base64_encode_deterministic ===" << std::endl;

    std::string input = "Deterministic base64 encoding test";

    std::string enc1 = StringUtil::Base64Encode(input);
    std::string enc2 = StringUtil::Base64Encode(input);
    assert(enc1 == enc2);

    std::string uenc1 = StringUtil::Base64UrlEncode(input);
    std::string uenc2 = StringUtil::Base64UrlEncode(input);
    assert(uenc1 == uenc2);

    std::cout << "PASS" << std::endl;
}

void test_hex_encode_deterministic() {
    std::cout << "=== test_hex_encode_deterministic ===" << std::endl;

    std::string input("\x01\x02\x03\x04\xFF\xFE\xFD\xFC", 8);

    std::string enc1 = StringUtil::HexEncode(input);
    std::string enc2 = StringUtil::HexEncode(input);
    assert(enc1 == enc2);

    std::cout << "PASS" << std::endl;
}

// ========== 边界测试 ==========

void test_large_string_roundtrip() {
    std::cout << "=== test_large_string_roundtrip ===" << std::endl;

    // 100KB 字符串
    std::string large(100 * 1024, 'D');
    for (size_t i = 0; i < large.size(); ++i) {
        large[i] = static_cast<char>('!' + (i % 93));  // 各种可打印字符
    }

    // Base64 往返
    std::string b64_enc = StringUtil::Base64Encode(large);
    std::string b64_dec = StringUtil::Base64Decode(b64_enc);
    assert(b64_dec == large);
    std::cout << "  Base64 100KB roundtrip OK" << std::endl;

    // Base64URL 往返
    std::string b64u_enc = StringUtil::Base64UrlEncode(large);
    std::string b64u_dec = StringUtil::Base64UrlDecode(b64u_enc);
    assert(b64u_dec == large);
    std::cout << "  Base64URL ~100KB roundtrip OK" << std::endl;

    // Hex 往返
    std::string hex_enc = StringUtil::HexEncode(large);
    std::string hex_dec = StringUtil::HexDecode(hex_enc);
    assert(hex_dec == large);
    std::cout << "  Hex 100KB roundtrip OK" << std::endl;

    // URL 编码只测试较小数据（因为有 % 转义，输出会更大）
    std::string url_test = std::string(4096, 'a');
    std::string url_enc = StringUtil::UrlEncode(url_test);
    std::string url_dec = StringUtil::UrlDecode(url_enc);
    assert(url_dec == url_test);
    std::cout << "  URL 4KB roundtrip OK" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_binary_data_roundtrip() {
    std::cout << "=== test_binary_data_roundtrip ===" << std::endl;

    // 构造包含所有字节值 0x00-0xFF 的二进制数据
    std::string all_bytes(256, '\0');
    for (int i = 0; i < 256; ++i) {
        all_bytes[i] = static_cast<char>(i);
    }

    // Base64 → Base64Decode
    std::string b64 = StringUtil::Base64Encode(all_bytes);
    std::string b64_back = StringUtil::Base64Decode(b64);
    assert(b64_back.size() == all_bytes.size());
    assert(b64_back == all_bytes);
    std::cout << "  Base64 all-256-bytes roundtrip OK" << std::endl;

    // Base64URL → Base64UrlDecode
    std::string b64u = StringUtil::Base64UrlEncode(all_bytes);
    std::string b64u_back = StringUtil::Base64UrlDecode(b64u);
    assert(b64u_back.size() == all_bytes.size());
    assert(b64u_back == all_bytes);
    std::cout << "  Base64URL all-256-bytes roundtrip OK" << std::endl;

    // Hex → HexDecode
    std::string hex = StringUtil::HexEncode(all_bytes);
    std::string hex_back = StringUtil::HexDecode(hex);
    assert(hex_back.size() == all_bytes.size());
    assert(hex_back == all_bytes);
    std::cout << "  Hex all-256-bytes roundtrip OK" << std::endl;

    // URL 编码所有字节（使用 UrlEncode 小写版本，正确处理所有 bytes）
    std::string url_enc = StringUtil::UrlEncode(all_bytes, false);  // space as %20
    std::string url_dec = StringUtil::UrlDecode(url_enc, false);
    assert(url_dec.size() == all_bytes.size());
    assert(url_dec == all_bytes);
    std::cout << "  URL all-256-bytes roundtrip OK" << std::endl;

    std::cout << "PASS" << std::endl;
}

// ========== main ==========

int main() {
    std::cout << "=== StringUtil Unit Tests ===" << std::endl;
    std::cout << std::endl;

    // URL 编码/解码
    test_url_encode_decode_roundtrip_basic();
    test_url_encode_decode_roundtrip_various();
    test_url_encode_decode_space_as_plus();
    test_URLEncode_Decode_roundtrip();
    test_url_decode_invalid_input();

    std::cout << std::endl;

    // Base64 编码/解码
    test_base64_roundtrip_basic();
    test_base64_roundtrip_various();
    test_base64_known_vectors();
    test_base64_url_roundtrip_basic();
    test_base64_url_roundtrip_various();
    test_base64_url_decode_compat();
    test_base64_decode_invalid();

    std::cout << std::endl;

    // Hex 编码/解码
    test_hex_roundtrip_basic();
    test_hex_roundtrip_various();
    test_hex_encode_case();
    test_hex_decode_invalid();

    std::cout << std::endl;

    // Split / Join
    test_split_join_roundtrip();
    test_split_skip_empty();
    test_split_char_delimiter();
    test_join_empty();

    std::cout << std::endl;

    // Trim
    test_trim_basic();
    test_trim_left_right();
    test_trim_custom_delimit();

    std::cout << std::endl;

    // Replace
    test_replace_char_char();
    test_replace_char_string();
    test_replace_string_string();

    std::cout << std::endl;

    // Case
    test_to_upper_lower();
    test_equals_ignore_case();

    std::cout << std::endl;

    // Format
    test_format_basic();

    std::cout << std::endl;

    // WString ↔ String
    test_wstring_string_roundtrip();

    std::cout << std::endl;

    // 确定性
    test_url_encode_deterministic();
    test_base64_encode_deterministic();
    test_hex_encode_deterministic();

    std::cout << std::endl;

    // 边界/压力
    test_large_string_roundtrip();
    test_binary_data_roundtrip();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
