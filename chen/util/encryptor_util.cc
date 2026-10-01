#include "encryptor_util.h"

#include <cstring>
#include <endian.h>
#include <fstream>
#include <istream>
#include <stdexcept>
#include <vector>

#include <crypt.h>
#include <cstdlib>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace chen {

// ============================================================================
// 内部工具：将原始二进制数据转换为小写十六进制字符串
// ============================================================================
static std::string HexString(const void* data, size_t len) {
    if (len == 0) {
        return std::string();
    }
    const unsigned char* buf = (const unsigned char*)data;
    std::string result;
    result.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        char c;
        c = (buf[i] >> 4) & 0xf;
        c = (c > 9) ? c + 'a' - 10 : c + '0';
        result[i * 2] = c;
        c = (buf[i] & 0xf);
        c = (c > 9) ? c + 'a' - 10 : c + '0';
        result[i * 2 + 1] = c;
    }
    return result;
}

// ============================================================================
// 通用 EVP digest 辅助函数
// ============================================================================
static std::string evp_digest(const void* data, size_t len, const EVP_MD* md) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return "";
    }
    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, data, len);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);
    return std::string((const char*)digest, digest_len);
}

// ============================================================================
// 通用 HMAC 辅助函数
// ============================================================================
static std::string evp_hmac(const void* data, size_t data_len, const void* key, size_t key_len, const EVP_MD* md) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;
    if (!HMAC(md, key, key_len, (const unsigned char*)data, data_len,
              result, &result_len)) {
        return "";
    }
    return std::string((const char*)result, result_len);
}

// ============================================================================
// AES-GCM 对称加密
// ============================================================================

GCMResult EncryptorUtil::EncryptAES128GCM(const std::string& plaintext, const std::string& key, const std::string& iv) {
    if (!ValidateKeySize(key, AES128_KEY_SIZE)) {
        return {false, "", "", "Invalid key size for AES-128. Expected 16 bytes."};
    }

    std::string actual_iv = iv.empty() ? GenerateRandomIV(GCM_IV_SIZE) : iv;
    if (actual_iv.size() != GCM_IV_SIZE) {
        return {false, "", "", "Invalid IV size for AES-GCM. Expected 12 bytes."};
    }

    return EncryptGCM(plaintext, key, actual_iv, AES128_KEY_SIZE);
}

GCMResult EncryptorUtil::DecryptAES128GCM(const std::string& ciphertext, const std::string& tag
        , const std::string& key, const std::string& iv) {
    if (!ValidateKeySize(key, AES128_KEY_SIZE)) {
        return {false, "", "", "Invalid key size for AES-128. Expected 16 bytes."};
    }

    if (iv.size() != GCM_IV_SIZE) {
        return {false, "", "", "Invalid IV size for AES-GCM. Expected 12 bytes."};
    }

    if (tag.size() != GCM_TAG_SIZE) {
        return {false, "", "", "Invalid tag size for AES-GCM. Expected 16 bytes."};
    }

    return DecryptGCM(ciphertext, tag, key, iv, AES128_KEY_SIZE);
}

GCMResult EncryptorUtil::EncryptAES256GCM(const std::string& plaintext, const std::string& key, const std::string& iv) {
    if (!ValidateKeySize(key, AES256_KEY_SIZE)) {
        return {false, "", "", "Invalid key size for AES-256. Expected 32 bytes."};
    }

    std::string actual_iv = iv.empty() ? GenerateRandomIV(GCM_IV_SIZE) : iv;
    if (actual_iv.size() != GCM_IV_SIZE) {
        return {false, "", "", "Invalid IV size for AES-GCM. Expected 12 bytes."};
    }

    return EncryptGCM(plaintext, key, actual_iv, AES256_KEY_SIZE);
}

GCMResult EncryptorUtil::DecryptAES256GCM(const std::string& ciphertext, const std::string& tag
        , const std::string& key, const std::string& iv) {
    if (!ValidateKeySize(key, AES256_KEY_SIZE)) {
        return {false, "", "", "Invalid key size for AES-256. Expected 32 bytes."};
    }

    if (iv.size() != GCM_IV_SIZE) {
        return {false, "", "", "Invalid IV size for AES-GCM. Expected 12 bytes."};
    }

    if (tag.size() != GCM_TAG_SIZE) {
        return {false, "", "", "Invalid tag size for AES-GCM. Expected 16 bytes."};
    }

    return DecryptGCM(ciphertext, tag, key, iv, AES256_KEY_SIZE);
}

GCMResult EncryptorUtil::EncryptGCM(const std::string& plaintext, const std::string& key
        , const std::string& iv, int keySize) {
    GCMResult result{false, "", "", ""};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        result.error = "Failed to create cipher context";
        return result;
    }

    const EVP_CIPHER* cipher = (keySize == AES128_KEY_SIZE) ? EVP_aes_128_gcm() : EVP_aes_256_gcm();

    if (1 != EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL)) {
        result.error = "Failed to initialize cipher";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.length(), NULL)) {
        result.error = "Failed to set IV length";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, (const unsigned char*)key.c_str(), (const unsigned char*)iv.c_str())) {
        result.error = "Failed to set key and IV";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    std::string ciphertext;
    ciphertext.resize(plaintext.length() + EVP_MAX_BLOCK_LENGTH);
    int len;
    int ciphertext_len = 0;

    if (1 != EVP_EncryptUpdate(ctx, (unsigned char*)&ciphertext[0], &len, (const unsigned char*)plaintext.c_str(),
                               plaintext.length())) {
        result.error = "Failed to encrypt data";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }
    ciphertext_len = len;

    if (1 != EVP_EncryptFinal_ex(ctx, (unsigned char*)&ciphertext[ciphertext_len], &len)) {
        result.error = "Failed to finalize encryption";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }
    ciphertext_len += len;

    ciphertext.resize(ciphertext_len);

    std::string tag(GCM_TAG_SIZE, '\0');
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_SIZE, (void*)tag.c_str())) {
        result.error = "Failed to get authentication tag";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    result.success = true;
    result.data = ciphertext;
    result.tag = tag;

    EVP_CIPHER_CTX_free(ctx);
    return result;
}

GCMResult EncryptorUtil::DecryptGCM(const std::string& ciphertext, const std::string& tag
        , const std::string& key, const std::string& iv, int keySize) {
    GCMResult result{false, "", "", ""};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        result.error = "Failed to create cipher context";
        return result;
    }

    const EVP_CIPHER* cipher = (keySize == AES128_KEY_SIZE) ? EVP_aes_128_gcm() : EVP_aes_256_gcm();

    if (1 != EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL)) {
        result.error = "Failed to initialize cipher";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.length(), NULL)) {
        result.error = "Failed to set IV length";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    if (1 != EVP_DecryptInit_ex(ctx, NULL, NULL, (const unsigned char*)key.c_str(), (const unsigned char*)iv.c_str())) {
        result.error = "Failed to set key and IV";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    std::string plaintext;
    plaintext.resize(ciphertext.length());
    int len;
    int plaintext_len = 0;

    if (1 != EVP_DecryptUpdate(ctx, (unsigned char*)&plaintext[0], &len, (const unsigned char*)ciphertext.c_str(),
                               ciphertext.length())) {
        result.error = "Failed to decrypt data";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }
    plaintext_len = len;

    // Set expected tag value
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.length(), (void*)tag.c_str())) {
        result.error = "Failed to set authentication tag";
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    // Finalize decryption and verify tag
    int rv = EVP_DecryptFinal_ex(ctx, (unsigned char*)&plaintext[plaintext_len], &len);

    EVP_CIPHER_CTX_free(ctx);

    if (rv != 1) {
        result.error = "Authentication failed - invalid tag or tampered data";
        return result;
    }

    plaintext_len += len;
    plaintext.resize(plaintext_len);

    result.success = true;
    result.data = plaintext;
    return result;
}

