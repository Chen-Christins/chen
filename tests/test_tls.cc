/**
 * @file test_tls.cc
 * @brief TLS 配置、握手、校验、ALPN 与超时测试
 * @author Christins
 * @date 2026-11-06
 */
#include "chen/iomanager/iomanager.h"
#include "chen/socket/socket.h"
#include "chen/tls/tls_config.h"
#include "chen/util/mutex.h"
#include "chen/util/time_util.h"

#include <cassert>
#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace chen;
using namespace chen::tls;

#ifndef CHEN_TEST_CERT_DIR
#define CHEN_TEST_CERT_DIR "tests/certs"
#endif

static std::string CertPath(const std::string& name)
{
    return std::string(CHEN_TEST_CERT_DIR) + "/" + name;
}

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

    server.cert_file = CertPath("server.crt");
    server.key_file = CertPath("server.key");
    assert(server.isValid(&err));

    server.min_version = "TLS9.9";
    assert(!server.isValid(&err));
    server.min_version = "TLS1.2";

    server.max_version = "TLS1.1";
    assert(!server.isValid(&err));
    server.max_version.clear();

    server.cert_file = "/not/exist/server.crt";
    assert(!server.isValid(&err));
    server.cert_file = CertPath("server.crt");

    TlsConfig one_way = TlsConfig::DefaultClient();
    one_way.cert_file = CertPath("server.crt");
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
    conf.cert_file = CertPath("server.crt");
    conf.key_file = CertPath("server.key");
    conf.alpn = {"h2", "http/1.1"};

    std::string desc = conf.toString();
    assert(desc.find("enabled=1") != std::string::npos);
    assert(desc.find("server=1") != std::string::npos);
    assert(desc.find("min_version=TLS1.2") != std::string::npos);
    assert(desc.find("h2,http/1.1") != std::string::npos);

    std::cout << "  " << desc << std::endl;
    std::cout << "PASS" << std::endl;
}

/**
 * @brief 启动一次性 TLS 服务端
 * @param cert_file 证书
 * @param key_file 私钥
 * @param alpn 服务端 ALPN 列表
 * @param handler accept + 握手成功后的处理逻辑
 * @return 监听端口，失败返回 0
 */
static uint16_t start_server(
    const std::string& cert_file,
    const std::string& key_file,
    const std::vector<std::string>& alpn,
    std::function<void(Socket::ptr)> handler)
{
    auto addr = Address::LookupAnyIPAddress("127.0.0.1");
    if (!addr) {
        return 0;
    }
    addr->setPort(0);

    TlsConfig conf = TlsConfig::DefaultServer();
    conf.cert_file = cert_file;
    conf.key_file = key_file;
    conf.alpn = alpn;
    conf.handshake_timeout_ms = 2000;

    auto listen = SSLSocket::CreateTCP(addr, conf);
    if (!listen) {
        return 0;
    }
    if (!listen->bind(addr) || !listen->listen()) {
        return 0;
    }

    auto local = std::dynamic_pointer_cast<IPAddress>(listen->getLocalAddress());
    if (!local) {
        return 0;
    }
    uint16_t port = local->getPort();

    IOManager::GetThis()->schedule([listen, handler = std::move(handler)]() {
        auto client = std::dynamic_pointer_cast<SSLSocket>(listen->accept());
        if (client && client->handshake(2000)) {
            handler(client);
            client->close();
        }
        listen->close();
    });
    return port;
}

/**
 * @brief 建立 TLS 客户端连接
 * @return 失败返回 nullptr
 */
static SSLSocket::ptr connect_client(
    uint16_t port,
    const std::string& host,
    const std::string& ca_file,
    bool verify_peer,
    const std::vector<std::string>& alpn = {},
    uint64_t handshake_timeout_ms = 2000)
{
    auto addr = Address::LookupAnyIPAddress("127.0.0.1");
    if (!addr) {
        return nullptr;
    }
    addr->setPort(port);

    TlsConfig conf = TlsConfig::DefaultClient();
    conf.ca_file = ca_file;
    conf.verify_peer = verify_peer;
    conf.alpn = alpn;
    conf.handshake_timeout_ms = handshake_timeout_ms;

    auto sock = SSLSocket::CreateTCP(addr, conf);
    if (!sock) {
        return nullptr;
    }
    sock->setServerName(host);
    if (!sock->connect(addr, 2000)) {
        sock->close();
        return nullptr;
    }
    return sock;
}

