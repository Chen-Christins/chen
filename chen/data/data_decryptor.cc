#include "data_decryptor.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

#include "../util/encryptor_util.h"

namespace chen::data {

DataDecryptor::DataDecryptor(EncryptionType type, const std::string& password)
        : m_type(type)
        , m_password(password) {
    // 初始化OpenSSL
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
}

DataDecryptor::~DataDecryptor() {
    // 清理OpenSSL
    EVP_cleanup();
    ERR_free_strings();
}

std::vector<uint8_t> DataDecryptor::deriveKey(const std::string& password
        , const std::vector<uint8_t>& salt, size_t key_length, int iterations) {
    std::vector<uint8_t> key(key_length);

    // 使用PKCS5_PBKDF2_HMAC进行密钥派生
    if (!PKCS5_PBKDF2_HMAC(password.c_str(), password.length(), salt.data(), salt.size(), iterations, EVP_sha256(),
                           key_length, key.data())) {
        handleOpenSSLError("PBKDF2 key derivation failed");
        throw std::runtime_error("Failed to derive key");
    }

    return key;
}

std::vector<uint8_t> DataDecryptor::decryptAES128GCM(const std::vector<uint8_t>& ciphertext
        , const std::vector<uint8_t>& key) {
    if (key.size() != 16) {
        throw std::invalid_argument("AES-128-GCM requires 16-byte key");
    }

    if (ciphertext.size() < 28) { // 12字节nonce + 至少16字节tag
        throw std::invalid_argument("Ciphertext too short for AES-128-GCM");
    }

    // 提取nonce, ciphertext, tag
    std::string nonce(ciphertext.begin(), ciphertext.begin() + 12);
    std::string tag(ciphertext.end() - 16, ciphertext.end());
    std::string ct(ciphertext.begin() + 12, ciphertext.end() - 16);
    std::string key_str(key.begin(), key.end());

    // 使用新的EncryptorUtil进行解密
    GCMResult result = chen::EncryptorUtil::DecryptAES128GCM(ct, tag, key_str, nonce);

    if (!result.success) {
        throw std::runtime_error("AES-128-GCM decryption failed: " + result.error);
    }

    return std::vector<uint8_t>(result.data.begin(), result.data.end());
}

std::vector<uint8_t> DataDecryptor::decryptAES256GCM(const std::vector<uint8_t>& ciphertext
        , const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        throw std::invalid_argument("AES-256-GCM requires 32-byte key");
    }

    if (ciphertext.size() < 28) { // 12字节nonce + 至少16字节tag
        throw std::invalid_argument("Ciphertext too short for AES-256-GCM");
    }

    // 提取nonce, ciphertext, tag
    std::string nonce(ciphertext.begin(), ciphertext.begin() + 12);
    std::string tag(ciphertext.end() - 16, ciphertext.end());
    std::string ct(ciphertext.begin() + 12, ciphertext.end() - 16);
    std::string key_str(key.begin(), key.end());

    // 使用新的EncryptorUtil进行解密
    GCMResult result = chen::EncryptorUtil::DecryptAES256GCM(ct, tag, key_str, nonce);

    if (!result.success) {
        throw std::runtime_error("AES-256-GCM decryption failed: " + result.error);
    }

    return std::vector<uint8_t>(result.data.begin(), result.data.end());
}

