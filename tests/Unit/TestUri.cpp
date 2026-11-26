/**
 * @file TestUri.cpp
 * @brief Uri 解析 / 序列化单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "../TestHarness.hpp"

#include "src/Http/Uri.hpp"
#include "src/Socket/Address.hpp"

#include <memory>
#include <string>

using chen::IPAddress;
using chen::Uri;

CHEN_TEST(uri, parse_full)
{
    auto uri = Uri::Create("http://127.0.0.1:8080/test/path?id=100&name=chen#frag");
    CHEN_CHECK(uri != nullptr);

    CHEN_CHECK_EQ(uri->getScheme(), std::string("http"));
    CHEN_CHECK_EQ(uri->getHost(), std::string("127.0.0.1"));
    CHEN_CHECK_EQ(uri->getPort(), 8080);
    CHEN_CHECK_EQ(uri->getPath(), std::string("/test/path"));
    CHEN_CHECK_EQ(uri->getQuery(), std::string("id=100&name=chen"));
    CHEN_CHECK_EQ(uri->getFragment(), std::string("frag"));
    CHEN_CHECK_EQ(uri->getUserinfo(), std::string(""));
}

CHEN_TEST(uri, default_port_and_path)
{
    auto https = Uri::Create("https://example.com");
    CHEN_CHECK(https != nullptr);
    // 未写端口时按 scheme 取默认端口；未写路径时 getPath() 返回 "/"
    CHEN_CHECK_EQ(https->getPort(), 443);
    CHEN_CHECK_EQ(https->getPath(), std::string("/"));
    CHEN_CHECK_EQ(https->toString(), std::string("https://example.com/"));

    auto http = Uri::Create("http://example.com:80/a/b");
    CHEN_CHECK(http != nullptr);
    CHEN_CHECK_EQ(http->getPort(), 80);
    // 默认端口序列化时省略
    CHEN_CHECK_EQ(http->toString(), std::string("http://example.com/a/b"));
}

CHEN_TEST(uri, empty_path_and_query)
{
    auto uri = Uri::Create("http://example.com/");
    CHEN_CHECK(uri != nullptr);
    CHEN_CHECK_EQ(uri->getPath(), std::string("/"));
    CHEN_CHECK_EQ(uri->getQuery(), std::string(""));
    CHEN_CHECK_EQ(uri->getFragment(), std::string(""));
}

CHEN_TEST(uri, userinfo_in_dump)
{
    auto uri = Uri::Create("http://127.0.0.1/x");
    CHEN_CHECK(uri != nullptr);
    uri->setUserinfo("user:pass");
    CHEN_CHECK_EQ(uri->toString(), std::string("http://user:pass@127.0.0.1/x"));
}

CHEN_TEST(uri, dump_round_trip)
{
    auto uri = Uri::Create("http://example.com/x");
    CHEN_CHECK(uri != nullptr);

    uri->setHost("127.0.0.1");
    uri->setPort(9000);
    uri->setPath("/only/path");
    uri->setQuery("k=v");
    uri->setFragment("f");

    CHEN_CHECK_EQ(uri->toString(), std::string("http://127.0.0.1:9000/only/path?k=v#f"));

    // toString 的结果可以被重新解析回来
    auto again = Uri::Create(uri->toString());
    CHEN_CHECK(again != nullptr);
    CHEN_CHECK_EQ(again->getHost(), std::string("127.0.0.1"));
    CHEN_CHECK_EQ(again->getPort(), 9000);
    CHEN_CHECK_EQ(again->getPath(), std::string("/only/path"));
    CHEN_CHECK_EQ(again->getQuery(), std::string("k=v"));
    CHEN_CHECK_EQ(again->getFragment(), std::string("f"));
}

CHEN_TEST(uri, create_address_numeric_host)
{
    // 数字地址不会触发 DNS，适合单测
    auto uri = Uri::Create("http://127.0.0.1:9000/");
    CHEN_CHECK(uri != nullptr);

    auto addr = uri->createAddress();
    CHEN_CHECK(addr != nullptr);
    // createAddress 返回 Address::ptr，端口只有 IPAddress 才有
    auto ip = std::dynamic_pointer_cast<IPAddress>(addr);
    CHEN_CHECK(ip != nullptr);
    CHEN_CHECK_EQ(ip->getPort(), 9000u);
}

CHEN_TEST_MAIN()