static bool send_all(Socket::ptr sock, const char* data, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        int rt = sock->send(data + offset, length - offset);
        if (rt <= 0) {
            return false;
        }
        offset += static_cast<size_t>(rt);
    }
    return true;
}

static bool recv_all(Socket::ptr sock, char* data, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        int rt = sock->recv(data + offset, length - offset);
        if (rt <= 0) {
            return false;
        }
        offset += static_cast<size_t>(rt);
    }
    return true;
}

/**
 * @brief 使用指定证书启动服务端，返回客户端是否连接成功
 */
static bool try_connect(
    const std::string& cert_file,
    const std::string& key_file,
    const std::string& host,
    const std::string& ca_file,
    bool verify_peer)
{
    uint16_t port = start_server(cert_file, key_file, {}, [](Socket::ptr client) {
        char buf[1];
        client->recv(buf, sizeof(buf));
    });
    assert(port != 0);

    auto client = connect_client(port, host, ca_file, verify_peer);
    if (!client) {
        return false;
    }
    client->close();
    return true;
}

static void test_roundtrip()
{
    std::cout << "=== test_roundtrip ===" << std::endl;

    uint16_t port = start_server(CertPath("server.crt"), CertPath("server.key"), {}, [](Socket::ptr client) {
        char buf[5] = {0};
        assert(client->recv(buf, sizeof(buf)) == (int)sizeof(buf));
        assert(memcmp(buf, "hello", sizeof(buf)) == 0);
        assert(client->send("world", 5) == 5);
    });
    assert(port != 0);

    auto client = connect_client(port, "localhost", CertPath("ca.crt"), true);
    assert(client);
    assert(client->getHandshakeStatus() == SSLSocket::HandshakeStatus::OK);
    assert(client->isAlpnH2() == false);

    assert(send_all(client, "hello", 5));
    char buf[5] = {0};
    assert(recv_all(client, buf, sizeof(buf)));
    assert(memcmp(buf, "world", sizeof(buf)) == 0);
    client->close();

    std::cout << "PASS" << std::endl;
}

static void test_verify_matrix()
{
    std::cout << "=== test_verify_matrix ===" << std::endl;

    const std::string ca = CertPath("ca.crt");
    const std::string server_cert = CertPath("server.crt");
    const std::string server_key = CertPath("server.key");

    // 正常：CA + 主机名匹配
    assert(try_connect(server_cert, server_key, "localhost", ca, true));

    // 未知 CA
    assert(!try_connect(server_cert, server_key, "localhost", "", true));

    // 主机名不匹配
    assert(!try_connect(server_cert, server_key, "wrong.example.com", ca, true));

    // 证书主机名与访问域名不匹配
    assert(!try_connect(CertPath("wrong-host.crt"), CertPath("wrong-host.key"), "localhost", ca, true));

    // 非受信 CA 签发的证书
    assert(!try_connect(CertPath("unknown-ca.crt"), CertPath("unknown-ca.key"), "localhost", ca, true));

    // 过期证书
    assert(!try_connect(CertPath("expired.crt"), CertPath("expired.key"), "localhost", ca, true));

    // 关闭校验后可以连接自签名证书
    assert(try_connect(CertPath("unknown-ca.crt"), CertPath("unknown-ca.key"), "localhost", "", false));

    std::cout << "PASS" << std::endl;
}