std::vector<uint8_t> DataDecryptor::decryptChaCha20Poly1305(const std::vector<uint8_t>& ciphertext
        , const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        throw std::invalid_argument("ChaCha20-Poly1305 requires 32-byte key");
    }

    if (ciphertext.size() < 28) { // 12字节nonce + 至少16字节tag
        throw std::invalid_argument("Ciphertext too short for ChaCha20-Poly1305");
    }

    // 提取nonce, ciphertext, tag
    std::vector<uint8_t> nonce(ciphertext.begin(), ciphertext.begin() + 12);
    std::vector<uint8_t> tag(ciphertext.end() - 16, ciphertext.end());
    std::vector<uint8_t> ct(ciphertext.begin() + 12, ciphertext.end() - 16);

    // 验证HMAC-SHA256认证标签
    std::vector<uint8_t> mac_key = computeHMACSHA256(key, nonce);
    std::vector<uint8_t> nonce_ct_data = nonce;
    nonce_ct_data.insert(nonce_ct_data.end(), ct.begin(), ct.end());
    std::vector<uint8_t> computed_tag = computeHMACSHA256(mac_key, nonce_ct_data);
    computed_tag.resize(16); // 只取前16字节

    if (!verifyHMACSHA256(mac_key, nonce_ct_data, tag)) {
        throw std::runtime_error("Authentication failed - data may be tampered");
    }

    // 解密ChaCha20
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>
        ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);

    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }

    // 初始化解密
    // ChaCha20的nonce长度必须是12字节
    if (nonce.size() != 12) {
        throw std::invalid_argument("ChaCha20 requires 12-byte nonce");
    }

    if (EVP_DecryptInit_ex(ctx.get(), EVP_chacha20(), nullptr, nullptr, nullptr) != 1) {
        handleOpenSSLError("Failed to initialize ChaCha20 decryption");
        throw std::runtime_error("Failed to initialize decryption");
    }

    // 设置密钥和nonce（ChaCha20直接在初始化时设置nonce）
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
        handleOpenSSLError("Failed to set key and nonce");
        throw std::runtime_error("Failed to set key and nonce");
    }

    // 解密数据
    std::vector<uint8_t> plaintext(ct.size());
    int len;
    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, ct.data(), ct.size()) != 1) {
        handleOpenSSLError("Failed to decrypt data");
        throw std::runtime_error("Failed to decrypt data");
    }

    // 完成解密
    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len) != 1) {
        handleOpenSSLError("Failed to finalize decryption");
        throw std::runtime_error("Failed to finalize decryption");
    }

    plaintext.resize(ct.size());
    return plaintext;
}

std::vector<uint8_t> DataDecryptor::decryptCustom(const std::vector<uint8_t>& ciphertext
        , const std::vector<uint8_t>& key) {
    if (key.size() < 16) {
        throw std::invalid_argument("Custom decryption requires at least 16-byte key");
    }

    if (ciphertext.size() < 32) {
        throw std::invalid_argument("Ciphertext too short for custom decryption");
    }

    // 分离数据和认证标签
    std::vector<uint8_t> data(ciphertext.begin(), ciphertext.end() - 32);
    std::vector<uint8_t> received_tag(ciphertext.end() - 32, ciphertext.end());

    // 验证HMAC
    std::vector<uint8_t> hmac_key_data(key.begin(), key.begin() + 16);
    uint8_t hmac_suffix[] = {'h', 'm', 'a', 'c'};
    hmac_key_data.insert(hmac_key_data.end(), hmac_suffix, hmac_suffix + 4);
    std::vector<uint8_t> hmac_key = computeHMACSHA256(key, hmac_key_data);
    if (!verifyHMACSHA256(hmac_key, data, received_tag)) {
        throw std::runtime_error("Authentication failed - data may be tampered");
    }

    // 反向第三轮：密钥流混淆
    std::vector<uint8_t> stream_key_data(key.begin(), key.begin() + 16);
    uint8_t stream_suffix[] = {'s', 't', 'r', 'e', 'a', 'm'};
    stream_key_data.insert(stream_key_data.end(), stream_suffix, stream_suffix + 6);
    std::vector<uint8_t> stream_key = computeHMACSHA256(key, stream_key_data);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= stream_key[i % stream_key.size()];
    }

    // 反向第二轮：字节移位
    uint8_t shift = key[0] % 8;
    if (shift > 0) {
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = ((data[i] >> shift) | (data[i] << (8 - shift))) & 0xFF;
        }
    }

    // 反向第一轮：XOR加密
    size_t key_len = key.size();
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % key_len];
    }

    return data;
}