std::string EncryptorUtil::GenerateRandomIV(size_t length) {
    std::vector<unsigned char> iv(length);
    if (1 != RAND_bytes(iv.data(), length)) {
        throw std::runtime_error("Failed to generate random IV");
    }
    return std::string(iv.begin(), iv.end());
}

std::string EncryptorUtil::GenerateRandomKey(size_t length) {
    std::vector<unsigned char> key(length);
    if (1 != RAND_bytes(key.data(), length)) {
        throw std::runtime_error("Failed to generate random key");
    }
    return std::string(key.begin(), key.end());
}

bool EncryptorUtil::ValidateKeySize(const std::string& key, size_t expectedSize) {
    return key.length() == expectedSize;
}

// ============================================================================
// RSA-SHA256 签名
// ============================================================================

std::string EncryptorUtil::RS256Sign(const std::string& data, const std::string& privateKeyPem) {
    if (privateKeyPem.empty()) {
        return "";
    }

    BIO* bio = BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size()));
    if (!bio) {
        return "";
    }

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        return "";
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return "";
    }

    std::string signature;
    size_t sigLen = 0;

    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
        goto cleanup;
    }
    if (EVP_DigestSignUpdate(ctx, data.data(), data.size()) != 1) {
        goto cleanup;
    }
    // 获取签名长度
    if (EVP_DigestSignFinal(ctx, nullptr, &sigLen) != 1) {
        goto cleanup;
    }
    {
        std::vector<unsigned char> sigBuf(sigLen);
        if (EVP_DigestSignFinal(ctx, sigBuf.data(), &sigLen) != 1) {
            goto cleanup;
        }
        signature.assign(reinterpret_cast<const char*>(sigBuf.data()), sigLen);
    }

cleanup:
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return signature;
}

// ============================================================================
// RSA-SHA256 验签
// ============================================================================

bool EncryptorUtil::RS256Verify(const std::string& data, const std::string& signature
        , const std::string& publicKeyPem) {
    if (publicKeyPem.empty() || signature.empty()) {
        return false;
    }

    BIO* bio = BIO_new_mem_buf(publicKeyPem.data(), static_cast<int>(publicKeyPem.size()));
    if (!bio) {
        return false;
    }

    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool ok = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
        goto cleanup;
    }
    if (EVP_DigestVerifyUpdate(ctx, data.data(), data.size()) != 1) {
        goto cleanup;
    }
    if (EVP_DigestVerifyFinal(ctx
            , reinterpret_cast<const unsigned char*>(signature.data())
            , signature.size()) == 1) {
        ok = true;
    }

cleanup:
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

// ============================================================================
// RSA 密钥对生成
// ============================================================================

RSAKeyPairResult EncryptorUtil::GenerateRSAKeyPair(unsigned bits) {
    RSAKeyPairResult result{false, "", "", ""};

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) {
        result.error = "EVP_PKEY_CTX_new_id failed";
        return result;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        result.error = "EVP_PKEY_keygen_init failed";
        EVP_PKEY_CTX_free(ctx);
        return result;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, static_cast<int>(bits)) <= 0) {
        result.error = "EVP_PKEY_CTX_set_rsa_keygen_bits failed";
        EVP_PKEY_CTX_free(ctx);
        return result;
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        result.error = "EVP_PKEY_keygen failed";
        EVP_PKEY_CTX_free(ctx);
        return result;
    }
    EVP_PKEY_CTX_free(ctx);

    // 写出私钥
    BIO* privBio = BIO_new(BIO_s_mem());
    if (!privBio) {
        result.error = "BIO_new for private key failed";
        EVP_PKEY_free(pkey);
        return result;
    }
    if (PEM_write_bio_PrivateKey(privBio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        result.error = "PEM_write_bio_PrivateKey failed";
        BIO_free(privBio);
        EVP_PKEY_free(pkey);
        return result;
    }
    {
        const char* pemData;
        long pemLen = BIO_get_mem_data(privBio, &pemData);
        result.private_key.assign(pemData, pemLen);
    }
    BIO_free(privBio);

    // 写出公钥
    BIO* pubBio = BIO_new(BIO_s_mem());
    if (!pubBio) {
        result.error = "BIO_new for public key failed";
        EVP_PKEY_free(pkey);
        return result;
    }
    if (PEM_write_bio_PUBKEY(pubBio, pkey) != 1) {
        result.error = "PEM_write_bio_PUBKEY failed";
        BIO_free(pubBio);
        EVP_PKEY_free(pkey);
        return result;
    }
    {
        const char* pemData;
        long pemLen = BIO_get_mem_data(pubBio, &pemData);
        result.public_key.assign(pemData, pemLen);
    }
    BIO_free(pubBio);

    EVP_PKEY_free(pkey);
    result.success = true;
    return result;
}

// ============================================================================
// SHA-1 — 兼容旧系统
// ============================================================================
std::string EncryptorUtil::SHA1(const std::string& data) {
    return SHA1(data.c_str(), data.size());
}

std::string EncryptorUtil::SHA1(const void* data, size_t len) {
    SHA_CTX ctx;
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, data, len);
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1_Final(digest, &ctx);
    return HexString(digest, SHA_DIGEST_LENGTH);
}

// ============================================================================
// MD5
// ============================================================================
std::string EncryptorUtil::MD5(const std::string& data) {
    return MD5(data.c_str(), data.size());
}

std::string EncryptorUtil::MD5(const void* data, size_t len) {
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, data, len);
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);
    return HexString(digest, MD5_DIGEST_LENGTH);
}

std::string EncryptorUtil::MD5(std::istream& stream) {
    MD5_CTX ctx;
    MD5_Init(&ctx);
    char buf[8192];
    while (stream) {
        stream.read(buf, sizeof(buf));
        MD5_Update(&ctx, buf, static_cast<size_t>(stream.gcount()));
    }
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);
    return HexString(digest, MD5_DIGEST_LENGTH);
}

std::string EncryptorUtil::MD5File(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return "";
    }
    return MD5(ifs);
}

// ============================================================================
// SHA-256
// ============================================================================
std::string EncryptorUtil::SHA256(const std::string& data) {
    return SHA256(data.c_str(), data.size());
}

std::string EncryptorUtil::SHA256(const void* data, size_t len) {
    std::string raw = evp_digest(data, len, EVP_sha256());
    return HexString(raw.c_str(), raw.size());
}

std::string EncryptorUtil::SHA256File(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return "";
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { return ""; }
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[8192];
    while (ifs) {
        ifs.read(buf, sizeof(buf));
        EVP_DigestUpdate(ctx, buf, static_cast<size_t>(ifs.gcount()));
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);
    return HexString(digest, digest_len);
}

// ============================================================================
// SHA-512
// ============================================================================
std::string EncryptorUtil::SHA512(const std::string& data) {
    return SHA512(data.c_str(), data.size());
}

std::string EncryptorUtil::SHA512(const void* data, size_t len) {
    std::string raw = evp_digest(data, len, EVP_sha512());
    return HexString(raw.c_str(), raw.size());
}

// ============================================================================
// SHA3-256
// ============================================================================
std::string EncryptorUtil::SHA3_256(const std::string& data) {
    return SHA3_256(data.c_str(), data.size());
}

