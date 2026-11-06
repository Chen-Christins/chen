/**
 * @file test_tls.cc
 * @brief TLS 配置与版本解析测试
 * @author Christins
 * @date 2026-11-06
 */
#include "chen/tls/tls_config.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace chen::tls;

static void test_parse_version()
{
    std::cout << "=== test_parse_version ===" << std::endl;

    TlsVersion version = TlsVersion::Unspecified;

    assert(ParseTlsVersion("", &version));
    assert(version == TlsVersion::Unspecified);

    assert(ParseTlsVersion("TLS1.2", &version));
    assert(version == TlsVersion::TLS1_2);

    assert(ParseTlsVersion("tlsv1.3", &version));
    assert(version == TlsVersion::TLS1_3);

    assert(ParseTlsVersion("tls1_1", &version));
    assert(version == TlsVersion::TLS1_1);

    assert(ParseTlsVersion("1.2", &version));
    assert(version == TlsVersion::TLS1_2);

    assert(!ParseTlsVersion("TLS9.9", &version));
    assert(!ParseTlsVersion("garbage", &version));
    assert(!ParseTlsVersion("TLS1.2", nullptr));

    std::cout << "PASS" << std::endl;
}

static void test_config_validate()
{
    std::cout << "=== test_config_validate ===" << std::endl;

    std::string err;

    TlsConfig client = TlsConfig::DefaultClient();
    assert(client.isValid(&err));
    assert(client.alpn.size() == 1);
    assert(client.alpn[0] == "http/1.1");

    TlsConfig server = TlsConfig::DefaultServer();
    assert(!server.isValid(&err));

    server.cert_file = "/dev/null";
    server.key_file = "/dev/null";
    assert(server.isValid(&err));

    server.min_version = "TLS9.9";
    assert(!server.isValid(&err));
    server.min_version = "TLS1.2";

    server.max_version = "TLS1.1";
    assert(!server.isValid(&err));
    server.max_version.clear();

    server.cert_file = "/not/exist/server.crt";
    assert(!server.isValid(&err));
    server.cert_file = "/dev/null";

    TlsConfig one_way = TlsConfig::DefaultClient();
    one_way.cert_file = "/dev/null";
    assert(!one_way.isValid(&err));

    TlsConfig bad_alpn = TlsConfig::DefaultClient();
    bad_alpn.alpn.push_back(std::string(256, 'x'));
    assert(!bad_alpn.isValid(&err));

    std::cout << "PASS" << std::endl;
}

static void test_config_to_string()
{
    std::cout << "=== test_config_to_string ===" << std::endl;

    TlsConfig conf = TlsConfig::DefaultServer();
    conf.cert_file = "/dev/null";
    conf.key_file = "/dev/null";
    conf.alpn = {"h2", "http/1.1"};

    std::string desc = conf.toString();
    assert(desc.find("enabled=1") != std::string::npos);
    assert(desc.find("server=1") != std::string::npos);
    assert(desc.find("min_version=TLS1.2") != std::string::npos);
    assert(desc.find("h2,http/1.1") != std::string::npos);

    std::cout << "  " << desc << std::endl;
    std::cout << "PASS" << std::endl;
}

int main()
{
    test_parse_version();
    test_config_validate();
    test_config_to_string();

    std::cout << "ALL PASS" << std::endl;
    return 0;
}
