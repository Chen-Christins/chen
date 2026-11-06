/**
 * @file tls_context.cc
 * @brief TLS 上下文实现
 * @author Christins
 * @date 2026-11-06
 * @copyright GPL-3.0
 */
#include "tls_context.h"

#include <cstring>

#include <openssl/err.h>
#include <openssl/x509v3.h>

#include "../log/log.h"

namespace chen::tls {

static Logger::ptr logger = LOG_NAME("system");

namespace {

/**
 * @brief 取最近一次 OpenSSL 错误，失败返回 unknown
 */
std::string LastSslError()
{
    unsigned long code = ERR_peek_last_error();
    if (code == 0) {
        return "unknown error";
    }
    char buf[256];
    ERR_error_string_n(code, buf, sizeof(buf));
    return buf;
}

void SetError(std::string* err, const std::string& msg)
{
    if (err) {
        *err = msg;
    }
}

bool Fail(std::string* err, const std::string& action)
{
    SetError(err, action + ": " + LastSslError());
    return false;
}

/**
 * @brief 释放挂在 SSL_CTX ex_data 上的 ALPN 配置
 */
void AlpnConfigFree(void* parent, void* ptr, CRYPTO_EX_DATA* ad, int idx, long argl, void* argp)
{
    (void)parent;
    (void)ad;
    (void)idx;
    (void)argl;
    (void)argp;
    delete static_cast<std::vector<std::string>*>(ptr);
}

/**
 * @brief ALPN 配置在 SSL_CTX 上的 ex_data 索引
 */
int AlpnConfigIndex()
{
    static int index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, AlpnConfigFree);
    return index;
}

/**
 * @brief 服务端 ALPN 选择回调
 * @details 客户端列表格式为：1 字节长度 + 协议字节 + ...
 *          按服务端配置顺序取第一个双方都支持的协议；没有交集时返回 NOACK，
 *          让握手继续完成（由 HTTP 层决定是否拒绝该连接）。
 *          ALPN 配置挂在 SSL_CTX 的 ex_data 上，不依赖 TlsContext 的生命周期。
 */
int AlpnSelectCallback(SSL* ssl, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void* arg)
{
    (void)arg;
    auto* protocols = static_cast<const std::vector<std::string>*>(SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), AlpnConfigIndex()));
    if (!protocols || protocols->empty()) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    const unsigned char* p = in;
    unsigned int remaining = inlen;
    while (remaining > 0) {
        unsigned int len = *p++;
        --remaining;
        if (len == 0 || len > remaining) {
            return SSL_TLSEXT_ERR_NOACK;
        }
        for (const auto& proto : *protocols) {
            if (proto.size() == len && memcmp(proto.data(), p, len) == 0) {
                *out = p;
                *outlen = static_cast<unsigned char>(len);
                return SSL_TLSEXT_ERR_OK;
            }
        }
        p += len;
        remaining -= len;
    }
    return SSL_TLSEXT_ERR_NOACK;
}

} // namespace

TlsContext::TlsContext(const TlsConfig& conf)
    : m_conf(conf)
{
}

TlsContext::~TlsContext() = default;

TlsContext::ptr TlsContext::CreateServer(const TlsConfig& conf)
{
    TlsConfig server_conf = conf;
    server_conf.enabled = true;
    server_conf.server = true;

    std::string err;
    if (!server_conf.isValid(&err)) {
        ERROR(logger) << "create tls server context failed: " << err << " conf=" << server_conf.toString();
        return nullptr;
    }

    ptr ctx(new TlsContext(server_conf));
    if (!ctx->reload(&err)) {
        ERROR(logger) << "create tls server context failed: " << err << " conf=" << server_conf.toString();
        return nullptr;
    }
    return ctx;
}

TlsContext::ptr TlsContext::CreateClient(const TlsConfig& conf)
{
    TlsConfig client_conf = conf;
    client_conf.enabled = true;
    client_conf.server = false;

    std::string err;
    if (!client_conf.isValid(&err)) {
        ERROR(logger) << "create tls client context failed: " << err << " conf=" << client_conf.toString();
        return nullptr;
    }

    ptr ctx(new TlsContext(client_conf));
    if (!ctx->reload(&err)) {
        ERROR(logger) << "create tls client context failed: " << err << " conf=" << client_conf.toString();
        return nullptr;
    }
    return ctx;
}

SSL_CTX* TlsContext::get() const
{
    SpinLock::Lock lock(m_mutex);
    return m_ctx.get();
}

std::shared_ptr<SSL_CTX> TlsContext::getShared() const
{
    SpinLock::Lock lock(m_mutex);
    return m_ctx;
}