std::string EncryptorUtil::SHA3_256(const void* data, size_t len) {
    std::string raw = evp_digest(data, len, EVP_sha3_256());
    return HexString(raw.c_str(), raw.size());
}

// ============================================================================
// SHA3-512
// ============================================================================
std::string EncryptorUtil::SHA3_512(const std::string& data) {
    return SHA3_512(data.c_str(), data.size());
}

std::string EncryptorUtil::SHA3_512(const void* data, size_t len) {
    std::string raw = evp_digest(data, len, EVP_sha3_512());
    return HexString(raw.c_str(), raw.size());
}

// ============================================================================
// HMAC-SHA1 — 返回原始二进制数据
// ============================================================================
std::string EncryptorUtil::HMAC_SHA1(const std::string& data, const std::string& key) {
    return HMAC_SHA1(data.c_str(), data.size(), key.c_str(), key.size());
}

std::string EncryptorUtil::HMAC_SHA1(const void* data, size_t data_len, const void* key, size_t key_len) {
    return evp_hmac(data, data_len, key, key_len, EVP_sha1());
}

// ============================================================================
// HMAC-SHA256 — 返回原始二进制数据
// ============================================================================
std::string EncryptorUtil::HMAC_SHA256(const std::string& data, const std::string& key) {
    return HMAC_SHA256(data.c_str(), data.size(), key.c_str(), key.size());
}

std::string EncryptorUtil::HMAC_SHA256(const void* data, size_t data_len, const void* key, size_t key_len) {
    return evp_hmac(data, data_len, key, key_len, EVP_sha256());
}

// ============================================================================
// HMAC-SHA384 — 返回原始二进制数据
// ============================================================================
std::string EncryptorUtil::HMAC_SHA384(const std::string& data, const std::string& key) {
    return HMAC_SHA384(data.c_str(), data.size(), key.c_str(), key.size());
}

std::string EncryptorUtil::HMAC_SHA384(const void* data, size_t data_len, const void* key, size_t key_len) {
    return evp_hmac(data, data_len, key, key_len, EVP_sha384());
}

// ============================================================================
// Murmur3 Hash
// ============================================================================
#define ROTL(x, r) ((x << r) | (x >> (32 - r)))

static inline uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

uint32_t EncryptorUtil::Murmur3_32(const void* data, uint32_t size, uint32_t seed) {
    if (!data) {
        return 0;
    }

    const char* str = (const char*)data;
    uint32_t s, h = seed, seed1 = 0xcc9e2d51, seed2 = 0x1b873593, *ptr = (uint32_t*)str;

    // handle begin blocks
    int len = size;
    int blk = len / 4;
    for (int i = 0; i < blk; i++) {
        s = ptr[i];
        s *= seed1;
        s = ROTL(s, 15);
        s *= seed2;

        h ^= s;
        h = ROTL(h, 13);
        h *= 5;
        h += 0xe6546b64;
    }

    // handle tail
    s = 0;
    uint8_t* tail = (uint8_t*)(str + blk * 4);
    switch (len & 3) {
    case 3:
        s |= tail[2] << 16;
    case 2:
        s |= tail[1] << 8;
    case 1:
        s |= tail[0];

        s *= seed1;
        s = ROTL(s, 15);
        s *= seed2;
        h ^= s;
    };

    return fmix32(h ^ len);
}

uint32_t EncryptorUtil::Murmur3_32(const char* str, uint32_t seed) {
    if (!str) {
        return 0;
    }

    uint32_t s, h = seed, seed1 = 0xcc9e2d51, seed2 = 0x1b873593, *ptr = (uint32_t*)str;

    // handle begin blocks
    int len = (int)strlen(str);
    int blk = len / 4;
    for (int i = 0; i < blk; i++) {
        s = ptr[i];
        s *= seed1;
        s = ROTL(s, 15);
        s *= seed2;

        h ^= s;
        h = ROTL(h, 13);
        h *= 5;
        h += 0xe6546b64;
    }

    // handle tail
    s = 0;
    uint8_t* tail = (uint8_t*)(str + blk * 4);
    switch (len & 3) {
    case 3:
        s |= tail[2] << 16;
    case 2:
        s |= tail[1] << 8;
    case 1:
        s |= tail[0];

        s *= seed1;
        s = ROTL(s, 15);
        s *= seed2;
        h ^= s;
    };

    return fmix32(h ^ len);
}

uint64_t EncryptorUtil::Murmur3_64(const void* data, uint32_t size, uint32_t seed, uint32_t seed2) {
    return (((uint64_t)Murmur3_32(data, size, seed)) << 32 | Murmur3_32(data, size, seed2));
}

uint64_t EncryptorUtil::Murmur3_64(const char* str, uint32_t seed, uint32_t seed2) {
    return (((uint64_t)Murmur3_32(str, seed)) << 32 | Murmur3_32(str, seed2));
}

// ============================================================================
// Quick Hash
// ============================================================================
uint32_t EncryptorUtil::QuickHash(const char* str) {
    unsigned int h = 0;
    for (; *str; str++) {
        h = 31 * h + *str;
    }
    return h;
}

uint32_t EncryptorUtil::QuickHash(const void* tmp, uint32_t size) {
    const char* str = (const char*)tmp;
    unsigned int h = 0;
    for (uint32_t i = 0; i < size; ++i) {
        h = 31 * h + str[i];
    }
    return h;
}

// ============================================================================
// XXHash (XXH32 / XXH64)
// ============================================================================
static const uint32_t XXH_PRIME32_1 = 0x9E3779B1;
static const uint32_t XXH_PRIME32_2 = 0x85EBCA77;
static const uint32_t XXH_PRIME32_3 = 0xC2B2AE3D;
static const uint32_t XXH_PRIME32_4 = 0x27D4EB2F;
static const uint32_t XXH_PRIME32_5 = 0x165667B1;

static const uint64_t XXH_PRIME64_1 = 0x9E3779B185EBCA87ULL;
static const uint64_t XXH_PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
static const uint64_t XXH_PRIME64_3 = 0x165667B19E3779F9ULL;
static const uint64_t XXH_PRIME64_4 = 0x85EBCA77C2B2AE3DULL;
static const uint64_t XXH_PRIME64_5 = 0x27D4EB2F165667C5ULL;

static inline uint32_t XXH_rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

static inline uint64_t XXH_rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static inline uint32_t XXH_readLE32(const void* ptr) {
    uint32_t val;
    memcpy(&val, ptr, sizeof(val));
    return val;
}

static inline uint64_t XXH_readLE64(const void* ptr) {
    uint64_t val;
    memcpy(&val, ptr, sizeof(val));
    return val;
}

static uint32_t XXH32_avalanche(uint32_t h) {
    h ^= h >> 15;
    h *= XXH_PRIME32_2;
    h ^= h >> 13;
    h *= XXH_PRIME32_3;
    h ^= h >> 16;
    return h;
}

