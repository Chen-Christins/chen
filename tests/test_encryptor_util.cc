/**
 * @file test_encryptor_util.cc
 * @brief 测试 EncryptorUtil 加解密、签名验签、哈希等功能的正确性
 * @author Christins
 * @date 2026-06-30
 */
#include "chen/util/encryptor_util.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace chen;

// ========== 辅助函数 ==========

/// 生成指定长度的可读测试字符串
static std::string makeTestString(size_t len) {
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        s += static_cast<char>('A' + (i % 26));
    }
    return s;
}

// ========== AES-GCM 加解密往返测试 ==========

void test_aes128_gcm_roundtrip_basic() {
    std::cout << "=== test_aes128_gcm_roundtrip_basic ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(16);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);
    std::string plaintext = "Hello, World! AES-128-GCM Test.";

    auto enc = EncryptorUtil::EncryptAES128GCM(plaintext, key, iv);
    assert(enc.success);
    assert(!enc.data.empty());
    assert(!enc.tag.empty());
    assert(enc.tag.size() == 16);  // GCM tag is 16 bytes

    auto dec = EncryptorUtil::DecryptAES128GCM(enc.data, enc.tag, key, iv);
    assert(dec.success);
    assert(dec.data == plaintext);

    std::cout << "  plaintext:  " << plaintext << std::endl;
    std::cout << "  decrypted:  " << dec.data << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_aes128_gcm_roundtrip_with_explicit_iv() {
    std::cout << "=== test_aes128_gcm_roundtrip_with_explicit_iv ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(16);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);
    std::string plaintext = "Sensitive data: username=admin, password=secret123";

    // 加密
    auto enc = EncryptorUtil::EncryptAES128GCM(plaintext, key, iv);
    assert(enc.success);
    assert(!enc.data.empty());
    assert(!enc.tag.empty());
    assert(enc.tag.size() == 16);  // GCM tag is 16 bytes

    // 解密（使用相同的 IV）
    auto dec = EncryptorUtil::DecryptAES128GCM(enc.data, enc.tag, key, iv);
    assert(dec.success);
    assert(dec.data == plaintext);

    std::cout << "  plaintext:  " << plaintext << std::endl;
    std::cout << "  decrypted:  " << dec.data << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_aes128_gcm_various_inputs() {
    std::cout << "=== test_aes128_gcm_various_inputs ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(16);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);

    // 测试不同长度的明文
    std::vector<std::string> inputs = {
        "",                                  // 空字符串
        "A",                                 // 单字节
        "Hello",                             // 短字符串
        makeTestString(15),                  // 小于 AES block size
        makeTestString(16),                  // 刚好 AES block size
        makeTestString(17),                  // 略大于 block size
        makeTestString(100),                 // 中等长度
        makeTestString(1024),                // 1KB
        std::string(4096, 'X'),              // 4KB 重复字符
        "你好世界！🌍",                       // UTF-8 多字节字符
        "data\x00with\x00null\x00bytes",     // 包含 null 字节的二进制数据
    };

    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        auto enc = EncryptorUtil::EncryptAES128GCM(input, key, iv);
        assert(enc.success);

        auto dec = EncryptorUtil::DecryptAES128GCM(enc.data, enc.tag, key, iv);
        assert(dec.success);
        assert(dec.data == input);

        std::cout << "  input[" << i << "] len=" << input.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_aes128_gcm_wrong_key_fails() {
    std::cout << "=== test_aes128_gcm_wrong_key_fails ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(16);
    std::string wrong_key = EncryptorUtil::GenerateRandomKey(16);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);
    std::string plaintext = "This should fail with wrong key";

    auto enc = EncryptorUtil::EncryptAES128GCM(plaintext, key, iv);
    assert(enc.success);

    // 使用错误密钥解密应该失败
    auto dec = EncryptorUtil::DecryptAES128GCM(enc.data, enc.tag, wrong_key, iv);
    assert(!dec.success);
    std::cout << "  decryption with wrong key correctly failed: " << dec.error << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_aes128_gcm_wrong_tag_fails() {
    std::cout << "=== test_aes128_gcm_wrong_tag_fails ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(16);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);
    std::string plaintext = "This should fail with wrong tag";

    auto enc = EncryptorUtil::EncryptAES128GCM(plaintext, key, iv);
    assert(enc.success);

    // 篡改认证标签
    std::string bad_tag = enc.tag;
    bad_tag[0] ^= 0xFF;

    auto dec = EncryptorUtil::DecryptAES128GCM(enc.data, bad_tag, key, iv);
    assert(!dec.success);
    std::cout << "  decryption with wrong tag correctly failed: " << dec.error << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_aes128_gcm_wrong_iv_fails() {
    std::cout << "=== test_aes128_gcm_wrong_iv_fails ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(16);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);
    std::string wrong_iv = EncryptorUtil::GenerateRandomIV(12);
    std::string plaintext = "This should fail with wrong IV";

    auto enc = EncryptorUtil::EncryptAES128GCM(plaintext, key, iv);
    assert(enc.success);

    // 使用错误 IV 解密应该失败
    auto dec = EncryptorUtil::DecryptAES128GCM(enc.data, enc.tag, key, wrong_iv);
    assert(!dec.success);
    std::cout << "  decryption with wrong IV correctly failed: " << dec.error << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_aes128_gcm_key_size_validation() {
    std::cout << "=== test_aes128_gcm_key_size_validation ===" << std::endl;

    // 密钥长度不对时应该失败
    std::string bad_key_short = "short";
    std::string bad_key_long = makeTestString(32);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);

    auto enc1 = EncryptorUtil::EncryptAES128GCM("test", bad_key_short, iv);
    assert(!enc1.success);
    std::cout << "  short key rejected: " << enc1.error << std::endl;

    auto enc2 = EncryptorUtil::EncryptAES128GCM("test", bad_key_long, iv);
    assert(!enc2.success);
    std::cout << "  long key rejected: " << enc2.error << std::endl;

    auto dec1 = EncryptorUtil::DecryptAES128GCM("data", "tag16bytes!!!!!!", bad_key_short, iv);
    assert(!dec1.success);
    std::cout << "  decrypt with bad key rejected: " << dec1.error << std::endl;

    std::cout << "PASS" << std::endl;
}

// ========== AES-256-GCM 加解密往返测试 ==========

void test_aes256_gcm_roundtrip_basic() {
    std::cout << "=== test_aes256_gcm_roundtrip_basic ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(32);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);
    std::string plaintext = "Hello, World! AES-256-GCM Test.";

    auto enc = EncryptorUtil::EncryptAES256GCM(plaintext, key, iv);
    assert(enc.success);
    assert(!enc.data.empty());
    assert(!enc.tag.empty());
    assert(enc.tag.size() == 16);

    auto dec = EncryptorUtil::DecryptAES256GCM(enc.data, enc.tag, key, iv);
    assert(dec.success);
    assert(dec.data == plaintext);

    std::cout << "  plaintext:  " << plaintext << std::endl;
    std::cout << "  decrypted:  " << dec.data << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_aes256_gcm_various_inputs() {
    std::cout << "=== test_aes256_gcm_various_inputs ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(32);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);

    std::vector<std::string> inputs = {
        "",
        "X",
        "AES-256 test",
        makeTestString(32),
        makeTestString(256),
        makeTestString(2048),
        std::string(8192, 'Z'),
        "日本語テストデータ",
        std::string("\x00\x01\x02\x03\x04\xFF\xFE\xFD", 8),
    };

    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        auto enc = EncryptorUtil::EncryptAES256GCM(input, key, iv);
        assert(enc.success);

        auto dec = EncryptorUtil::DecryptAES256GCM(enc.data, enc.tag, key, iv);
        assert(dec.success);
        assert(dec.data == input);

        std::cout << "  input[" << i << "] len=" << input.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_aes256_gcm_wrong_key_fails() {
    std::cout << "=== test_aes256_gcm_wrong_key_fails ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(32);
    std::string wrong_key = EncryptorUtil::GenerateRandomKey(32);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);
    std::string plaintext = "AES-256 with wrong key";

    auto enc = EncryptorUtil::EncryptAES256GCM(plaintext, key, iv);
    assert(enc.success);

    auto dec = EncryptorUtil::DecryptAES256GCM(enc.data, enc.tag, wrong_key, iv);
    assert(!dec.success);
    std::cout << "  correctly rejected: " << dec.error << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_aes256_gcm_key_size_validation() {
    std::cout << "=== test_aes256_gcm_key_size_validation ===" << std::endl;

    std::string bad_key = EncryptorUtil::GenerateRandomKey(16);  // 16 bytes, should be 32
    std::string iv = EncryptorUtil::GenerateRandomIV(12);

    auto enc = EncryptorUtil::EncryptAES256GCM("test", bad_key, iv);
    assert(!enc.success);
    std::cout << "  16-byte key rejected for AES-256: " << enc.error << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_aes_gcm_auto_iv() {
    std::cout << "=== test_aes_gcm_auto_iv ===" << std::endl;

    // 测试自动生成 IV 的情况：加密不会失败，且两次加密产生不同密文（IV 随机）
    std::string key = EncryptorUtil::GenerateRandomKey(16);
    std::string plaintext = "Same plaintext, different IVs";

    auto enc1 = EncryptorUtil::EncryptAES128GCM(plaintext, key);
    assert(enc1.success);
    assert(!enc1.data.empty());
    assert(!enc1.tag.empty());

    auto enc2 = EncryptorUtil::EncryptAES128GCM(plaintext, key);
    assert(enc2.success);
    assert(!enc2.data.empty());
    assert(!enc2.tag.empty());

    // 因为 IV 随机，密文应该不同
    assert(enc1.data != enc2.data);

    // 无法用 auto IV 解密（IV 未返回），使用空白 IV 解密会因 IV 长度校验失败
    auto dec = EncryptorUtil::DecryptAES128GCM(enc1.data, enc1.tag, key, "");
    assert(!dec.success);

    std::cout << "  auto-IV: encryption works, different IVs produce different ciphertexts" << std::endl;
    std::cout << "  auto-IV: decrypt without explicit IV correctly rejected" << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== RSA 签名验签往返测试 ==========

void test_rsa_sign_verify_roundtrip() {
    std::cout << "=== test_rsa_sign_verify_roundtrip ===" << std::endl;

    // 生成密钥对
    auto keys = EncryptorUtil::GenerateRSAKeyPair(2048);
    assert(keys.success);
    assert(!keys.private_key.empty());
    assert(!keys.public_key.empty());

    std::cout << "  RSA 2048 key pair generated" << std::endl;

    // 签名
    std::string data = "This is a test message for RSA signing.";
    std::string signature = EncryptorUtil::RS256Sign(data, keys.private_key);
    assert(!signature.empty());
    std::cout << "  signature size: " << signature.size() << " bytes" << std::endl;

    // 验签
    bool valid = EncryptorUtil::RS256Verify(data, signature, keys.public_key);
    assert(valid);
    std::cout << "  signature verified OK" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_rsa_sign_verify_various_data() {
    std::cout << "=== test_rsa_sign_verify_various_data ===" << std::endl;

    auto keys = EncryptorUtil::GenerateRSAKeyPair(2048);
    assert(keys.success);

    std::vector<std::string> messages = {
        "",
        "a",
        "Hello, RSA!",
        makeTestString(100),
        makeTestString(500),
        "JWT payload: {\"sub\":\"123\",\"name\":\"John\"}",
        std::string(1024, 'R'),
    };

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        auto sig = EncryptorUtil::RS256Sign(msg, keys.private_key);
        assert(!sig.empty());

        bool valid = EncryptorUtil::RS256Verify(msg, sig, keys.public_key);
        assert(valid);

        std::cout << "  msg[" << i << "] len=" << msg.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_rsa_wrong_signature_fails() {
    std::cout << "=== test_rsa_wrong_signature_fails ===" << std::endl;

    auto keys = EncryptorUtil::GenerateRSAKeyPair(2048);
    assert(keys.success);

    std::string data = "Original message";
    std::string signature = EncryptorUtil::RS256Sign(data, keys.private_key);
    assert(!signature.empty());

    // 篡改签名
    std::string bad_sig = signature;
    if (!bad_sig.empty()) {
        bad_sig[bad_sig.size() / 2] ^= 0x01;
    }

    bool valid = EncryptorUtil::RS256Verify(data, bad_sig, keys.public_key);
    assert(!valid);
    std::cout << "  tampered signature correctly rejected" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_rsa_wrong_data_fails() {
    std::cout << "=== test_rsa_wrong_data_fails ===" << std::endl;

    auto keys = EncryptorUtil::GenerateRSAKeyPair(2048);
    assert(keys.success);

    std::string data = "Original message";
    std::string signature = EncryptorUtil::RS256Sign(data, keys.private_key);
    assert(!signature.empty());

    // 用错误数据验签
    bool valid = EncryptorUtil::RS256Verify("Tampered message", signature, keys.public_key);
    assert(!valid);
    std::cout << "  tampered data correctly rejected" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_rsa_wrong_key_fails() {
    std::cout << "=== test_rsa_wrong_key_fails ===" << std::endl;

    auto keys1 = EncryptorUtil::GenerateRSAKeyPair(2048);
    auto keys2 = EncryptorUtil::GenerateRSAKeyPair(2048);
    assert(keys1.success && keys2.success);

    std::string data = "Cross-key verification test";
    std::string signature = EncryptorUtil::RS256Sign(data, keys1.private_key);
    assert(!signature.empty());

    // 用另一对密钥的公钥验签
    bool valid = EncryptorUtil::RS256Verify(data, signature, keys2.public_key);
    assert(!valid);
    std::cout << "  wrong public key correctly rejected" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_rsa_key_pair_generation() {
    std::cout << "=== test_rsa_key_pair_generation ===" << std::endl;

    // 默认 2048 位
    auto k1 = EncryptorUtil::GenerateRSAKeyPair();
    assert(k1.success);
    assert(k1.private_key.find("BEGIN RSA PRIVATE KEY") != std::string::npos
           || k1.private_key.find("BEGIN PRIVATE KEY") != std::string::npos);
    assert(k1.public_key.find("BEGIN PUBLIC KEY") != std::string::npos);

    // 4096 位
    auto k2 = EncryptorUtil::GenerateRSAKeyPair(4096);
    assert(k2.success);
    assert(k2.private_key.find("BEGIN") != std::string::npos);
    assert(k2.public_key.find("BEGIN PUBLIC KEY") != std::string::npos);

    // 每次生成的密钥不同
    auto k3 = EncryptorUtil::GenerateRSAKeyPair(2048);
    assert(k3.success);
    assert(k1.private_key != k3.private_key);
    assert(k1.public_key != k3.public_key);

    std::cout << "  2048-bit and 4096-bit keys generated, uniqueness verified" << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== bcrypt 密码哈希往返测试 ==========

void test_bcrypt_hash_verify_roundtrip() {
    std::cout << "=== test_bcrypt_hash_verify_roundtrip ===" << std::endl;

    std::string password = "MySecurePassword123!";

    std::string hash = EncryptorUtil::BCryptHash(password);
    assert(!hash.empty());
    assert(hash.find("$2b$") == 0);  // bcrypt hash 前缀
    std::cout << "  hash: " << hash << std::endl;

    bool valid = EncryptorUtil::BCryptVerify(password, hash);
    assert(valid);
    std::cout << "  password verified OK" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_bcrypt_various_passwords() {
    std::cout << "=== test_bcrypt_various_passwords ===" << std::endl;

    std::vector<std::string> passwords = {
        "",
        "a",
        "password",
        "P@ssw0rd!",
        "CorrectHorseBatteryStaple",
        "中文密码测试",
        "a very long password that exceeds typical length limits for some systems but bcrypt should handle it",
        std::string(72, 'X'),  // bcrypt 最大有效输入长度
        "!@#$%^&*()_+-=[]{}|;':\",./<>?",
    };

    for (size_t i = 0; i < passwords.size(); ++i) {
        const auto& pw = passwords[i];
        std::string hash = EncryptorUtil::BCryptHash(pw);
        assert(!hash.empty());

        bool valid = EncryptorUtil::BCryptVerify(pw, hash);
        assert(valid);

        std::cout << "  pw[" << i << "] len=" << pw.size() << " OK" << std::endl;
    }

    std::cout << "PASS" << std::endl;
}

void test_bcrypt_wrong_password_fails() {
    std::cout << "=== test_bcrypt_wrong_password_fails ===" << std::endl;

    std::string hash = EncryptorUtil::BCryptHash("correct_password");
    assert(!hash.empty());

    bool valid = EncryptorUtil::BCryptVerify("wrong_password", hash);
    assert(!valid);
    std::cout << "  wrong password correctly rejected" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_bcrypt_different_hashes() {
    std::cout << "=== test_bcrypt_different_hashes ===" << std::endl;

    std::string password = "SamePassword";

    // 每次生成的 hash 应该不同（因为 salt 随机）
    std::string h1 = EncryptorUtil::BCryptHash(password);
    std::string h2 = EncryptorUtil::BCryptHash(password);
    assert(!h1.empty() && !h2.empty());
    assert(h1 != h2);
    std::cout << "  h1 != h2 (different salts)" << std::endl;

    // 但都能验证通过
    assert(EncryptorUtil::BCryptVerify(password, h1));
    assert(EncryptorUtil::BCryptVerify(password, h2));
    std::cout << "  both verify OK" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_bcrypt_custom_rounds() {
    std::cout << "=== test_bcrypt_custom_rounds ===" << std::endl;

    std::string password = "RoundsTest123";

    // 使用不同轮数
    std::string h1 = EncryptorUtil::BCryptHash(password, 4);
    assert(!h1.empty());
    assert(h1.find("$2b$04$") == 0);

    std::string h2 = EncryptorUtil::BCryptHash(password, 10);
    assert(!h2.empty());
    assert(h2.find("$2b$10$") == 0);

    // 都能验证
    assert(EncryptorUtil::BCryptVerify(password, h1));
    assert(EncryptorUtil::BCryptVerify(password, h2));

    std::cout << "  rounds=4 and rounds=10 both verify OK" << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== 摘要/Hash 确定性测试 ==========

void test_md5_deterministic() {
    std::cout << "=== test_md5_deterministic ===" << std::endl;

    std::string data = "The quick brown fox jumps over the lazy dog";
    std::string h1 = EncryptorUtil::MD5(data);
    std::string h2 = EncryptorUtil::MD5(data);
    assert(h1 == h2);
    assert(h1.size() == 32);

    // 已知向量的 MD5
    assert(EncryptorUtil::MD5("") == "d41d8cd98f00b204e9800998ecf8427e");
    assert(EncryptorUtil::MD5("hello") == "5d41402abc4b2a76b9719d911017c592");

    std::cout << "  MD5(\"\") = " << EncryptorUtil::MD5("") << std::endl;
    std::cout << "  MD5(\"hello\") = " << EncryptorUtil::MD5("hello") << std::endl;
    std::cout << "  MD5(fox) = " << h1 << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_sha1_deterministic() {
    std::cout << "=== test_sha1_deterministic ===" << std::endl;

    std::string h1 = EncryptorUtil::SHA1("hello");
    std::string h2 = EncryptorUtil::SHA1("hello");
    assert(h1 == h2);
    assert(h1.size() == 40);
    assert(h1 == "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d");

    std::cout << "  SHA1(\"hello\") = " << h1 << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_sha256_deterministic() {
    std::cout << "=== test_sha256_deterministic ===" << std::endl;

    std::string h1 = EncryptorUtil::SHA256("hello");
    std::string h2 = EncryptorUtil::SHA256("hello");
    assert(h1 == h2);
    assert(h1.size() == 64);
    assert(h1 == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");

    // 空字符串
    assert(EncryptorUtil::SHA256("").size() == 64);

    std::cout << "  SHA256(\"hello\") = " << h1 << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_sha512_deterministic() {
    std::cout << "=== test_sha512_deterministic ===" << std::endl;

    std::string h1 = EncryptorUtil::SHA512("hello");
    std::string h2 = EncryptorUtil::SHA512("hello");
    assert(h1 == h2);
    assert(h1.size() == 128);

    std::cout << "  SHA512(\"hello\") = " << h1 << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_sha3_deterministic() {
    std::cout << "=== test_sha3_deterministic ===" << std::endl;

    std::string s256_1 = EncryptorUtil::SHA3_256("hello");
    std::string s256_2 = EncryptorUtil::SHA3_256("hello");
    assert(s256_1 == s256_2);
    assert(!s256_1.empty());

    std::string s512_1 = EncryptorUtil::SHA3_512("hello");
    std::string s512_2 = EncryptorUtil::SHA3_512("hello");
    assert(s512_1 == s512_2);
    assert(!s512_1.empty());

    // 不同算法产生不同结果
    assert(s256_1 != s512_1);

    std::cout << "  SHA3-256(\"hello\") = " << s256_1 << std::endl;
    std::cout << "  SHA3-512(\"hello\") = " << s512_1 << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_hash_raw_data() {
    std::cout << "=== test_hash_raw_data ===" << std::endl;

    const uint8_t data[] = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD};
    size_t len = sizeof(data);

    std::string md5_str = EncryptorUtil::MD5(data, len);
    std::string sha1_str = EncryptorUtil::SHA1(data, len);
    std::string sha256_str = EncryptorUtil::SHA256(data, len);
    std::string sha512_str = EncryptorUtil::SHA512(data, len);
    std::string sha3_256_str = EncryptorUtil::SHA3_256(data, len);
    std::string sha3_512_str = EncryptorUtil::SHA3_512(data, len);

    assert(!md5_str.empty());
    assert(!sha1_str.empty());
    assert(!sha256_str.empty());
    assert(!sha512_str.empty());
    assert(!sha3_256_str.empty());
    assert(!sha3_512_str.empty());

    // 同一个算法，string 版本和 raw data 版本应该产生相同结果（对相同数据）
    std::string same_data(reinterpret_cast<const char*>(data), len);
    assert(EncryptorUtil::MD5(data, len) == EncryptorUtil::MD5(same_data));

    std::cout << "  all raw data hash variants work" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_hash_different_inputs_different_outputs() {
    std::cout << "=== test_hash_different_inputs_different_outputs ===" << std::endl;

    // 不同输入应该产生不同哈希
    std::string h1 = EncryptorUtil::SHA256("hello");
    std::string h2 = EncryptorUtil::SHA256("world");
    assert(h1 != h2);

    // MD5
    std::string m1 = EncryptorUtil::MD5("hello");
    std::string m2 = EncryptorUtil::MD5("world");
    assert(m1 != m2);

    std::cout << "  different inputs produce different hashes" << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== HMAC 确定性测试 ==========

void test_hmac_sha256_deterministic() {
    std::cout << "=== test_hmac_sha256_deterministic ===" << std::endl;

    std::string data = "message";
    std::string key = "secret-key";

    std::string h1 = EncryptorUtil::HMAC_SHA256(data, key);
    std::string h2 = EncryptorUtil::HMAC_SHA256(data, key);
    assert(h1 == h2);
    assert(!h1.empty());
    assert(h1.size() == 32);  // SHA256 produces 32 bytes

    // 不同 key 产生不同 HMAC
    std::string h3 = EncryptorUtil::HMAC_SHA256(data, "different-key");
    assert(h1 != h3);

    // 不同 data 产生不同 HMAC
    std::string h4 = EncryptorUtil::HMAC_SHA256("different-message", key);
    assert(h1 != h4);

    std::cout << "  HMAC-SHA256 deterministic, size=" << h1.size() << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_hmac_sha384_deterministic() {
    std::cout << "=== test_hmac_sha384_deterministic ===" << std::endl;

    std::string data = "message";
    std::string key = "secret-key";

    std::string h1 = EncryptorUtil::HMAC_SHA384(data, key);
    std::string h2 = EncryptorUtil::HMAC_SHA384(data, key);
    assert(h1 == h2);
    assert(!h1.empty());
    assert(h1.size() == 48);  // SHA384 produces 48 bytes

    // 不同算法产生不同长度和不同结果
    std::string hmac256 = EncryptorUtil::HMAC_SHA256(data, key);
    assert(h1.size() != hmac256.size());
    assert(h1 != hmac256);

    std::cout << "  HMAC-SHA384 deterministic, size=" << h1.size() << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_hmac_raw_data() {
    std::cout << "=== test_hmac_raw_data ===" << std::endl;

    const char data[] = "binary data test";
    const char key[] = "binary key test";

    std::string h1 = EncryptorUtil::HMAC_SHA256(data, strlen(data), key, strlen(key));
    std::string h2 = EncryptorUtil::HMAC_SHA256(std::string(data), std::string(key));
    assert(h1 == h2);

    std::string h3 = EncryptorUtil::HMAC_SHA384(data, strlen(data), key, strlen(key));
    std::string h4 = EncryptorUtil::HMAC_SHA384(std::string(data), std::string(key));
    assert(h3 == h4);

    std::cout << "  raw data variants match string variants" << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== 非加密 Hash 测试 ==========

void test_murmur3_deterministic() {
    std::cout << "=== test_murmur3_deterministic ===" << std::endl;

    const char* str = "Hello, MurmurHash3!";
    uint32_t h32_1 = EncryptorUtil::Murmur3_32(str);
    uint32_t h32_2 = EncryptorUtil::Murmur3_32(str);
    assert(h32_1 == h32_2);

    uint64_t h64_1 = EncryptorUtil::Murmur3_64(str);
    uint64_t h64_2 = EncryptorUtil::Murmur3_64(str);
    assert(h64_1 == h64_2);

    // 不同种子产生不同结果
    uint32_t h32_s1 = EncryptorUtil::Murmur3_32(str, 42);
    uint32_t h32_s2 = EncryptorUtil::Murmur3_32(str, 99);
    assert(h32_s1 != h32_s2);

    // 不同字符串产生不同结果
    uint32_t h_a = EncryptorUtil::Murmur3_32("hello");
    uint32_t h_b = EncryptorUtil::Murmur3_32("world");
    assert(h_a != h_b);

    // 二进制版本
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint32_t h_bin = EncryptorUtil::Murmur3_32(data, sizeof(data));
    assert(h_bin > 0);

    std::cout << "  Murmur3_32(\"" << str << "\") = " << h32_1 << std::endl;
    std::cout << "  Murmur3_64(\"" << str << "\") = " << h64_1 << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_quickhash_deterministic() {
    std::cout << "=== test_quickhash_deterministic ===" << std::endl;

    const char* str = "QuickHash test string";
    uint32_t h1 = EncryptorUtil::QuickHash(str);
    uint32_t h2 = EncryptorUtil::QuickHash(str);
    assert(h1 == h2);

    // 不同输入不同哈希
    assert(EncryptorUtil::QuickHash("abc") != EncryptorUtil::QuickHash("abd"));

    // 二进制版本应与字符串版本结果一致
    uint32_t h_bin = EncryptorUtil::QuickHash(str, strlen(str));
    assert(h_bin == h1);

    std::cout << "  QuickHash(\"" << str << "\") = " << h1 << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_xxhash_deterministic() {
    std::cout << "=== test_xxhash_deterministic ===" << std::endl;

    const char* str = "XXHash test data";
    size_t len = strlen(str);

    uint32_t h32_1 = EncryptorUtil::XXHash32(str, len);
    uint32_t h32_2 = EncryptorUtil::XXHash32(str, len);
    assert(h32_1 == h32_2);

    uint64_t h64_1 = EncryptorUtil::XXHash64(str, len);
    uint64_t h64_2 = EncryptorUtil::XXHash64(str, len);
    assert(h64_1 == h64_2);

    // 不同种子
    assert(EncryptorUtil::XXHash32(str, len, 0) != EncryptorUtil::XXHash32(str, len, 12345));
    assert(EncryptorUtil::XXHash64(str, len, 0) != EncryptorUtil::XXHash64(str, len, 99999));

    std::cout << "  XXHash32(\"" << str << "\") = " << h32_1 << std::endl;
    std::cout << "  XXHash64(\"" << str << "\") = " << h64_1 << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== 工具函数测试 ==========

void test_generate_random_key() {
    std::cout << "=== test_generate_random_key ===" << std::endl;

    std::string k1 = EncryptorUtil::GenerateRandomKey(16);
    assert(k1.size() == 16);

    std::string k2 = EncryptorUtil::GenerateRandomKey(32);
    assert(k2.size() == 32);

    // 两次生成应该不同（概率极高）
    std::string k3 = EncryptorUtil::GenerateRandomKey(16);
    assert(k1 != k3);

    std::cout << "  16-byte and 32-byte random keys generated" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_generate_random_iv() {
    std::cout << "=== test_generate_random_iv ===" << std::endl;

    std::string iv1 = EncryptorUtil::GenerateRandomIV(12);
    assert(iv1.size() == 12);

    std::string iv2 = EncryptorUtil::GenerateRandomIV(16);
    assert(iv2.size() == 16);

    // 两次生成应该不同
    std::string iv3 = EncryptorUtil::GenerateRandomIV(12);
    assert(iv1 != iv3);

    std::cout << "  12-byte and 16-byte random IVs generated" << std::endl;
    std::cout << "PASS" << std::endl;
}

void test_validate_key_size() {
    std::cout << "=== test_validate_key_size ===" << std::endl;

    assert(EncryptorUtil::ValidateKeySize(makeTestString(16), 16) == true);
    assert(EncryptorUtil::ValidateKeySize(makeTestString(15), 16) == false);
    assert(EncryptorUtil::ValidateKeySize(makeTestString(17), 16) == false);
    assert(EncryptorUtil::ValidateKeySize(makeTestString(32), 32) == true);
    assert(EncryptorUtil::ValidateKeySize("", 16) == false);

    std::cout << "  key size validation works correctly" << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== 边界和压力测试 ==========

void test_aes_large_data() {
    std::cout << "=== test_aes_large_data ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(32);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);

    // 100KB 数据
    std::string large(100 * 1024, 'D');
    for (size_t i = 0; i < large.size(); ++i) {
        large[i] = static_cast<char>('A' + (i % 62));  // 可打印字符
    }

    auto enc = EncryptorUtil::EncryptAES256GCM(large, key, iv);
    assert(enc.success);
    std::cout << "  encrypted 100KB: " << enc.data.size() << " bytes ciphertext" << std::endl;

    auto dec = EncryptorUtil::DecryptAES256GCM(enc.data, enc.tag, key, iv);
    assert(dec.success);
    assert(dec.data == large);
    std::cout << "  decrypted 100KB: data matches" << std::endl;

    std::cout << "PASS" << std::endl;
}

void test_repeated_encrypt_decrypt() {
    std::cout << "=== test_repeated_encrypt_decrypt ===" << std::endl;

    std::string key = EncryptorUtil::GenerateRandomKey(16);
    std::string iv = EncryptorUtil::GenerateRandomIV(12);

    // 重复加解密 100 次，验证稳定性
    for (int i = 0; i < 100; ++i) {
        std::string msg = "Round " + std::to_string(i) + ": " + makeTestString(50);
        auto enc = EncryptorUtil::EncryptAES128GCM(msg, key, iv);
        assert(enc.success);

        auto dec = EncryptorUtil::DecryptAES128GCM(enc.data, enc.tag, key, iv);
        assert(dec.success);
        assert(dec.data == msg);
    }

    std::cout << "  100 encrypt/decrypt cycles completed successfully" << std::endl;
    std::cout << "PASS" << std::endl;
}

// ========== main ==========

int main() {
    std::cout << "=== EncryptorUtil Unit Tests ===" << std::endl;
    std::cout << std::endl;

    // AES-128-GCM 加解密
    test_aes128_gcm_roundtrip_basic();
    test_aes128_gcm_roundtrip_with_explicit_iv();
    test_aes128_gcm_various_inputs();
    test_aes128_gcm_wrong_key_fails();
    test_aes128_gcm_wrong_tag_fails();
    test_aes128_gcm_wrong_iv_fails();
    test_aes128_gcm_key_size_validation();

    std::cout << std::endl;

    // AES-256-GCM 加解密
    test_aes256_gcm_roundtrip_basic();
    test_aes256_gcm_various_inputs();
    test_aes256_gcm_wrong_key_fails();
    test_aes256_gcm_key_size_validation();
    test_aes_gcm_auto_iv();

    std::cout << std::endl;

    // RSA 签名验签
    test_rsa_sign_verify_roundtrip();
    test_rsa_sign_verify_various_data();
    test_rsa_wrong_signature_fails();
    test_rsa_wrong_data_fails();
    test_rsa_wrong_key_fails();
    test_rsa_key_pair_generation();

    std::cout << std::endl;

    // bcrypt 密码哈希
    test_bcrypt_hash_verify_roundtrip();
    test_bcrypt_various_passwords();
    test_bcrypt_wrong_password_fails();
    test_bcrypt_different_hashes();
    test_bcrypt_custom_rounds();

    std::cout << std::endl;

    // 摘要/Hash 确定性
    test_md5_deterministic();
    test_sha1_deterministic();
    test_sha256_deterministic();
    test_sha512_deterministic();
    test_sha3_deterministic();
    test_hash_raw_data();
    test_hash_different_inputs_different_outputs();

    std::cout << std::endl;

    // HMAC 确定性
    test_hmac_sha256_deterministic();
    test_hmac_sha384_deterministic();
    test_hmac_raw_data();

    std::cout << std::endl;

    // 非加密 Hash
    test_murmur3_deterministic();
    test_quickhash_deterministic();
    test_xxhash_deterministic();

    std::cout << std::endl;

    // 工具函数
    test_generate_random_key();
    test_generate_random_iv();
    test_validate_key_size();

    std::cout << std::endl;

    // 边界/压力
    test_aes_large_data();
    test_repeated_encrypt_decrypt();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