std::vector<uint8_t> DataDecryptor::decryptXOR(const std::vector<uint8_t>& data, uint32_t key) {
    std::vector<uint8_t> result(data.size());

    for (size_t i = 0; i < data.size(); ++i) {
        result[i] = data[i] ^ ((key >> (8 * (i % 4))) & 0xFF);
    }

    return result;
}

std::vector<uint8_t> DataDecryptor::parseAndDecrypt(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        throw std::invalid_argument("Invalid data or size");
    }

    // 版本1格式：直接XOR加密的数据
    if (m_type == EncryptionType::XOR) {
        return decryptXOR(std::vector<uint8_t>(data, data + size), 0xABCD1234);
    }

    // 版本2格式：包含元数据的加密数据
    if (size < 4) {
        throw std::invalid_argument("Data too small for metadata");
    }

    // 读取元数据长度
    uint32_t metadata_len = *reinterpret_cast<const uint32_t*>(data);
    if (size < 4 + metadata_len) {
        throw std::invalid_argument("Data too small for metadata");
    }

    // 解析元数据（简单的JSON解析）
    std::string metadata_json(reinterpret_cast<const char*>(data + 4), metadata_len);

    // 手动解析JSON（简化版）
    // 使用构造函数指定的加密类型，而不是从文件中读取
    EncryptionType enc_type = m_type;
    std::map<std::string, std::string> metadata_map;

    // 查找metadata字段
    size_t metadata_pos = metadata_json.find("\"metadata\":");
    if (metadata_pos != std::string::npos) {
        size_t obj_start = metadata_json.find("{", metadata_pos);
        if (obj_start != std::string::npos) {
            size_t obj_end = metadata_json.find("}", obj_start);
            if (obj_end != std::string::npos) {
                std::string metadata_obj = metadata_json.substr(obj_start + 1, obj_end - obj_start - 1);

                // 简单解析键值对
                size_t pos = 0;
                while (pos < metadata_obj.length()) {
                    size_t key_start = metadata_obj.find("\"", pos);
                    if (key_start == std::string::npos)
                        break;
                    size_t key_end = metadata_obj.find("\"", key_start + 1);
                    if (key_end == std::string::npos)
                        break;

                    std::string key = metadata_obj.substr(key_start + 1, key_end - key_start - 1);

                    size_t colon_pos = metadata_obj.find(":", key_end);
                    if (colon_pos == std::string::npos)
                        break;

                    size_t value_start = metadata_obj.find("\"", colon_pos);
                    size_t value_end = 0;
                    std::string value;

                    if (value_start != std::string::npos && value_start < metadata_obj.find(",", colon_pos)) {
                        // 字符串值
                        value_start++;
                        value_end = metadata_obj.find("\"", value_start);
                        if (value_end != std::string::npos) {
                            value = metadata_obj.substr(value_start, value_end - value_start);
                        }
                    } else {
                        // 布尔或其他值
                        value_start = metadata_obj.find_first_not_of(" \t", colon_pos + 1);
                        if (value_start != std::string::npos) {
                            value_end = metadata_obj.find_first_of(",}", value_start);
                            if (value_end == std::string::npos)
                                value_end = metadata_obj.length();
                            value = metadata_obj.substr(value_start, value_end - value_start);
                        }
                    }

                    metadata_map[key] = value;
                    pos = value_end + 1;
                }
            }
        }
    }

    // 获取加密数据
    const uint8_t* encrypted_data = data + 4 + metadata_len;
    size_t encrypted_size = size - 4 - metadata_len;

    std::vector<uint8_t> encrypted_vec(encrypted_data, encrypted_data + encrypted_size);

    // 根据类型解密
    switch (enc_type) {
    case EncryptionType::NONE:
        return encrypted_vec;

    case EncryptionType::XOR: {
        uint32_t xor_key = std::stoul(metadata_map["xor_key"]);
        return decryptXOR(encrypted_vec, xor_key);
    }

    case EncryptionType::AES128_GCM: {
        std::vector<uint8_t> key;
        if (metadata_map["derived"] == "true") {
            std::vector<uint8_t> salt = hexStringToBytes(metadata_map["salt"]);
            key = deriveKey(m_password, salt, 16);
        } else {
            throw std::runtime_error("Direct key not supported for AES-128-GCM");
        }
        return decryptAES128GCM(encrypted_vec, key);
    }

    case EncryptionType::AES256_GCM: {
        std::vector<uint8_t> key;
        if (metadata_map["derived"] == "true") {
            std::vector<uint8_t> salt = hexStringToBytes(metadata_map["salt"]);
            key = deriveKey(m_password, salt, 32);
        } else {
            throw std::runtime_error("Direct key not supported for AES-256-GCM");
        }
        return decryptAES256GCM(encrypted_vec, key);
    }

    case EncryptionType::CHACHA20_POLY1305: {
        std::vector<uint8_t> key;
        if (metadata_map["derived"] == "true") {
            std::vector<uint8_t> salt = hexStringToBytes(metadata_map["salt"]);
            key = deriveKey(m_password, salt, 32);
        } else {
            throw std::runtime_error("Direct key not supported for ChaCha20-Poly1305");
        }
        return decryptChaCha20Poly1305(encrypted_vec, key);
    }

    case EncryptionType::CUSTOM: {
        std::vector<uint8_t> key;
        if (metadata_map["derived"] == "true") {
            std::vector<uint8_t> pwd_data(m_password.begin(), m_password.end());
            std::vector<uint8_t> empty_data;
            key = computeHMACSHA256(pwd_data, empty_data);
        } else {
            throw std::runtime_error("Direct key not supported for custom encryption");
        }
        return decryptCustom(encrypted_vec, key);
    }

    default:
        throw std::runtime_error("Unsupported encryption type");
    }
}

