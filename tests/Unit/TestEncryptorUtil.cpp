/**
 * @file TestEncryptorUtil.cpp
 * @brief EncryptorUtil 哈希 / HMAC / AES-GCM / PBKDF2 单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @details 只覆盖纯计算路径：已知摘要向量、加解密往返与失败分支。
 *          RSA 密钥生成、bcrypt 等耗时操作不放进单元测试（保留在
 *          tests/TestEncryptorUtil.cc 手工运行）。
 */
#include "../TestHarness.hpp"

#include "src/Util/EncryptorUtil.hpp"

#include <string>

using chen::EncryptorUtil;
using chen::GCMResult;

namespace {

/// HMAC 返回原始二进制，转 hex 后与已知向量比对
std::string ToHex(const std::string& raw)
{
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 0x0f]);
    }
    return out;
}

} // namespace

CHEN_TEST(encryptor, digest_known_vectors)
{
    CHEN_CHECK_EQ(EncryptorUtil::MD5("abc"), std::string("900150983cd24fb0d6963f7d28e17f72"));
    CHEN_CHECK_EQ(EncryptorUtil::SHA1("abc"), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    CHEN_CHECK_EQ(
        EncryptorUtil::SHA256("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHEN_CHECK_EQ(EncryptorUtil::SHA512("abc"),
        std::string("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                    "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));

    // Raw 版本返回二进制摘要，长度固定
    CHEN_CHECK_EQ(EncryptorUtil::MD5Raw("abc").size(), 16u);
    CHEN_CHECK_EQ(EncryptorUtil::SHA256Raw("abc").size(), 32u);
    CHEN_CHECK_EQ(EncryptorUtil::SHA512Raw("abc").size(), 64u);

    // 空串也有确定摘要
    CHEN_CHECK_EQ(EncryptorUtil::MD5(""), std::string("d41d8cd98f00b204e9800998ecf8427e"));
}

CHEN_TEST(encryptor, hmac_known_vector)
{
    const std::string msg = "The quick brown fox jumps over the lazy dog";
    // HMAC_SHA256 返回原始二进制摘要
    CHEN_CHECK_EQ(EncryptorUtil::HMAC_SHA256(msg, "key").size(), 32u);
    CHEN_CHECK_EQ(ToHex(EncryptorUtil::HMAC_SHA256(msg, "key")),
        std::string("f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"));
    // 同长不同 key 必须产生不同签名（防实现退化为“忽略 key”）
    CHEN_CHECK(EncryptorUtil::HMAC_SHA256(msg, "key") != EncryptorUtil::HMAC_SHA256(msg, "kez"));
}

CHEN_TEST(encryptor, aes128_gcm_round_trip)
{
    const std::string key = EncryptorUtil::GenerateRandomKey(16);
    const std::string iv = EncryptorUtil::GenerateRandomIV(12);
    const std::string plaintext = "hello aes-128-gcm";

    CHEN_CHECK_EQ(key.size(), 16u);
    CHEN_CHECK_EQ(iv.size(), 12u);
    CHEN_CHECK(EncryptorUtil::ValidateKeySize(key, 16));

    GCMResult enc = EncryptorUtil::EncryptAES128GCM(plaintext, key, iv);
    CHEN_CHECK(enc.success);
    CHEN_CHECK(!enc.data.empty());
    CHEN_CHECK_EQ(enc.tag.size(), 16u);

    GCMResult dec = EncryptorUtil::DecryptAES128GCM(enc.data, enc.tag, key, iv);
    CHEN_CHECK(dec.success);
    CHEN_CHECK_EQ(dec.data, plaintext);

    // 篡改 tag 必须认证失败
    std::string bad_tag = enc.tag;
    bad_tag[0] = static_cast<char>(bad_tag[0] ^ 0xff);
    GCMResult tampered = EncryptorUtil::DecryptAES128GCM(enc.data, bad_tag, key, iv);
    CHEN_CHECK(!tampered.success);

    // 错误 key 长度必须被拒绝
    GCMResult bad_key = EncryptorUtil::EncryptAES128GCM(plaintext, "short", iv);
    CHEN_CHECK(!bad_key.success);
}

CHEN_TEST(encryptor, aes256_gcm_round_trip)
{
    const std::string key = EncryptorUtil::GenerateRandomKey(32);
    const std::string iv = EncryptorUtil::GenerateRandomIV(12);
    const std::string plaintext = std::string("binary\0payload", 14);

    CHEN_CHECK_EQ(key.size(), 32u);
    GCMResult enc = EncryptorUtil::EncryptAES256GCM(plaintext, key, iv);
    CHEN_CHECK(enc.success);

    GCMResult dec = EncryptorUtil::DecryptAES256GCM(enc.data, enc.tag, key, iv);
    CHEN_CHECK(dec.success);
    CHEN_CHECK_EQ(dec.data, plaintext);
}

CHEN_TEST(encryptor, random_key_unique)
{
    const std::string a = EncryptorUtil::GenerateRandomKey(32);
    const std::string b = EncryptorUtil::GenerateRandomKey(32);
    CHEN_CHECK_EQ(a.size(), 32u);
    CHEN_CHECK_EQ(b.size(), 32u);
    CHEN_CHECK(a != b);
}

CHEN_TEST(encryptor, pbkdf2_deterministic)
{
    const std::string d1 = EncryptorUtil::PBKDF2_SHA256("password", "salt", 1000, 32);
    const std::string d2 = EncryptorUtil::PBKDF2_SHA256("password", "salt", 1000, 32);
    const std::string d3 = EncryptorUtil::PBKDF2_SHA256("password", "other-salt", 1000, 32);

    CHEN_CHECK_EQ(d1.size(), 32u);
    CHEN_CHECK_EQ(d1, d2);
    CHEN_CHECK(d1 != d3);
    CHEN_CHECK_EQ(EncryptorUtil::PBKDF2_SHA256("password", "salt", 1000, 16).size(), 16u);
}

CHEN_TEST(encryptor, secure_compare)
{
    CHEN_CHECK(EncryptorUtil::SecureCompare("abc", "abc"));
    CHEN_CHECK(!EncryptorUtil::SecureCompare("abc", "abd"));
    CHEN_CHECK(!EncryptorUtil::SecureCompare("abc", "abcd"));
    CHEN_CHECK(EncryptorUtil::SecureCompare("", ""));
}

CHEN_TEST_MAIN()
