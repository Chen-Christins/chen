/**
 * @file encryptor_util.h
 * @brief 加密/哈希/摘要工具类
 * @author Christins
 * @date 2026-06-26
 */
#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace chen {

/**
 * @brief AES-GCM 加解密结果
 */
struct GCMResult {
    bool success;       ///< 操作是否成功
    std::string data;   ///< 加密/解密后的数据
    std::string tag;    ///< GCM 认证标签（加密时生成，解密时需传入）
    std::string error;  ///< 错误描述（失败时有效）
};

/**
 * @brief RSA 密钥对生成结果
 */
struct RSAKeyPairResult {
    bool success;          ///< 操作是否成功
    std::string private_key;  ///< PEM 格式 RSA 私钥
    std::string public_key;   ///< PEM 格式 RSA 公钥
    std::string error;     ///< 错误描述（失败时有效）
};

/**
 * @brief 加密/哈希/摘要工具类
 */
class EncryptorUtil {
public:
    // ========== AES-GCM 对称加密 ==========

    /**
     * @brief AES-128-GCM 加密
     * @param plaintext 明文数据
     * @param key 密钥，必须为 16 字节
     * @param iv 初始向量，12 字节，为空时自动生成
     * @return GCMResult，包含密文和认证标签
     */
    static GCMResult EncryptAES128GCM(const std::string& plaintext, const std::string& key, const std::string& iv = "");

    /**
     * @brief AES-128-GCM 解密
     * @param ciphertext 密文数据
     * @param tag GCM 认证标签
     * @param key 密钥，必须为 16 字节
     * @param iv 初始向量，12 字节
     * @return GCMResult，解密成功时 data 字段为明文
     */
    static GCMResult DecryptAES128GCM(const std::string& ciphertext, const std::string& tag, const std::string& key, const std::string& iv = "");

    /**
     * @brief AES-256-GCM 加密
     * @param plaintext 明文数据
     * @param key 密钥，必须为 32 字节
     * @param iv 初始向量，12 字节，为空时自动生成
     * @return GCMResult，包含密文和认证标签
     */
    static GCMResult EncryptAES256GCM(const std::string& plaintext, const std::string& key, const std::string& iv = "");

    /**
     * @brief AES-256-GCM 解密
     * @param ciphertext 密文数据
     * @param tag GCM 认证标签
     * @param key 密钥，必须为 32 字节
     * @param iv 初始向量，12 字节
     * @return GCMResult，解密成功时 data 字段为明文
     */
    static GCMResult DecryptAES256GCM(const std::string& ciphertext, const std::string& tag, const std::string& key, const std::string& iv = "");

    /**
     * @brief 生成随机初始向量
     * @param length 向量长度（字节），默认 12
     * @return 随机字节串
     */
    static std::string GenerateRandomIV(size_t length = 12);

    /**
     * @brief 生成随机密钥
     * @param length 密钥长度（字节），默认 32
     * @return 随机字节串
     */
    static std::string GenerateRandomKey(size_t length = 32);

    /**
     * @brief 校验密钥长度是否与预期一致
     * @param key 密钥
     * @param expectedSize 期望长度
     * @return bool 是否匹配
     */
    static bool ValidateKeySize(const std::string& key, size_t expectedSize);

    // ========== 摘要/Hash ==========

    /**
     * @brief 计算 MD5 摘要（十六进制字符串）
     * @param data 输入字符串
     * @return std::string 32 位小写十六进制 MD5
     */
    static std::string MD5(const std::string& data);

    /**
     * @brief 计算 MD5 摘要（十六进制字符串）
     * @param data 数据指针
     * @param len 数据长度
     * @return std::string 32 位小写十六进制 MD5
     */
    static std::string MD5(const void* data, size_t len);

    /**
     * @brief 计算输入流的 MD5 摘要（流式处理，不占内存）
     * @param stream 输入流
     * @return std::string 32 位小写十六进制 MD5，流无效返回空串
     */
    static std::string MD5(std::istream& stream);

    /**
     * @brief 计算文件的 MD5 摘要（流式读取，适合大文件）
     * @param path 文件路径
     * @return std::string 32 位小写十六进制 MD5，文件不存在返回空串
     */
    static std::string MD5File(const std::string& path);

    /**
     * @brief 计算 SHA-1 摘要（十六进制字符串）
     * @param data 输入字符串
     * @return std::string 40 位小写十六进制 SHA-1
     */
    static std::string SHA1(const std::string& data);