std::vector<uint8_t> DataDecryptor::computeHMACSHA256(const std::vector<uint8_t>& key
        , const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hmac(SHA256_DIGEST_LENGTH);
    unsigned int hmac_len;

    if (!HMAC(EVP_sha256(), key.data(), key.size(),
              data.data(), data.size(),
              hmac.data(), &hmac_len)) {
        throw std::runtime_error("Failed to compute HMAC");
    }

    return hmac;
}

bool DataDecryptor::verifyHMACSHA256(const std::vector<uint8_t>& key
        , const std::vector<uint8_t>& data, const std::vector<uint8_t>& hmac) {
    try {
        std::vector<uint8_t> computed_hmac = computeHMACSHA256(key, data);
        return CRYPTO_memcmp(computed_hmac.data(), hmac.data(), std::min(computed_hmac.size(), hmac.size())) == 0;
    } catch (...) {
        return false;
    }
}

std::vector<uint8_t> DataDecryptor::hexStringToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;

    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }

    return bytes;
}

void DataDecryptor::handleOpenSSLError(const std::string& msg) {
    unsigned long err_code;
    std::string error_str;

    while ((err_code = ERR_get_error()) != 0) {
        char err_buf[256];
        ERR_error_string_n(err_code, err_buf, sizeof(err_buf));
        if (!error_str.empty()) {
            error_str += "; ";
        }
        error_str += err_buf;
    }

    if (!error_str.empty()) {
        throw std::runtime_error(msg + ": " + error_str);
    }
}

std::unique_ptr<DataDecryptor> createDecryptor(const std::string& type_name, const std::string& password) {
    std::map<std::string, EncryptionType> type_map = {
        {"none", EncryptionType::NONE},
        {"xor", EncryptionType::XOR},
        {"aes128_gcm", EncryptionType::AES128_GCM},
        {"aes256_gcm", EncryptionType::AES256_GCM},
        {"chacha20_poly1305", EncryptionType::CHACHA20_POLY1305},
        {"custom", EncryptionType::CUSTOM}
    };

    auto it = type_map.find(type_name);
    EncryptionType type = (it != type_map.end()) ? it->second : EncryptionType::AES256_GCM;

    return std::make_unique<DataDecryptor>(type, password);
}

} // namespace chen::data
