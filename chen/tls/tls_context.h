/**
 * @file tls_context.h
 * @brief TLS 上下文封装
 * @author Christins
 * @date 2026-11-06
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <string>

#include <openssl/ssl.h>

#include "../util/mutex.h"
#include "../util/noncopyable.h"
#include "tls_config.h"

namespace chen::tls {

/**
 * @brief SSL_CTX 的 RAII 封装
 * @details 服务端/客户端分别由一个 TlsContext 持有一份 SSL_CTX。
 *          证书热加载通过 reload() 原子替换内部 SSL_CTX 实现；已建立的连接
 *          由 OpenSSL 自身的引用计数保证旧 SSL_CTX 不会提前释放。
 */
class TlsContext : public Noncopyable {
public:
    typedef std::shared_ptr<TlsContext> ptr;

    /**
     * @brief 创建服务端上下文
     * @param conf TLS 配置，必须提供 cert_file / key_file
     * @return 失败返回 nullptr
     */
    static ptr CreateServer(const TlsConfig& conf);

    /**
     * @brief 创建客户端上下文
     * @param conf TLS 配置
     * @return 失败返回 nullptr
     */
    static ptr CreateClient(const TlsConfig& conf);

    ~TlsContext() override;

    /**
     * @brief 获取裸 SSL_CTX
     * @attention 与 reload() 并发时返回值可能失效；需要跨调用持有时用 getShared()
     */
    SSL_CTX* get() const;

    /**
     * @brief 获取 SSL_CTX 的 shared_ptr，保证调用期间不被 reload 释放
     */
    std::shared_ptr<SSL_CTX> getShared() const;

    /**
     * @brief 获取创建时的配置
     */
    const TlsConfig& getConfig() const { return m_conf; }

    /**
     * @brief 重新读取证书/私钥并原子替换内部 SSL_CTX
     * @param[out] err 失败原因，可为空
     * @return bool 是否成功；失败时保留旧 SSL_CTX
     */
    bool reload(std::string* err = nullptr);

    /**
     * @brief 读取握手协商出的 ALPN 协议
     * @return 未协商时返回空字符串
     */
    static std::string GetAlpn(SSL* ssl);

    /**
     * @brief 是否协商为 HTTP/2
     */
    static bool IsAlpnH2(SSL* ssl);

private:
    explicit TlsContext(const TlsConfig& conf);

    /**
     * @brief 根据配置构造一份新的 SSL_CTX
     */
    std::shared_ptr<SSL_CTX> build(std::string* err);

    std::shared_ptr<SSL_CTX> buildServer(std::string* err);
    std::shared_ptr<SSL_CTX> buildClient(std::string* err);

    bool applyVersions(SSL_CTX* ctx, std::string* err);
    bool applyCiphers(SSL_CTX* ctx, std::string* err);
    bool loadCertificates(SSL_CTX* ctx, std::string* err);

private:
    /// 创建后不再修改，reload 复用同一份配置
    const TlsConfig m_conf;
    /// 保护 m_ctx 的替换与读取（临界区只有指针交换，用 SpinLock）
    mutable SpinLock m_mutex;
    /// 当前生效的 SSL_CTX
    std::shared_ptr<SSL_CTX> m_ctx;
};

} // namespace chen::tls