uint32_t EncryptorUtil::XXHash32(const void* data, size_t len, uint32_t seed) {
    const uint8_t* p = (const uint8_t*)data;
    const uint8_t* const end = p + len;
    uint32_t h32;

    if (len >= 16) {
        const uint8_t* const limit = end - 16;
        uint32_t v1 = seed + XXH_PRIME32_1 + XXH_PRIME32_2;
        uint32_t v2 = seed + XXH_PRIME32_2;
        uint32_t v3 = seed;
        uint32_t v4 = seed - XXH_PRIME32_1;

        do {
            v1 = XXH_rotl32(v1 + XXH_readLE32(p) * XXH_PRIME32_2, 13) * XXH_PRIME32_1;
            p += 4;
            v2 = XXH_rotl32(v2 + XXH_readLE32(p) * XXH_PRIME32_2, 13) * XXH_PRIME32_1;
            p += 4;
            v3 = XXH_rotl32(v3 + XXH_readLE32(p) * XXH_PRIME32_2, 13) * XXH_PRIME32_1;
            p += 4;
            v4 = XXH_rotl32(v4 + XXH_readLE32(p) * XXH_PRIME32_2, 13) * XXH_PRIME32_1;
            p += 4;
        } while (p <= limit);

        h32 = XXH_rotl32(v1, 1) + XXH_rotl32(v2, 7) + XXH_rotl32(v3, 12) + XXH_rotl32(v4, 18);
    } else {
        h32 = seed + XXH_PRIME32_5;
    }

    h32 += (uint32_t)len;

    while (p + 4 <= end) {
        h32 += XXH_readLE32(p) * XXH_PRIME32_3;
        h32 = XXH_rotl32(h32, 17) * XXH_PRIME32_4;
        p += 4;
    }

    while (p < end) {
        h32 += (uint32_t)(*p) * XXH_PRIME32_5;
        h32 = XXH_rotl32(h32, 11) * XXH_PRIME32_1;
        p++;
    }

    return XXH32_avalanche(h32);
}

static uint64_t XXH64_avalanche(uint64_t h) {
    h ^= h >> 33;
    h *= XXH_PRIME64_2;
    h ^= h >> 29;
    h *= XXH_PRIME64_3;
    h ^= h >> 32;
    return h;
}

uint64_t EncryptorUtil::XXHash64(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = (const uint8_t*)data;
    const uint8_t* const end = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
        uint64_t v2 = seed + XXH_PRIME64_2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - XXH_PRIME64_1;

        do {
            v1 = XXH_rotl64(v1 + XXH_readLE64(p) * XXH_PRIME64_2, 31) * XXH_PRIME64_1;
            p += 8;
            v2 = XXH_rotl64(v2 + XXH_readLE64(p) * XXH_PRIME64_2, 31) * XXH_PRIME64_1;
            p += 8;
            v3 = XXH_rotl64(v3 + XXH_readLE64(p) * XXH_PRIME64_2, 31) * XXH_PRIME64_1;
            p += 8;
            v4 = XXH_rotl64(v4 + XXH_readLE64(p) * XXH_PRIME64_2, 31) * XXH_PRIME64_1;
            p += 8;
        } while (p <= limit);

        h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) + XXH_rotl64(v4, 18);

        v1 *= XXH_PRIME64_2;
        v1 = XXH_rotl64(v1, 31);
        v1 *= XXH_PRIME64_1;
        h64 ^= v1;
        h64 = h64 * XXH_PRIME64_1 + XXH_PRIME64_4;

        v2 *= XXH_PRIME64_2;
        v2 = XXH_rotl64(v2, 31);
        v2 *= XXH_PRIME64_1;
        h64 ^= v2;
        h64 = h64 * XXH_PRIME64_1 + XXH_PRIME64_4;

        v3 *= XXH_PRIME64_2;
        v3 = XXH_rotl64(v3, 31);
        v3 *= XXH_PRIME64_1;
        h64 ^= v3;
        h64 = h64 * XXH_PRIME64_1 + XXH_PRIME64_4;

        v4 *= XXH_PRIME64_2;
        v4 = XXH_rotl64(v4, 31);
        v4 *= XXH_PRIME64_1;
        h64 ^= v4;
        h64 = h64 * XXH_PRIME64_1 + XXH_PRIME64_4;
    } else {
        h64 = seed + XXH_PRIME64_5;
    }

    h64 += (uint64_t)len;

    while (p + 8 <= end) {
        uint64_t k1 = XXH_readLE64(p) * XXH_PRIME64_2;
        k1 = XXH_rotl64(k1, 31);
        k1 *= XXH_PRIME64_1;
        h64 ^= k1;
        h64 = XXH_rotl64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        p += 8;
    }

    if (p + 4 <= end) {
        h64 ^= (uint64_t)(XXH_readLE32(p)) * XXH_PRIME64_1;
        h64 = XXH_rotl64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        p += 4;
    }

    while (p < end) {
        h64 ^= (uint64_t)(*p) * XXH_PRIME64_5;
        h64 = XXH_rotl64(h64, 11) * XXH_PRIME64_1;
        p++;
    }

    return XXH64_avalanche(h64);
}

// ============================================================================
// bcrypt — 基于 Blowfish 的密码哈希
// ============================================================================

// Blowfish 初始 P-array（来自圆周率 pi 的十六进制位）
static const uint32_t BF_P[18] = {
    0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344, 0xa4093822, 0x299f31d0,
    0x082efa98, 0xec4e6c89, 0x452821e6, 0x38d01377, 0xbe5466cf, 0x34e90c6c,
    0xc0ac29b7, 0xc97c50dd, 0x3f84d5b5, 0xb5470917, 0x9216d5d9, 0x8979fb1b
};