    /**
     * @brief 计算 SHA-1 摘要（十六进制字符串）
     * @param data 数据指针
     * @param len 数据长度
     * @return std::string 40 位小写十六进制 SHA-1
     */
    static std::string SHA1(const void* data, size_t len);

    /**
     * @brief 计算 SHA-256 摘要（十六进制字符串）
     * @param data 输入字符串
     * @return std::string 64 位小写十六进制 SHA-256
     */
    static std::string SHA256(const std::string& data);

    /**
     * @brief 计算 SHA-256 摘要（十六进制字符串）
     * @param data 数据指针
     * @param len 数据长度
     * @return std::string 64 位小写十六进制 SHA-256
     */
    static std::string SHA256(const void* data, size_t len);

    /**
     * @brief 计算文件的 SHA-256 摘要（流式读取，适合大文件）
     * @param path 文件路径
     * @return std::string 64 位小写十六进制 SHA-256，文件不存在返回空串
     */
    static std::string SHA256File(const std::string& path);

    /**
     * @brief 计算 SHA-512 摘要（十六进制字符串）
     * @param data 输入字符串
     * @return std::string 128 位小写十六进制 SHA-512
     */
    static std::string SHA512(const std::string& data);

    /**
     * @brief 计算 SHA-512 摘要（十六进制字符串）
     * @param data 数据指针
     * @param len 数据长度
     * @return std::string 128 位小写十六进制 SHA-512
     */
    static std::string SHA512(const void* data, size_t len);

    /**
     * @brief 计算 SHA3-256 摘要（十六进制字符串）
     * @param data 输入字符串
     * @return std::string 小写十六进制 SHA3-256
     */
    static std::string SHA3_256(const std::string& data);

    /**
     * @brief 计算 SHA3-256 摘要（十六进制字符串）
     * @param data 数据指针
     * @param len 数据长度
     * @return std::string 小写十六进制 SHA3-256
     */
    static std::string SHA3_256(const void* data, size_t len);

    /**
     * @brief 计算 SHA3-512 摘要（十六进制字符串）
     * @param data 输入字符串
     * @return std::string 小写十六进制 SHA3-512
     */
    static std::string SHA3_512(const std::string& data);

    /**
     * @brief 计算 SHA3-512 摘要（十六进制字符串）
     * @param data 数据指针
     * @param len 数据长度
     * @return std::string 小写十六进制 SHA3-512
     */
    static std::string SHA3_512(const void* data, size_t len);

    // ========== HMAC 签名 ==========

    /**
     * @brief HMAC-SHA1 签名（原始二进制）
     * @param data 待签名数据
     * @param key 密钥
     * @return std::string 原始二进制签名（20字节），失败返回空串
     */
    static std::string HMAC_SHA1(const std::string& data, const std::string& key);

    /**
     * @brief HMAC-SHA1 签名（原始二进制）
     * @param data 数据指针
     * @param data_len 数据长度
     * @param key 密钥指针
     * @param key_len 密钥长度
     * @return std::string 原始二进制签名（20字节），失败返回空串
     */
    static std::string HMAC_SHA1(const void* data, size_t data_len, const void* key, size_t key_len);

    /**
     * @brief HMAC-SHA256 签名（原始二进制）
     * @param data 待签名数据
     * @param key 密钥
     * @return std::string 原始二进制签名
     */
    static std::string HMAC_SHA256(const std::string& data, const std::string& key);

    /**
     * @brief HMAC-SHA256 签名（原始二进制）
     * @param data 数据指针
     * @param data_len 数据长度
     * @param key 密钥指针
     * @param key_len 密钥长度
     * @return std::string 原始二进制签名
     */
    static std::string HMAC_SHA256(const void* data, size_t data_len, const void* key, size_t key_len);

    /**
     * @brief HMAC-SHA384 签名（原始二进制）
     * @param data 待签名数据
     * @param key 密钥
     * @return std::string 原始二进制签名
     */
    static std::string HMAC_SHA384(const std::string& data, const std::string& key);

    /**
     * @brief HMAC-SHA384 签名（原始二进制）
     * @param data 数据指针
     * @param data_len 数据长度
     * @param key 密钥指针
     * @param key_len 密钥长度
     * @return std::string 原始二进制签名
     */
    static std::string HMAC_SHA384(const void* data, size_t data_len, const void* key, size_t key_len);

    // ========== 非加密 Hash（哈希表、分片） ==========

    /**
     * @brief MurmurHash3 32位（字符串版本）
     * @param str 输入字符串
     * @param seed 种子，默认 1060627423
     * @return uint32_t 哈希值
     */
    static uint32_t Murmur3_32(const char* str, uint32_t seed = 1060627423);

