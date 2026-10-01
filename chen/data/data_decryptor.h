/**
 * @file data_decryptor.h
 * @brief 数据解密模块（仅解密）
 * @details 支持解密Python端的多种加密方案
 * @author Christins
 * @date 2024-11-06
 */
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace chen::data {

/**
 * @brief 加密类型枚举
 */
enum class EncryptionType : uint8_t {
    NONE = 0,              // 无加密
    XOR = 1,               // 简单XOR加密（不安全，仅用于兼容）
    AES128_GCM = 2,        // AES-128-GCM
    AES256_GCM = 3,        // AES-256-GCM
    CHACHA20_POLY1305 = 4, // ChaCha20-Poly1305
    CUSTOM = 5             // 自定义加密方案
};

/**
 * @brief 解密元数据结构
 */
struct DecryptionMetadata {
    EncryptionType type;
    std::map<std::string, std::string> data; // 存储字符串形式的元数据

    DecryptionMetadata() : type(EncryptionType::NONE) {}
};

/**
 * @brief 数据解密器类（仅解密）
 */
class DataDecryptor {
public:
    /**
     * @brief 构造函数
     * @param type 加密类型
     * @param password 密码
     */
    explicit DataDecryptor(EncryptionType type, const std::string& password);

    /**
     * @brief 析构函数
     */
    ~DataDecryptor();

    /**
     * @brief 禁用拷贝构造和赋值
     */
    DataDecryptor(const DataDecryptor&) = delete;
    DataDecryptor& operator=(const DataDecryptor&) = delete;

    /**
     * @brief 使用PBKDF2从密码派生密钥
     * @param password 密码
     * @param salt 盐值
     * @param key_length 密钥长度
     * @param iterations 迭代次数
     * @return 派生的密钥
     */
    std::vector<uint8_t> deriveKey(const std::string& password, const std::vector<uint8_t>& salt, size_t key_length = 32, int iterations = 100000);

    /**
     * @brief AES-128-GCM解密
     * @param ciphertext 密文数据（nonce + ciphertext + tag）
     * @param key 解密密钥（16字节）
     * @return 明文数据
     */
    std::vector<uint8_t> decryptAES128GCM(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key);

    /**
     * @brief AES-256-GCM解密
     * @param ciphertext 密文数据（nonce + ciphertext + tag）
     * @param key 解密密钥（32字节）
     * @return 明文数据
     */
    std::vector<uint8_t> decryptAES256GCM(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key);

    /**
     * @brief ChaCha20-Poly1305解密
     * @param ciphertext 密文数据（nonce + ciphertext + tag）
     * @param key 解密密钥（32字节）
     * @return 明文数据
     */
    std::vector<uint8_t> decryptChaCha20Poly1305(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key);

    /**
     * @brief 自定义解密方案
     * @param ciphertext 密文数据（ciphertext + hmac）
     * @param key 解密密钥（至少16字节）
     * @return 明文数据
     */
    std::vector<uint8_t> decryptCustom(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key);

    /**
     * @brief XOR解密（不安全，仅用于兼容）
     * @param data 数据
     * @param key 32位密钥
     * @return 解密后的数据
     */
    std::vector<uint8_t> decryptXOR(const std::vector<uint8_t>& data, uint32_t key);

    /**
     * @brief 从加密的流中解析并解密数据
     * @param data 加密的流数据
     * @param size 数据大小
     * @return 解密后的数据
     */
    std::vector<uint8_t> parseAndDecrypt(const uint8_t* data, size_t size);

    /**
     * @brief 获取解密类型
     */
    EncryptionType getType() const { return m_type; }

private:
    EncryptionType m_type;
    std::string m_password;

    /**
     * @brief 计算HMAC-SHA256
     * @param key 密钥
     * @param data 数据
     * @return HMAC值
     */
    std::vector<uint8_t> computeHMACSHA256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);

    /**
     * @brief 验证HMAC
     * @param key 密钥
     * @param data 数据
     * @param hmac HMAC值
     * @return 是否验证通过
     */
    bool verifyHMACSHA256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data, const std::vector<uint8_t>& hmac);

    /**
     * @brief 十六进制字符串转字节数组
     * @param hex 十六进制字符串
     * @return 字节数组
     */
    std::vector<uint8_t> hexStringToBytes(const std::string& hex);

    /**
     * @brief 处理OpenSSL错误
     * @param msg 错误消息前缀
     */
    void handleOpenSSLError(const std::string& msg);
};

/**
 * @brief 创建解密器
 * @param type_name 加密类型名称
 * @param password 密码
 * @return 解密器指针
 */
std::unique_ptr<DataDecryptor> createDecryptor(const std::string& type_name, const std::string& password);

} // namespace chen::data