// Blowfish 初始 S-box（来自圆周率 pi 的十六进制位）
static const uint32_t BF_S[4][256] = {
    {0xd1310ba6,0x98dfb5ac,0x2ffd72db,0xd01adfb7,0xb8e1afed,0x6a267e96,0xba7c9045,0xf12c7f99,
     0x24a19947,0xb3916cf7,0x0801f2e2,0x858efc16,0x636920d8,0x71574e69,0xa458fea3,0xf4933d7e,
     0x0d95748f,0x728eb658,0x718bcd58,0x82154aee,0x7b54a41d,0xc25a59b5,0x9c30d539,0x2af26013,
     0xc5d1b023,0x286085f0,0xca417918,0xb8db38ef,0x8e79dcb0,0x603a180e,0x6c9e0e8b,0xb01e8a3e,
     0xd71577c1,0xbd314b27,0x78af2fda,0x55605c60,0xe65525f3,0xaa55ab94,0x57489862,0x63e81440,
     0x55ca396a,0x2aab10b6,0xb4cc5c34,0x1141e8ce,0xa15486af,0x7c72e993,0xb3ee1411,0x636fbc2a,
     0x2ba9c55d,0x741831f6,0xce5c3e16,0x9b87931e,0xafd6ba33,0x6c24cf5c,0x7a325381,0x28958677,
     0x3b8f4898,0x6b4bb9af,0xc4bfe81b,0x66282193,0x61d809cc,0xfb21a991,0x487cac60,0x5dec8032,
     0xef845d5d,0xe98575b1,0xdc262302,0xeb651b88,0x23893e81,0xd396acc5,0x0f6d6ff3,0x83f44239,
     0x2e0b4482,0xa4842004,0x69c8f04a,0x9e1f9b5e,0x21c66842,0xf6e96c9a,0x670c9c61,0xabd388f0,
     0x6a51a0d2,0xd8542f68,0x960fa728,0xab5133a3,0x6eef0b6c,0x137a3be4,0xba3bf050,0x7efb2a98,
     0xa1f1651d,0x39af0176,0x66ca593e,0x82430e88,0x8cee8619,0x456f9fb4,0x7d84a5c3,0x3b8b5ebe,
     0xe06f75d8,0x85c12073,0x401a449f,0x56c16aa6,0x4ed3aa62,0x363f7706,0x1bfedf72,0x429b023d,
     0x37d0d724,0xd00a1248,0xdb0fead3,0x49f1c09b,0x075372c9,0x80991b7b,0x25d479d8,0xf6e8def7,
     0xe3fe501a,0xb6794c3b,0x976ce0bd,0x04c006ba,0xc1a94fb6,0x409f60c4,0x5e5c9ec2,0x196a2463,
     0x68fb6faf,0x3e6c53b5,0x1339b2eb,0x3b52ec6f,0x6dfc511f,0x9b30952c,0xcc814544,0xaf5ebd09,
     0xbee3d004,0xde334afd,0x660f2807,0x192e4bb3,0xc0cba857,0x45c8740f,0xd20b5f39,0xb9d3fbdb,
     0x5579c0bd,0x1a60320a,0xd6a100c6,0x402c7279,0x679f25fe,0xfb1fa3cc,0x8ea5e9f8,0xdb3222f8,
     0x3c7516df,0xfd616b15,0x2f501ec8,0xad0552ab,0x323db5fa,0xfd238760,0x53317b48,0x3e00df82,
     0x9e5c57bb,0xca6f8ca0,0x1a87562e,0xdf1769db,0xd542a8f6,0x287effc3,0xac6732c6,0x8c4f5573,
     0x695b27b0,0xbbca58c8,0xe1ffa35d,0xb8f011a0,0x10fa3d98,0xfd2183b8,0x4afcb56c,0x2dd1d35b,
     0x9a53e479,0xb6f84565,0xd28e49bc,0x4bfb9790,0xe1ddf2da,0xa4cb7e33,0x62fb1341,0xcee4c6e8,
     0xef20cada,0x36774c01,0xd07e9efe,0x2bf11fb4,0x95dbda4d,0xae909198,0xeaad8e71,0x6b93d5a0,
     0xd08ed1d0,0xafc725e0,0x8e3c5b2f,0x8e7594b7,0x8ff6e2fb,0xf2122b64,0x8888b812,0x900df01c,
     0x4fad5ea0,0x688fc31c,0xd1cff191,0xb3a8c1ad,0x2f2f2218,0xbe0e1777,0xea752dfe,0x8b021fa1,
     0xe5a0cc0f,0xb56f74e8,0x18acf3d6,0xce89e299,0xb4a84fe0,0xfd13e0b7,0x7cc43b81,0xd2ada8d9,
     0x165fa266,0x80957705,0x93cc7314,0x211a1477,0xe6ad2065,0x77b5fa86,0xc75442f5,0xfb9d35cf,
     0xebcdaf0c,0x7b3e89a0,0xd6411bd3,0xae1e7e49,0x00250e2d,0x2071b35e,0x226800bb,0x57b8e0af,
     0x2464369b,0xf009b91e,0x5563911d,0x59dfa6aa,0x78c14389,0xd95a537f,0x207d5ba2,0x02e5b9c5,
     0x83260376,0x6295cfa9,0x11c81968,0x4e734a41,0xb3472dca,0x7b14a94a,0x1b510052,0x9a532915,
     0xd60f573f,0xbc9bc6e4,0x2b60a476,0x81e67400,0x08ba6fb5,0x571be91f,0xf296ec6b,0x2a0dd915,
     0xb6636521,0xe7b9f9b6,0xff34052e,0xc5855664,0x53b02d5d,0xa99f8fa1,0x08ba4799,0x6e85076a},
    {0x4b7a70e9,0xb5b32944,0xdb75092e,0xc4192623,0xad6ea6b0,0x49a7df7d,0x9cee60b8,0x8fedb266,
     0xecaa8c71,0x699a17ff,0x5664526c,0xc2b19ee1,0x193602a5,0x75094c29,0xa0591340,0xe4183a3e,
     0x3f54989a,0x5b429d65,0x6b8fe4d6,0x99f73fd6,0xa1d29c07,0xefe830f5,0x4d2d38e6,0xf0255dc1,
     0x4cdd2086,0x8470eb26,0x6382e9c6,0x021ecc5e,0x09686b3f,0x3ebaefc9,0x3c971814,0x6b6a70a1,
     0x687f3584,0x52a0e286,0xb79c5305,0xaa500737,0x3e07841c,0x7fdeae5c,0x8e7d44ec,0x5716f2b8,
     0xb03ada37,0xf0500c0d,0xf01c1f04,0x0200b3ff,0xae0cf51a,0x3cb574b2,0x25837a58,0xdc0921bd,
     0xd19113f9,0x7ca92ff6,0x94324773,0x22f54701,0x3ae5e581,0x37c2dadc,0xc8b57634,0x9af3dda7,
     0xa9446146,0x0fd0030e,0xecc8c73e,0xa4751e41,0xe238cd99,0x3bea0e2f,0x3280bba1,0x183eb331,
     0x4e548b38,0x4f6db908,0x6f420d03,0xf60a04bf,0x2cb81290,0x24977c79,0x5679b072,0xbcaf89af,
     0xde9a771f,0xd9930810,0xb38bae12,0xdccf3f2e,0x5512721f,0x2e6b7124,0x501adde6,0x9f84cd87,
     0x7a584718,0x7408da17,0xbc9f9abc,0xe94b7d8c,0xec7aec3a,0xdb851dfa,0x63094366,0xc464c3d2,
     0xef1c1847,0x3215d908,0xdd433b37,0x24c2ba16,0x12a14d43,0x2a65c451,0x50940002,0x133ae4dd,
     0x71dff89e,0x10314e55,0x81ac77d6,0x5f11199b,0x043556f1,0xd7a3c76b,0x3c11183b,0x5924a509,
     0xf28fe6ed,0x97f1fbfa,0x9ebabf2c,0x1e153c6e,0x86e34570,0xeae96fb1,0x860e5e0a,0x5a3e2ab3,
     0x771fe71c,0x4e3d06fa,0x2965dcb9,0x99e71d0f,0x803e89d6,0x5266c825,0x2e4cc978,0x9c10b36a,
     0xc6150eba,0x94e2ea78,0xa5fc3c53,0x1e0a2df4,0xf2f74ea7,0x361d2b3d,0x1939260f,0x19c27960,
     0x5223a708,0xf71312b6,0xebadfe6e,0xeac31f66,0xe3bc4595,0xa67bc883,0xb17f37d1,0x018cff28,
     0xc332ddef,0xbe6c5aa5,0x65582185,0x68ab9802,0xeecea50f,0xdb2f953b,0x2aef7dad,0x5b6e2f84,
     0x1521b628,0x29076170,0xecdd4775,0x619f1510,0x13cca830,0xeb61bd96,0x0334fe1e,0xaa0363cf,
     0xb5735c90,0x4c70a239,0xd59e9e0b,0xcbaade14,0xeecc86bc,0x60622ca7,0x9cab5cab,0xb2f3846e,
     0x648b1eaf,0x19bdf0ca,0xa02369b9,0x655abb50,0x40685a32,0x3c2ab4b3,0x319ee9d5,0xc021b8f7,
     0x9b540b19,0x875fa099,0x95f7997e,0x623d7da8,0xf837889a,0x97e32d77,0x11ed935f,0x16681281,
     0x0e358829,0xc7e61fd6,0x96dedfa1,0x7858ba99,0x57f584a5,0x1b227263,0x9b83c3ff,0x1ac24696,
     0xcdb30aeb,0x532e3054,0x8fd948e4,0x6dbc3128,0x58ebf2ef,0x34c6ffea,0xfe28ed61,0xee7c3c73,
     0x5d4a14d9,0xe864b7e3,0x42105d14,0x203e13e0,0x45eee2b6,0xa3aaabea,0xdb6c4f15,0xfacb4fd0,
     0xc742f442,0xef6abbb5,0x654f3b1d,0x41cd2105,0xd81e799e,0x86854dc7,0xe44b476a,0x3d816250,
     0xcf62a1f2,0x5b8d2646,0xfc8883a0,0xc1c7b6a3,0x7f1524c3,0x69cb7492,0x47848a0b,0x5692b285,
     0x095bbf00,0xad19489d,0x1462b174,0x23820e00,0x58428d2a,0x0c55f5ea,0x1dadf43e,0x233f7061,
     0x3372f092,0x8d937e41,0xd65fecf1,0x6c223bdb,0x7cde3759,0xcbee7460,0x4085f2a7,0xce77326e,
     0xa6078084,0x19f8509e,0xe8efd855,0x61d99735,0xa969a7aa,0xc50c06c2,0x5a04abfc,0x800bcadc,
     0x9e447a2e,0xc3453484,0xfdd56705,0x0e1e9ec9,0xdb73dbd3,0x105588cd,0x675fda79,0xe3674340,
     0xc5c43465,0x713e38d8,0x3d28f89e,0xf16dff20,0x153e21e7,0x8fb03d4a,0xe6e39f2b,0xdb83adf7},
    {0xe93d5a68,0x948140f7,0xf64c261c,0x94692934,0x411520f7,0x7602d4f7,0xbcf46b2e,0xd4a20068,
     0xd4082471,0x3320f46a,0x43b7d4b7,0x500061af,0x1e39f62e,0x97244546,0x14214f74,0xbf8b8840,
     0x4d95fc1d,0x96b591af,0x70f4ddd3,0x66a02f45,0xbfbc09ec,0x03bd9785,0x7fac6dd0,0x31cb8504,
     0x96eb27b3,0x55fd3941,0xda2547e6,0xabca0a9a,0x28507825,0x530429f4,0x0a2c86da,0xe9b66dfb,
     0x68dc1462,0xd7486900,0x680ec0a4,0x27a18dee,0x4f3ffea2,0xe887ad8c,0xb58ce006,0x7af4d6b6,
     0xaace1e7c,0xd3375fec,0xce78a399,0x406b2a42,0x20fe9e35,0xd9f385b9,0xee39d7ab,0x3b124e8b,
     0x1dc9faf7,0x4b6d1856,0x26a36631,0xeae397b2,0x3a6efa74,0xdd5b4332,0x6841e7f7,0xca7820fb,
     0xfb0af54e,0xd8feb397,0x454056ac,0xba489527,0x55533a3a,0x20838d87,0xfe6ba9b7,0xd096954b,
     0x55a867bc,0xa1159a58,0xcca92963,0x99e1db33,0xa62a4a56,0x3f3125f9,0x5ef47e1c,0x9029317c,
     0xfdf8e802,0x04272f70,0x80bb155c,0x05282ce3,0x95c11548,0xe4c66d22,0x48c1133f,0xc70f86dc,
     0x07f9c9ee,0x41041f0f,0x404779a4,0x5d886e17,0x325f51eb,0xd59bc0d1,0xf2bcc18f,0x41113564,
     0x257b7834,0x602a9c60,0xdff8e8a3,0x1f636c1b,0x0e12b4c2,0x02e1329e,0xaf664fd1,0xcad18115,
     0x6b2395e0,0x333e92e1,0x3b240b62,0xeebeb922,0x85b2a20e,0xe6ba0d99,0xde720c8c,0x2da2f728,
     0xd0127845,0x95b794fd,0x647d0862,0xe7ccf5f0,0x5449a36f,0x877d48fa,0xc39dfd27,0xf33e8d1e,
     0x0a476341,0x992eff74,0x3a6f6eab,0xf4f8fd37,0xa812dc60,0xa1ebddf8,0x991be14c,0xdb6e6b0d,
     0xc67b5510,0x6d672c37,0x2765d43b,0xdcd0e804,0xf1290dc7,0xcc00ffa3,0xb5390f92,0x690fed0b,
     0x667b9ffb,0xcedb7d9c,0xa091cf0b,0xd9155ea3,0xbb132f88,0x515bad24,0x7b9479bf,0x763bd6eb,
     0x37392eb3,0xcc115979,0x8026e297,0xf42e312d,0x6842ada7,0xc66a2b3b,0x12754ccc,0x782ef11c,
     0x6a124237,0xb79251e7,0x06a1bbe6,0x4bfb6350,0x1a6b1018,0x11caedfa,0x3d25bdd8,0xe2e1c3c9,
     0x44421659,0x0a121386,0xd90cec6e,0xd5abea2a,0x64af674e,0xda86a85f,0xbebfe988,0x64e4c3fe,
     0x9dbc8057,0xf0f7c086,0x60787bf8,0x6003604d,0xd1fd8346,0xf6381fb0,0x7745ae04,0xd736fccc,
     0x83426b33,0xf01eab71,0xb0804187,0x3c005e5f,0x77a057be,0xbde8ae24,0x55464299,0xbf582e61,
     0x4e58f48f,0xf2ddfda2,0xf474ef38,0x8789bdc2,0x5366f9c3,0xc8b38e74,0xb475f255,0x46fcd9b9,
     0x7aeb2661,0x8b1ddf84,0x846a0e79,0x915f95e2,0x466e598e,0x20b45770,0x8cd55591,0xc902de4c,
     0xb90bace1,0xbb8205d0,0x11a86248,0x7574a99e,0xb77f19b6,0xe0a9dc09,0x662d09a1,0xc4324633,
     0xe85a1f02,0x09f0be8c,0x4a99a025,0x1d6efe10,0x1ab93d1d,0x0ba5a4df,0xa186f20f,0x2868f169,
     0xdcb7da83,0x573906fe,0xa1e2ce9b,0x4fcd7f52,0x50115e01,0xa70683fa,0xa002b5c4,0x0de6d027,
     0x9af88c27,0x773f8641,0xc3604c06,0x61a806b5,0xf0177a28,0xc0f586e0,0x006058aa,0x30dc7d62,
     0x11e69ed7,0x2338ea63,0x53c2dd94,0xc2c21634,0xbbcbee56,0x90bcb6de,0xebfc7da1,0xce591d76,
     0x6f05e409,0x4b7c0188,0x39720a3d,0x7c927c24,0x86e3725f,0x724d9db9,0x1ac15bb4,0xd39eb8fc,
     0xed545578,0x08fca5b5,0xd83d7cd3,0x4dad0fc4,0x1e50ef5e,0xb161e6f8,0xa28514d9,0x6c51133c,
     0x6fd5c7e7,0x56e14ec4,0x362abfce,0xddc6c837,0xd79a3234,0x92638212,0x670efa8e,0x406000e0},
    {0x3a39ce37,0xd3faf5cf,0xabc27737,0x5ac52d1b,0x5cb0679e,0x4fa33742,0xd3822740,0x99bc9bbe,
     0xd5118e9d,0xbf0f7315,0xd62d1c7e,0xc700c47b,0xb78c1b6b,0x21a19045,0xb26eb1be,0x6a366eb4,
     0x5748ab2f,0xbc946e79,0xc6a376d2,0x6549c2c8,0x530ff8ee,0x468dde7d,0xd5730a1d,0x4cd04dc6,
     0x2939bbdb,0xa9ba4650,0xac9526e8,0xbe5ee304,0xa1fad5f0,0x6a2d519a,0x63ef8ce2,0x9a86ee22,
     0xc089c2b8,0x43242ef6,0xa51e03aa,0x9cf2d0a4,0x83c061ba,0x9be96a4d,0x8fe51550,0xba645bd6,
     0x2826a2f9,0xa73a3ae1,0x4ba99586,0xef5562e9,0xc72fefd3,0xf752f7da,0x3f046f69,0x77fa0a59,
     0x80e4a915,0x87b08601,0x9b09e6ad,0x3b3ee593,0xe990fd5a,0x9e34d797,0x2cf0b7d9,0x022b8b51,
     0x96d5ac3a,0x017da67d,0xd1cf3ed6,0x7c7d2d28,0x1f9f25cf,0xadf2b89b,0x5ad6b472,0x5a88f54c,
     0xe029ac71,0xe019a5e6,0x47b0acfd,0xed93fa9b,0xe8d3c48d,0x283b57cc,0xf8d56629,0x79132e28,
     0x785f0191,0xed756055,0xf7960e44,0xe3d35e8c,0x15056dd4,0x88f46dba,0x03a16125,0x0564f0bd,
     0xc3eb9e15,0x3c9057a2,0x97271aec,0xa93a072a,0x1b3f6d9b,0x1e6321f5,0xf59c66fb,0x26dcf319,
     0x7533d928,0xb155fdf5,0x03563482,0x8aba3cbb,0x28517711,0xc20ad9f8,0xabcc5167,0xccad925f,
     0x4de81751,0x3830dc8e,0x379d5862,0x9320f991,0xea7a90c2,0xfb3e7bce,0x5121ce64,0x774fbe32,
     0xa8b6e37e,0xc3293d46,0x48de5369,0x6413e680,0xa2ae0810,0xdd6db224,0x69852dfd,0x09072166,
     0xb39a460a,0x6445c0dd,0x586cdecf,0x1c20c8ae,0x5bbef7dd,0x1b588d40,0xccd2017f,0x6bb4e3bb,
     0xdda26a7e,0x3a59ff45,0x3e350a44,0xbcb4cdd5,0x72eacea8,0xfa6484bb,0x8d6612ae,0xbf3c6f47,
     0xd29be463,0x542f5d9e,0xaec2771b,0xf64e6370,0x740e0d8d,0xe75b1357,0xf8721671,0xaf537d5d,
     0x4040cb08,0x4eb4e2cc,0x34d2466a,0x0115af84,0xe1b00428,0x95983a1d,0x06b89fb4,0xce6ea048,
     0x6f3f3b82,0x3520ab82,0x011a1d4b,0x277227f8,0x611560b1,0xe7933fdc,0xbb3a792b,0x344525bd,
     0xa08839e1,0x51ce794b,0x2f32c9b7,0xa01fbac9,0xe01cc87e,0xbcc7d1f6,0xcf0111c3,0xa1e8aac7,
     0x1a908749,0xd44fbd9a,0xd0dadecb,0xd50ada38,0x0339c32a,0xc6913667,0x8df9317c,0xe0b12b4f,
     0xf79e59b7,0x43f5bb3a,0xf2d519ff,0x27d9459c,0xbf97222c,0x15e6fc2a,0x0f91fc71,0x9b941525,
     0xfae59361,0xceb69ceb,0xc2a86459,0x12baa8d1,0xb6c1075e,0xe3056a0c,0x10d25065,0xcb03a442,
     0xe0ec6e0e,0x1698db3b,0x4c98a0be,0x3278e964,0x9f1f9532,0xe0d392df,0xd3a0342b,0x8971f21e,
     0x1b0a7441,0x4ba3348c,0xc5be7120,0xc37632d8,0xdf359f8d,0x9b992f2e,0xe60b6f47,0x0fe3f11d,
     0xe54cda54,0x1edad891,0xce6279cf,0xcd3e7e6f,0x1618b166,0xfd2c1d05,0x848fd2c5,0xf6fb2299,
     0xf523f357,0xa6327623,0x93a83531,0x56cccd02,0xacf08162,0x5a75ebb5,0x6e163697,0x88d273cc,
     0xde966292,0x81b949d0,0x4c50901b,0x71c65614,0xe6c6c7bd,0x327a140a,0x45e1d006,0xc3f27b9a,
     0xc9aa53fd,0x62a80f00,0xbb25bfe2,0x35bdd2f6,0x71126905,0xb2040222,0xb6cbcf7c,0xcd769c2b,
     0x53113ec0,0x1640e3d3,0x38abbd60,0x2547adf0,0xba38209c,0xf746ce76,0x77afa1c5,0x20756060,
     0x85cbfe4e,0x8ae88dd8,0x7aaaf9b0,0x4cf9aa7e,0x1948c25c,0x02fb8a8c,0x01c36ae4,0xd6ebe1f9,
     0x90d4f869,0xa65cdea0,0x3f09252d,0xc208e69f,0xb74e6132,0xce77e25b,0x578fdfe3,0x3ac372e6}
};

