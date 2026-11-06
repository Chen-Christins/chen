/**
 * @file tls_config.cc
 * @brief TLS 配置实现
 * @author Christins
 * @date 2026-11-06
 * @copyright GPL-3.0
 */
#include "tls_config.h"

#include <sstream>

#include "../util/fs_util.h"
#include "../util/string_util.h"

namespace chen::tls {

namespace {

void SetError(std::string* err, const std::string& msg)
{
    if (err) {
        *err = msg;
    }
}

} // namespace

bool ParseTlsVersion(const std::string& v, TlsVersion* out)
{
    if (!out) {
        return false;
    }
    std::string s = StringUtil::ToLower(StringUtil::Trim(v));
    if (s.empty()) {
        *out = TlsVersion::Unspecified;
        return true;
    }

    // 统一成 tls1.x 形式，兼容 tlsv1.2 / tls1_2 / 1.2
    if (s.rfind("tlsv", 0) == 0) {
        s = "tls" + s.substr(4);
    } else if (s.rfind("tls", 0) != 0) {
        s = "tls" + s;
    }
    s = StringUtil::Replace(s, '_', '.');

    if (s == "tls1" || s == "tls1.0") {
        *out = TlsVersion::TLS1_0;
        return true;
    }
    if (s == "tls1.1") {
        *out = TlsVersion::TLS1_1;
        return true;
    }
    if (s == "tls1.2") {
        *out = TlsVersion::TLS1_2;
        return true;
    }
    if (s == "tls1.3") {
        *out = TlsVersion::TLS1_3;
        return true;
    }
    return false;
}

TlsConfig TlsConfig::DefaultServer()
{
    TlsConfig conf;
    conf.enabled = true;
    conf.server = true;
    return conf;
}

TlsConfig TlsConfig::DefaultClient()
{
    TlsConfig conf;
    conf.enabled = true;
    conf.server = false;
    conf.verify_peer = true;
    // 普通 HTTPS 客户端默认只声明 HTTP/1.1，避免误协商到 h2 后仍按 HTTP/1.1 发请求。
    conf.alpn = {"http/1.1"};
    return conf;
}

bool TlsConfig::isValid(std::string* err) const
{
    if (!enabled) {
        return true;
    }

    TlsVersion min_version_value = TlsVersion::Unspecified;
    TlsVersion max_version_value = TlsVersion::Unspecified;
    if (!ParseTlsVersion(min_version, &min_version_value)) {
        SetError(err, "invalid min_version: " + min_version);
        return false;
    }
    if (!ParseTlsVersion(max_version, &max_version_value)) {
        SetError(err, "invalid max_version: " + max_version);
        return false;
    }
    if (min_version_value != TlsVersion::Unspecified && max_version_value != TlsVersion::Unspecified
        && static_cast<int>(min_version_value) > static_cast<int>(max_version_value)) {
        SetError(err, "min_version is greater than max_version");
        return false;
    }

    if (server && (cert_file.empty() || key_file.empty())) {
        SetError(err, "server tls requires cert_file and key_file");
        return false;
    }
    if (!server && (cert_file.empty() != key_file.empty())) {
        SetError(err, "client certificate requires both cert_file and key_file");
        return false;
    }

    if (verify_depth < 0) {
        SetError(err, "verify_depth must be >= 0");
        return false;
    }

    // 证书/CA 路径用框架 FSUtil 校验，避免把明显错误的配置带到握手阶段
    if (!cert_file.empty() && !FSUtil::Exists(cert_file)) {
        SetError(err, "cert_file not found: " + cert_file);
        return false;
    }
    if (!key_file.empty() && !FSUtil::Exists(key_file)) {
        SetError(err, "key_file not found: " + key_file);
        return false;
    }
    if (!ca_file.empty() && !FSUtil::Exists(ca_file)) {
        SetError(err, "ca_file not found: " + ca_file);
        return false;
    }
    if (!ca_path.empty() && !FSUtil::IsDirectory(ca_path)) {
        SetError(err, "ca_path is not a directory: " + ca_path);
        return false;
    }

    for (const auto& proto : alpn) {
        if (proto.empty()) {
            SetError(err, "alpn protocol must not be empty");
            return false;
        }
        if (proto.size() > 255) {
            SetError(err, "alpn protocol is too long: " + proto);
            return false;
        }
    }

    return true;
}

std::string TlsConfig::toString() const
{
    std::ostringstream oss;
    oss << "{enabled=" << (enabled ? 1 : 0) << " server=" << (server ? 1 : 0);
    if (!cert_file.empty()) {
        oss << " cert_file=" << cert_file;
    }
    if (!key_file.empty()) {
        oss << " key_file=" << key_file;
    }
    if (!ca_file.empty()) {
        oss << " ca_file=" << ca_file;
    }
    if (!ca_path.empty()) {
        oss << " ca_path=" << ca_path;
    }
    oss << " min_version=" << (min_version.empty() ? "-" : min_version) << " max_version=" << (max_version.empty() ? "-" : max_version)
        << " verify_peer=" << (verify_peer ? 1 : 0) << " verify_client=" << (verify_client ? 1 : 0) << " verify_depth=" << verify_depth
        << " handshake_timeout_ms=" << handshake_timeout_ms;

    oss << " alpn=[";
    for (size_t i = 0; i < alpn.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << alpn[i];
    }
    oss << "]";

    if (!sni.empty()) {
        oss << " sni=[";
        for (size_t i = 0; i < sni.size(); ++i) {
            if (i > 0) {
                oss << ",";
            }
            oss << sni[i].host;
        }
        oss << "]";
    }
    oss << "}";
    return oss.str();
}

} // namespace chen::tls