static void test_alpn()
{
    std::cout << "=== test_alpn ===" << std::endl;

    auto wait_close = [](Socket::ptr client) {
        char buf[1];
        client->recv(buf, sizeof(buf));
    };

    uint16_t h2_port = start_server(CertPath("server.crt"), CertPath("server.key"), {"h2", "http/1.1"}, wait_close);
    assert(h2_port != 0);
    auto h2_client = connect_client(h2_port, "localhost", CertPath("ca.crt"), true, {"h2"});
    assert(h2_client);
    assert(h2_client->getAlpnProtocol() == "h2");
    assert(h2_client->isAlpnH2());
    h2_client->close();

    uint16_t h1_port = start_server(CertPath("server.crt"), CertPath("server.key"), {"h2", "http/1.1"}, wait_close);
    assert(h1_port != 0);
    auto h1_client = connect_client(h1_port, "localhost", CertPath("ca.crt"), true, {"http/1.1"});
    assert(h1_client);
    assert(h1_client->getAlpnProtocol() == "http/1.1");
    assert(!h1_client->isAlpnH2());
    h1_client->close();

    std::cout << "PASS" << std::endl;
}

static void test_large_io()
{
    std::cout << "=== test_large_io ===" << std::endl;

    const size_t data_size = 2 * 1024 * 1024;

    uint16_t port = start_server(CertPath("server.crt"), CertPath("server.key"), {}, [data_size](Socket::ptr client) {
        // 延迟读取，让客户端 SSL_write 触发 WANT_WRITE
        usleep(200 * 1000);
        std::string data(data_size, '\0');
        assert(recv_all(client, data.data(), data.size()));

        // 延迟发送，让服务端写缓冲区打满
        assert(send_all(client, data.data(), data.size()));
    });
    assert(port != 0);

    auto client = connect_client(port, "localhost", CertPath("ca.crt"), true);
    assert(client);

    std::string payload(data_size, 'x');
    assert(send_all(client, payload.data(), payload.size()));

    usleep(100 * 1000);
    std::string received(data_size, '\0');
    assert(recv_all(client, received.data(), received.size()));
    assert(received == payload);

    client->close();
    std::cout << "PASS" << std::endl;
}

static void test_handshake_timeout()
{
    std::cout << "=== test_handshake_timeout ===" << std::endl;

    auto addr = Address::LookupAnyIPAddress("127.0.0.1");
    assert(addr);
    addr->setPort(0);

    auto listen = Socket::CreateTCP(addr);
    assert(listen->bind(addr));
    assert(listen->listen());

    auto local = std::dynamic_pointer_cast<IPAddress>(listen->getLocalAddress());
    assert(local);
    uint16_t port = local->getPort();

    // 只 accept 不握手，模拟慢握手 / 恶意连接
    IOManager::GetThis()->schedule([listen]() {
        auto client = listen->accept();
        if (client) {
            usleep(800 * 1000);
            client->close();
        }
        listen->close();
    });

    uint64_t begin_ms = TimeUtil::GetCurrentMs();
    auto client = connect_client(port, "localhost", "", false, {}, 300);
    uint64_t elapsed_ms = TimeUtil::GetCurrentMs() - begin_ms;

    assert(client == nullptr);
    // 客户端握手超时 300ms；服务端 800ms 后才关闭，若耗时接近 800ms 说明超时定时器没生效
    assert(elapsed_ms < 700);

    std::cout << "  elapsed_ms=" << elapsed_ms << std::endl;
    std::cout << "PASS" << std::endl;
}

static void run_network_tests()
{
    test_roundtrip();
    test_verify_matrix();
    test_alpn();
    test_large_io();
    test_handshake_timeout();
}

int main()
{
    // OpenSSL 写已关闭连接可能触发 SIGPIPE；服务端由 Application 统一忽略，测试里也忽略
    signal(SIGPIPE, SIG_IGN);

    test_parse_version();
    test_config_validate();
    test_config_to_string();

    IOManager iom(4, false, "test_tls");
    Semaphore done(0);
    iom.schedule([&done]() {
        run_network_tests();
        done.notify();
    });
    iom.start();
    done.wait();
    iom.stop();

    std::cout << "ALL PASS" << std::endl;
    return 0;
}