// bcrypt 专用 base64 字母表
static const char BCryptB64[] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

static uint8_t bcrypt_b64_decode_char(char c) {
    if (c == '.') {
        return 0;
    }
    if (c == '/') {
        return 1;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 2;
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 28;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 54;
    }
    return 0;
}

static inline uint32_t BF_F(uint32_t x, const uint32_t S[4][256]) {
    return ((S[0][(x >> 24) & 0xFF] + S[1][(x >> 16) & 0xFF]) ^ S[2][(x >> 8) & 0xFF]) + S[3][x & 0xFF];
}

static void BF_encrypt(uint32_t& L, uint32_t& R, const uint32_t P[18], const uint32_t S[4][256]) {
    uint32_t Xl = L, Xr = R;
    for (int i = 0; i < 16; i++) {
        Xl ^= P[i];
        Xr ^= ((S[0][(Xl >> 24) & 0xFF] + S[1][(Xl >> 16) & 0xFF])
              ^ S[2][(Xl >> 8) & 0xFF]) + S[3][Xl & 0xFF];
        std::swap(Xl, Xr);
    }
    std::swap(Xl, Xr);
    Xr ^= P[16];
    Xl ^= P[17];
    L = Xl;
    R = Xr;
}

static void BF_keyschedule(const uint8_t* key, size_t key_len, uint32_t P[18], uint32_t S[4][256]) {
    // 拷贝原始常量
    memcpy(P, BF_P, sizeof(BF_P));
    memcpy(S, BF_S, sizeof(BF_S));

    // XOR key into P-array
    size_t ki = 0;
    for (int i = 0; i < 18; i++) {
        uint32_t data = 0;
        for (int j = 0; j < 4; j++) {
            data = (data << 8) | key[ki];
            ki = (ki + 1) % key_len;
        }
        P[i] ^= data;
    }

    // Encrypt zero block and replace P entries
    uint32_t L = 0, R = 0;
    for (int i = 0; i < 18; i += 2) {
        BF_encrypt(L, R, P, S);
        P[i] = L;
        P[i + 1] = R;
    }

    // Replace S-box entries
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 256; j += 2) {
            BF_encrypt(L, R, P, S);
            S[i][j] = L;
            S[i][j + 1] = R;
        }
    }
}

