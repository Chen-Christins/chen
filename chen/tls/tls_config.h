/**
 * @file tls_config.h
 * @brief TLS 配置结构
 * @author Christins
 * @date 2026-11-06
 * @copyright GPL-3.0
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chen::tls {

/**
 * @brief TLS 协议版本
 * @details 数值与 OpenSSL 的 TLS1_x_VERSION 保持一致，解析后可直接传给
 *          SSL_CTX_set_min_proto_version / SSL_CTX_set_max_proto_version。
 */
enum class TlsVersion : int {
    Unspecified = 0,
    TLS1_0 = 0x0301,
    TLS1_1 = 0x0302,
    TLS1_2 = 0x0303,
    TLS1_3 = 0x0304,
};

/**
 * @brief 解析 TLS 版本字符串
 * @param[in] v 版本字符串，支持 TLS1.0 / TLS1.1 / TLS1.2 / TLS1.3（大小写不敏感）
 * @param[out] out 解析结果，空字符串对应 Unspecified
 * @return bool 是否解析成功
 */
bool ParseTlsVersion(const std::string& v, TlsVersion* out);

/**
 * @brief SNI 备用证书配置
 * @details M1 只保留结构，实际 SNI 多证书选择在 M2 实现。
 */
struct SniCert {
    /// 精确域名或通配符域名（如 *.example.com）
    std::string host;
    /// 证书链文件
    std::string cert_file;
    /// 私钥文件
    std::string key_file;
};

/**
 * @brief TLS 配置
 * @details 服务端/客户端共用同一份结构，由 TlsContext 负责翻译成 OpenSSL 调用。
 *          TcpServerConf 只做 YAML 映射，不直接持有 OpenSSL 对象。
 */
struct TlsConfig {
    /// 是否启用 TLS
    bool enabled = false;
    /// true=服务端，false=客户端
    bool server = true;

    /// 服务端：证书链文件；客户端：可选的客户端证书
    std::string cert_file;
    /// 服务端：私钥文件；客户端：可选的客户端私钥
    std::string key_file;
    /// CA 文件，用于客户端校验服务端 / 服务端校验客户端（mTLS）
    std::string ca_file;
    /// CA 目录，配合 ca_file 使用
    std::string ca_path;
    /// OpenSSL cipher list（TLS 1.2 及以下）
    std::string ciphers;
    /// OpenSSL ciphersuites（TLS 1.3），OpenSSL 1.1.1+
    std::string ciphersuites;

    /// 最低 TLS 版本，空表示不限制；支持 TLS1.0 / TLS1.1 / TLS1.2 / TLS1.3
    std::string min_version = "TLS1.2";
    /// 最高 TLS 版本，空表示不限制
    std::string max_version;

    /// 客户端：是否校验服务端证书链与主机名
    bool verify_peer = true;
    /// 服务端：是否要求客户端证书（mTLS），M2 接入配置
    bool verify_client = false;
    /// 证书链最大校验深度
    int verify_depth = 4;

    /// ALPN 协议列表；为空时由调用方决定默认值（HTTP 默认 h2/http1.1）
    std::vector<std::string> alpn;

    /// 服务端：SNI 备用证书，M2 使用
    std::vector<SniCert> sni;

    /// 服务端：优先使用服务端 cipher 顺序
    bool prefer_server_ciphers = true;
    /// 服务端：是否启用 session ticket
    bool session_tickets = true;
    /// 是否允许证书热加载，M2 使用
    bool hot_reload = true;
    /// 握手超时，单位毫秒；0 表示使用默认值 10000
    uint64_t handshake_timeout_ms = 10000;

    /**
     * @brief 服务端默认配置
     */
    static TlsConfig DefaultServer();

    /**
     * @brief 客户端默认配置
     */
    static TlsConfig DefaultClient();

    /**
     * @brief 校验配置是否自洽
     * @param[out] err 失败原因，可为空
     * @return bool 是否合法
     */
    bool isValid(std::string* err = nullptr) const;

    /**
     * @brief 生成用于日志的配置描述（不包含私钥内容）
     */
    std::string toString() const;
};

} // namespace chen::tls
