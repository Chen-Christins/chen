/**
 * @file TestStringUtil.cpp
 * @brief StringUtil 编解码与字符串处理单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "../TestHarness.hpp"

#include "src/Util/StringUtil.hpp"

#include <string>
#include <vector>

using chen::StringUtil;

CHEN_TEST(stringutil, split_and_join)
{
    const std::vector<std::string> parts = {"a", "b", "c"};
    CHEN_CHECK_EQ(StringUtil::Join(parts, ","), std::string("a,b,c"));
    CHEN_CHECK_EQ(StringUtil::Join(StringUtil::Split("a,b,c", ','), ","), std::string("a,b,c"));

    // 默认跳过空段
    const auto skip_empty = StringUtil::Split("a,,b,", ',');
    CHEN_CHECK_EQ(skip_empty.size(), 2u);
    CHEN_CHECK_EQ(skip_empty[0], std::string("a"));
    CHEN_CHECK_EQ(skip_empty[1], std::string("b"));

    // 保留空段
    const auto keep_empty = StringUtil::Split("a,,b,", ',', false);
    CHEN_CHECK_EQ(keep_empty.size(), 4u);
    CHEN_CHECK_EQ(keep_empty[1], std::string(""));
    CHEN_CHECK_EQ(keep_empty[3], std::string(""));

    // 多字符分隔符
    const auto multi = StringUtil::Split("x::y::z", "::");
    CHEN_CHECK_EQ(multi.size(), 3u);
    CHEN_CHECK_EQ(multi[2], std::string("z"));
}

CHEN_TEST(stringutil, trim_and_replace)
{
    CHEN_CHECK_EQ(StringUtil::Trim("  \t hello \r\n "), std::string("hello"));
    CHEN_CHECK_EQ(StringUtil::TrimLeft("  hello"), std::string("hello"));
    CHEN_CHECK_EQ(StringUtil::TrimRight("hello  "), std::string("hello"));
    CHEN_CHECK_EQ(StringUtil::Trim(""), std::string(""));

    CHEN_CHECK_EQ(StringUtil::Replace("a-b-c", '-', '+'), std::string("a+b+c"));
    CHEN_CHECK_EQ(StringUtil::Replace("a-b-c", "-", "::"), std::string("a::b::c"));
    CHEN_CHECK_EQ(StringUtil::Replace("aaa", 'a', std::string("bb")), std::string("bbbbbb"));
}

CHEN_TEST(stringutil, case_and_equals)
{
    CHEN_CHECK_EQ(StringUtil::ToUpper("aBc1"), std::string("ABC1"));
    CHEN_CHECK_EQ(StringUtil::ToLower("aBc1"), std::string("abc1"));
    CHEN_CHECK(StringUtil::EqualsIgnoreCase("Hello", "hELLO"));
    CHEN_CHECK(!StringUtil::EqualsIgnoreCase("Hello", "World"));
}

CHEN_TEST(stringutil, url_roundtrip)
{
    const std::vector<std::string> inputs = {
        "",
        "a",
        "hello world",
        "key1=value1&key2=value2",
        "中文测试",
        "/?query=hello world#fragment",
        "100% sure",
        std::string(256, 'x') + " with spaces ",
    };

    for (const auto& input : inputs) {
        // space_as_plus = true 用 '+' 表示空格
        CHEN_CHECK_EQ(StringUtil::UrlDecode(StringUtil::UrlEncode(input, true), true), input);
        // space_as_plus = false 用 %20 表示空格
        CHEN_CHECK_EQ(StringUtil::UrlDecode(StringUtil::UrlEncode(input, false), false), input);
        // URLEncode/URLDecode 大写版本
        CHEN_CHECK_EQ(StringUtil::URLDecode(StringUtil::URLEncode(input)), input);
    }

    CHEN_CHECK_EQ(StringUtil::UrlEncode("hello world", true), std::string("hello+world"));
    // 大写版本固定 space -> '+'（与 UrlEncode(space_as_plus=false) 的 %20 不同）
    CHEN_CHECK_EQ(StringUtil::URLEncode("hello world"), std::string("hello+world"));
    CHEN_CHECK_EQ(StringUtil::URLDecode("hello%20world"), std::string("hello world"));
    CHEN_CHECK_EQ(StringUtil::URLDecode("hello+world"), std::string("hello world"));
}

CHEN_TEST(stringutil, base64_roundtrip)
{
    CHEN_CHECK_EQ(StringUtil::Base64Encode("hello"), std::string("aGVsbG8="));
    CHEN_CHECK_EQ(StringUtil::Base64Decode("aGVsbG8="), std::string("hello"));

    const std::vector<std::string> inputs = {
        "",
        "a",
        "hello world",
        std::string("\x00\x01\x02\xfb\xff", 5),
        "中文测试",
    };

    for (const auto& input : inputs) {
        CHEN_CHECK_EQ(StringUtil::Base64Decode(StringUtil::Base64Encode(input)), input);
        CHEN_CHECK_EQ(StringUtil::Base64UrlDecode(StringUtil::Base64UrlEncode(input)), input);
    }
}

CHEN_TEST(stringutil, base32_roundtrip)
{
    const std::vector<std::string> inputs = {
        "",
        "a",
        "hello",
        "hello world",
        "中文测试",
    };

    for (const auto& input : inputs) {
        CHEN_CHECK_EQ(StringUtil::Base32Decode(StringUtil::Base32Encode(input)), input);
    }
}

CHEN_TEST(stringutil, hex_roundtrip)
{
    CHEN_CHECK_EQ(StringUtil::HexEncode("AB"), std::string("4142"));
    CHEN_CHECK_EQ(StringUtil::HexDecode("4142"), std::string("AB"));

    const std::vector<std::string> inputs = {
        "",
        "A",
        std::string("\x00\x01\xfe\xff", 4),
        "hello world",
    };

    for (const auto& input : inputs) {
        CHEN_CHECK_EQ(StringUtil::HexDecode(StringUtil::HexEncode(input)), input);
        CHEN_CHECK_EQ(StringUtil::HexDecode(StringUtil::HexEncode(input, false)), input);
    }
}

CHEN_TEST(stringutil, format)
{
    CHEN_CHECK_EQ(StringUtil::Format("%d-%s", 42, "x"), std::string("42-x"));
    CHEN_CHECK_EQ(StringUtil::Format("%s", "plain"), std::string("plain"));
}

CHEN_TEST_MAIN()