static void eksblowfish(const uint8_t* password, size_t pw_len, const uint8_t* salt, size_t salt_len
        , uint32_t rounds, uint32_t P[18], uint32_t S[4][256]) {
    BF_keyschedule(password, pw_len, P, S);

    for (uint32_t r = 0; r < rounds; r++) {
        // XOR salt（小端字节序，与标准 bcrypt 兼容）
        size_t si = 0;
        for (int i = 0; i < 18; i++) {
            uint32_t data = 0;
            for (int j = 0; j < 4; j++) {
                data = (data >> 8) | ((uint32_t)salt[si] << 24);
                si = (si + 1) % salt_len;
            }
            P[i] ^= data;
        }
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 256; j++) {
                uint32_t data = 0;
                for (int k = 0; k < 4; k++) {
                    data = (data >> 8) | ((uint32_t)salt[si] << 24);
                    si = (si + 1) % salt_len;
                }
                S[i][j] ^= data;
            }
        }

        // XOR password（大端字节序，与标准 Blowfish key schedule 一致）
        size_t pi = 0;
        for (int i = 0; i < 18; i++) {
            uint32_t data = 0;
            for (int j = 0; j < 4; j++) {
                data = (data << 8) | password[pi];
                pi = (pi + 1) % pw_len;
            }
            P[i] ^= data;
        }
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 256; j++) {
                uint32_t data = 0;
                for (int k = 0; k < 4; k++) {
                    data = (data << 8) | password[pi];
                    pi = (pi + 1) % pw_len;
                }
                S[i][j] ^= data;
            }
        }

        // Re-encrypt
        uint32_t L = 0, R = 0;
        for (int i = 0; i < 18; i += 2) {
            BF_encrypt(L, R, P, S);
            P[i] = L;
            P[i + 1] = R;
        }
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 256; j += 2) {
                BF_encrypt(L, R, P, S);
                S[i][j] = L;
                S[i][j + 1] = R;
            }
        }
    }
}