    /**
     * @brief MurmurHash3 64位（字符串版本）
     * @param str 输入字符串
     * @param seed 种子，默认 1060627423
     * @param seed2 第二种子，默认 1050126127
     * @return uint64_t 哈希值
     */
    static uint64_t Murmur3_64(const char* str, uint32_t seed = 1060627423, uint32_t seed2 = 1050126127);

    /**
     * @brief MurmurHash3 32位（二进制数据版本）
     * @param data 数据指针
     * @param size 数据长度
     * @param seed 种子，默认 1060627423
     * @return uint32_t 哈希值
     */
    static uint32_t Murmur3_32(const void* data, uint32_t size, uint32_t seed = 1060627423);

    /**
     * @brief MurmurHash3 64位（二进制数据版本）
     * @param data 数据指针
     * @param size 数据长度
     * @param seed 种子，默认 1060627423
     * @param seed2 第二种子，默认 1050126127
     * @return uint64_t 哈希值
     */
    static uint64_t Murmur3_64(const void* data, uint32_t size, uint32_t seed = 1060627423, uint32_t seed2 = 1050126127);

    /**
     * @brief 快速字符串哈希（基于 31 倍算法，类似 Java String.hashCode）
     * @param str 输入字符串
     * @return uint32_t 哈希值
     */
    static uint32_t QuickHash(const char* str);

    /**
     * @brief 快速二进制数据哈希
     * @param data 数据指针
     * @param size 数据长度
     * @return uint32_t 哈希值
     */
    static uint32_t QuickHash(const void* data, uint32_t size);

    /**
     * @brief XXHash 32位
     * @param data 数据指针
     * @param len 数据长度
     * @param seed 种子，默认 0
     * @return uint32_t 哈希值
     */
    static uint32_t XXHash32(const void* data, size_t len, uint32_t seed = 0);

    /**
     * @brief XXHash 64位
     * @param data 数据指针
     * @param len 数据长度
     * @param seed 种子，默认 0
     * @return uint64_t 哈希值
     */
    static uint64_t XXHash64(const void* data, size_t len, uint64_t seed = 0);

    // ========== RSA 签名 ==========

    /**
     * @brief RSA-SHA256 签名（用于 JWT RS256 等场景）
     * @param data 待签名数据
     * @param privateKeyPem PEM 格式的 RSA 私钥字符串
     * @return 原始二进制签名；失败返回空串
     */
    static std::string RS256Sign(const std::string& data, const std::string& privateKeyPem);

    /**
     * @brief RSA-SHA256 验签
     * @param data 原始数据
     * @param signature 待验证的签名（原始二进制）
     * @param publicKeyPem PEM 格式的 RSA 公钥字符串
     * @return bool 签名是否有效
     */
    static bool RS256Verify(const std::string& data, const std::string& signature
            , const std::string& publicKeyPem);

    /**
     * @brief 生成 RSA 密钥对（PEM 格式）
     * @param bits 密钥位数，默认 2048
     * @return RSAKeyPairResult，success 为 true 时 private_key/public_key 有效
     */
    static RSAKeyPairResult GenerateRSAKeyPair(unsigned bits = 2048);

    // ========== 密码哈希 ==========

    /**
     * @brief bcrypt 密码哈希
     * @param password 明文密码
     * @param rounds 迭代轮数（4-31），实际次数 = 2^rounds，默认 12
     * @return std::string 格式为 $2b$<rounds>$<salt><hash> 的密码串，失败返回空串
     */
    static std::string BCryptHash(const std::string& password, unsigned rounds = 12);

    /**
     * @brief 验证 bcrypt 密码
     * @param password 明文密码
     * @param hash 之前由 BCryptHash 生成的密码串
     * @return bool 密码是否匹配
     */
    static bool BCryptVerify(const std::string& password, const std::string& hash);

private:
    static const size_t AES128_KEY_SIZE = 16;
    static const size_t AES256_KEY_SIZE = 32;
    static const size_t GCM_IV_SIZE = 12;
    static const size_t GCM_TAG_SIZE = 16;

    static GCMResult EncryptGCM(const std::string& plaintext, const std::string& key, const std::string& iv, int keySize);
    static GCMResult DecryptGCM(const std::string& ciphertext, const std::string& tag, const std::string& key, const std::string& iv, int keySize);

    /// 内部：用指定 salt 计算 bcrypt hash（BCryptHash 生成随机 salt，BCryptVerify 用提取的 salt）
    static std::string BCryptHashWithSalt(const std::string& password, unsigned rounds
            , const uint8_t* salt, size_t saltLen);
};

} // namespace chen