bool TlsContext::reload(std::string* err)
{
    auto ctx = build(err);
    if (!ctx) {
        return false;
    }

    SpinLock::Lock lock(m_mutex);
    m_ctx.swap(ctx);
    // ctx 离开作用域时释放旧 SSL_CTX；已建立的 SSL 仍持有旧 ctx 的引用
    return true;
}

std::shared_ptr<SSL_CTX> TlsContext::build(std::string* err)
{
    ERR_clear_error();
    return m_conf.server ? buildServer(err) : buildClient(err);
}

std::shared_ptr<SSL_CTX> TlsContext::buildServer(std::string* err)
{
    SSL_CTX* raw = SSL_CTX_new(TLS_server_method());
    if (!raw) {
        SetError(err, "SSL_CTX_new(TLS_server_method) failed: " + LastSslError());
        return nullptr;
    }
    std::shared_ptr<SSL_CTX> ctx(raw, SSL_CTX_free);

    long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
#ifdef SSL_OP_NO_RENEGOTIATION
    options |= SSL_OP_NO_RENEGOTIATION;
#endif
    SSL_CTX_set_options(ctx.get(), options);
    SSL_CTX_set_mode(ctx.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_RELEASE_BUFFERS);

    if (!applyVersions(ctx.get(), err) || !applyCiphers(ctx.get(), err)) {
        return nullptr;
    }

    if (m_conf.prefer_server_ciphers) {
        SSL_CTX_set_options(ctx.get(), SSL_OP_CIPHER_SERVER_PREFERENCE);
    }
    if (!m_conf.session_tickets) {
        SSL_CTX_set_options(ctx.get(), SSL_OP_NO_TICKET);
    }

    if (!loadCertificates(ctx.get(), err)) {
        return nullptr;
    }

    if (m_conf.verify_client) {
        int mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        SSL_CTX_set_verify(ctx.get(), mode, nullptr);
        SSL_CTX_set_verify_depth(ctx.get(), m_conf.verify_depth);
        if (!m_conf.ca_file.empty() || !m_conf.ca_path.empty()) {
            const char* ca_file = m_conf.ca_file.empty() ? nullptr : m_conf.ca_file.c_str();
            const char* ca_path = m_conf.ca_path.empty() ? nullptr : m_conf.ca_path.c_str();
            ERR_clear_error();
            if (SSL_CTX_load_verify_locations(ctx.get(), ca_file, ca_path) != 1) {
                Fail(err, "SSL_CTX_load_verify_locations for client CA");
                return nullptr;
            }
            if (!m_conf.ca_file.empty()) {
                STACK_OF(X509_NAME)* ca_names = SSL_load_client_CA_file(m_conf.ca_file.c_str());
                if (ca_names) {
                    SSL_CTX_set_client_CA_list(ctx.get(), ca_names);
                }
            }
        } else if (SSL_CTX_set_default_verify_paths(ctx.get()) != 1) {
            WARN(logger) << "SSL_CTX_set_default_verify_paths failed, mTLS 可能无法校验客户端证书: " << LastSslError();
        }
    }

    if (!m_conf.alpn.empty()) {
        int index = AlpnConfigIndex();
        if (index < 0) {
            SetError(err, "SSL_CTX_get_ex_new_index failed: " + LastSslError());
            return nullptr;
        }
        auto* protocols = new std::vector<std::string>(m_conf.alpn);
        if (SSL_CTX_set_ex_data(ctx.get(), index, protocols) != 1) {
            delete protocols;
            SetError(err, "SSL_CTX_set_ex_data(alpn) failed: " + LastSslError());
            return nullptr;
        }
        SSL_CTX_set_alpn_select_cb(ctx.get(), AlpnSelectCallback, nullptr);
    }

    return ctx;
}