static void bcrypt_encode(std::string& out, const uint8_t* data, int len) {
    // 标准 bcrypt base64: 每 3 字节 → 4 字符，末尾不足 3 字节的组输出 2 或 3 字符
    for (int i = 0; i + 3 <= len; i += 3) {
        uint8_t b0 = data[i], b1 = data[i + 1], b2 = data[i + 2];
        out += BCryptB64[b0 >> 2];
        out += BCryptB64[((b0 << 4) | (b1 >> 4)) & 0x3f];
        out += BCryptB64[((b1 << 2) | (b2 >> 6)) & 0x3f];
        out += BCryptB64[b2 & 0x3f];
    }
    // 处理剩余字节
    int rem = len % 3;
    if (rem == 2) {
        uint8_t b0 = data[len - 2], b1 = data[len - 1];
        out += BCryptB64[b0 >> 2];
        out += BCryptB64[((b0 << 4) | (b1 >> 4)) & 0x3f];
        out += BCryptB64[(b1 << 2) & 0x3f];
    } else if (rem == 1) {
        uint8_t b0 = data[len - 1];
        out += BCryptB64[b0 >> 2];
        out += BCryptB64[(b0 << 4) & 0x3f];
    }
}

static bool bcrypt_decode(const char*& p, uint8_t* out, int len) {
    // 标准 bcrypt base64: 4 字符 → 3 字节
    int bytes = 0;
    while (bytes < len) {
        if (!*p) return false;
        int c1 = bcrypt_b64_decode_char(*p++);
        if (!*p) return false;
        int c2 = bcrypt_b64_decode_char(*p++);

        out[bytes++] = (uint8_t)(((c1 << 2) | (c2 >> 4)) & 0xff);
        if (bytes >= len) break;

        if (!*p) return false;
        int c3 = bcrypt_b64_decode_char(*p++);
        out[bytes++] = (uint8_t)(((c2 << 4) | (c3 >> 2)) & 0xff);
        if (bytes >= len) break;

        if (!*p) return false;
        int c4 = bcrypt_b64_decode_char(*p++);
        out[bytes++] = (uint8_t)(((c3 << 6) | c4) & 0xff);
    }
    return bytes == len;
}

std::string EncryptorUtil::BCryptHashWithSalt(const std::string& password, unsigned rounds
        , const uint8_t* salt, size_t saltLen) {
    if (rounds < 4) {
        rounds = 4;
    }
    if (rounds > 31) {
        rounds = 31;
    }

    uint32_t P[18], S[4][256];
    eksblowfish((const uint8_t*)password.c_str(), password.size(), salt, saltLen, (uint32_t)(1u << rounds), P, S);

    // 加密 "OrpheanBeholderScryDoubt"
    // bcrypt 标准要求 Blowfish 输入输出为 big-endian 字节序
    // x86 小端下需要做字节序转换，否则与 Python/OpenBSD 不兼容
    uint32_t ctext[6];
    memcpy(ctext, "OrpheanBeholderScryDoubt", 24);
    for (int i = 0; i < 6; i++) {
        ctext[i] = be32toh(ctext[i]);  // big-endian bytes → host uint32_t
    }
    for (int i = 0; i < 64; i++) {
        BF_encrypt(ctext[0], ctext[1], P, S);
        BF_encrypt(ctext[2], ctext[3], P, S);
        BF_encrypt(ctext[4], ctext[5], P, S);
    }
    for (int i = 0; i < 6; i++) {
        ctext[i] = htobe32(ctext[i]);  // host uint32_t → big-endian bytes
    }

    // 构建输出: $2b$<rounds>$<22-char-salt><31-char-hash>
    std::string result;
    result.reserve(60);
    result += "$2b$";
    if (rounds < 10) {
        result += '0';
    }
    result += std::to_string(rounds);
    result += '$';
    bcrypt_encode(result, salt, 16);

    uint8_t hash_bytes[24];
    memcpy(hash_bytes, ctext, 24);
    bcrypt_encode(result, hash_bytes, 23);
    return result;
}

std::string EncryptorUtil::BCryptHash(const std::string& password, unsigned rounds) {
    if (rounds < 4) rounds = 4;
    if (rounds > 31) rounds = 31;

    // 生成随机 salt（16 字节），然后用标准 bcrypt base64 编码
    // bcrypt base64 字母表: ./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789
    static const char B64[] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    uint8_t raw[16];
    if (!RAND_bytes(raw, sizeof(raw))) return "";

    std::string salt_str = "$2b$";
    if (rounds < 10) salt_str += '0';
    salt_str += std::to_string(rounds);
    salt_str += '$';

    // 16 字节 → 22 bcrypt base64 字符
    for (int i = 0; i < 16; i += 3) {
        uint8_t c1 = raw[i];
        uint8_t c2 = i + 1 < 16 ? raw[i + 1] : 0;
        salt_str += B64[c1 >> 2];
        salt_str += B64[((c1 << 4) | (c2 >> 4)) & 0x3f];
        if (i + 2 < 16) {
            uint8_t c3 = raw[i + 2];
            salt_str += B64[((c2 << 2) | (c3 >> 6)) & 0x3f];
            salt_str += B64[c3 & 0x3f];
        } else {
            // 最后 1 字节
            salt_str += B64[(c2 << 2) & 0x3f];
        }
    }

    // 用系统 crypt_r 生成 bcrypt hash
    struct crypt_data* pdata = (struct crypt_data*)malloc(sizeof(struct crypt_data));
    if (!pdata) return "";
    memset(pdata, 0, sizeof(*pdata));

    char* result = crypt_r(password.c_str(), salt_str.c_str(), pdata);
    if (!result) {
        free(pdata);
        return "";
    }
    std::string out(result);
    free(pdata);
    return out;
}

bool EncryptorUtil::BCryptVerify(const std::string& password, const std::string& hash) {
    if (hash.size() < 28 || hash[0] != '$' || hash[1] != '2' || hash[3] != '$') {
        return false;
    }

    struct crypt_data* pdata = (struct crypt_data*)malloc(sizeof(struct crypt_data));
    if (!pdata) return false;
    memset(pdata, 0, sizeof(*pdata));

    char* result = crypt_r(password.c_str(), hash.c_str(), pdata);
    if (!result) {
        free(pdata);
        return false;
    }
    bool ok = (hash == result);
    free(pdata);
    return ok;
}

} // namespace chen