std::shared_ptr<SSL_CTX> TlsContext::buildClient(std::string* err)
{
    SSL_CTX* raw = SSL_CTX_new(TLS_client_method());
    if (!raw) {
        SetError(err, "SSL_CTX_new(TLS_client_method) failed: " + LastSslError());
        return nullptr;
    }
    std::shared_ptr<SSL_CTX> ctx(raw, SSL_CTX_free);

    long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
#ifdef SSL_OP_NO_RENEGOTIATION
    options |= SSL_OP_NO_RENEGOTIATION;
#endif
    SSL_CTX_set_options(ctx.get(), options);
    SSL_CTX_set_mode(ctx.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_RELEASE_BUFFERS);

    if (!applyVersions(ctx.get(), err) || !applyCiphers(ctx.get(), err)) {
        return nullptr;
    }

    if (m_conf.verify_peer) {
        SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_verify_depth(ctx.get(), m_conf.verify_depth);
        if (!m_conf.ca_file.empty() || !m_conf.ca_path.empty()) {
            const char* ca_file = m_conf.ca_file.empty() ? nullptr : m_conf.ca_file.c_str();
            const char* ca_path = m_conf.ca_path.empty() ? nullptr : m_conf.ca_path.c_str();
            ERR_clear_error();
            if (SSL_CTX_load_verify_locations(ctx.get(), ca_file, ca_path) != 1) {
                SetError(err, "SSL_CTX_load_verify_locations failed: " + LastSslError());
                return nullptr;
            }
        } else if (SSL_CTX_set_default_verify_paths(ctx.get()) != 1) {
            // 系统 CA 路径缺失时仍允许继续，握手阶段会以 verify 失败体现
            WARN(logger) << "SSL_CTX_set_default_verify_paths failed, 可用 ca_file 或 SSL_CERT_FILE/SSL_CERT_DIR 指定 CA";
        }
    } else {
        SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    }

    // 客户端证书（双向 TLS）为可选能力
    if (!m_conf.cert_file.empty() || !m_conf.key_file.empty()) {
        if (!loadCertificates(ctx.get(), err)) {
            return nullptr;
        }
    }

    return ctx;
}

bool TlsContext::applyVersions(SSL_CTX* ctx, std::string* err)
{
    TlsVersion min_version = TlsVersion::Unspecified;
    TlsVersion max_version = TlsVersion::Unspecified;
    if (!ParseTlsVersion(m_conf.min_version, &min_version)) {
        SetError(err, "invalid min_version: " + m_conf.min_version);
        return false;
    }
    if (!ParseTlsVersion(m_conf.max_version, &max_version)) {
        SetError(err, "invalid max_version: " + m_conf.max_version);
        return false;
    }

    ERR_clear_error();
    if (min_version != TlsVersion::Unspecified && SSL_CTX_set_min_proto_version(ctx, static_cast<int>(min_version)) != 1) {
        return Fail(err, "SSL_CTX_set_min_proto_version(" + m_conf.min_version + ")");
    }
    ERR_clear_error();
    if (max_version != TlsVersion::Unspecified && SSL_CTX_set_max_proto_version(ctx, static_cast<int>(max_version)) != 1) {
        return Fail(err, "SSL_CTX_set_max_proto_version(" + m_conf.max_version + ")");
    }
    return true;
}

bool TlsContext::applyCiphers(SSL_CTX* ctx, std::string* err)
{
    if (!m_conf.ciphers.empty()) {
        ERR_clear_error();
        if (SSL_CTX_set_cipher_list(ctx, m_conf.ciphers.c_str()) != 1) {
            return Fail(err, "SSL_CTX_set_cipher_list(" + m_conf.ciphers + ")");
        }
    }

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    if (!m_conf.ciphersuites.empty()) {
        ERR_clear_error();
        if (SSL_CTX_set_ciphersuites(ctx, m_conf.ciphersuites.c_str()) != 1) {
            return Fail(err, "SSL_CTX_set_ciphersuites(" + m_conf.ciphersuites + ")");
        }
    }
#else
    if (!m_conf.ciphersuites.empty()) {
        SetError(err, "ciphersuites requires OpenSSL 1.1.1 or later");
        return false;
    }
#endif
    return true;
}

bool TlsContext::loadCertificates(SSL_CTX* ctx, std::string* err)
{
    if (m_conf.cert_file.empty() || m_conf.key_file.empty()) {
        SetError(err, "cert_file and key_file are required");
        return false;
    }

    ERR_clear_error();
    if (SSL_CTX_use_certificate_chain_file(ctx, m_conf.cert_file.c_str()) != 1) {
        return Fail(err, "SSL_CTX_use_certificate_chain_file(" + m_conf.cert_file + ")");
    }
    ERR_clear_error();
    if (SSL_CTX_use_PrivateKey_file(ctx, m_conf.key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        return Fail(err, "SSL_CTX_use_PrivateKey_file(" + m_conf.key_file + ")");
    }
    if (SSL_CTX_check_private_key(ctx) != 1) {
        return Fail(err, "SSL_CTX_check_private_key(cert=" + m_conf.cert_file + ", key=" + m_conf.key_file + ")");
    }
    return true;
}

std::string TlsContext::GetAlpn(SSL* ssl)
{
    if (!ssl) {
        return "";
    }
    const unsigned char* data = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(ssl, &data, &len);
    if (!data || len == 0) {
        return "";
    }
    return std::string(reinterpret_cast<const char*>(data), len);
}

bool TlsContext::IsAlpnH2(SSL* ssl)
{
    return GetAlpn(ssl) == "h2";
}

} // namespace chen::tls
