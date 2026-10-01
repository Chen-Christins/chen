#include "data_reader.h"

#include <fstream>

#include <openssl/md5.h>

namespace chen::data {

static Logger::ptr logger = LOG_NAME("data");

// 文件头标识
const char DataReader::FILE_HEADER[4] = {'C', 'H', 'G', 'D'};

DataReader::DataReader(const std::string& encryption_type, const std::string& password)
    : m_decryptor(createDecryptor(encryption_type, password)) {
}

DataReader::~DataReader() {
    clear();
}

bool DataReader::loadFromFile(const std::string& file_path) {
    // 读取文件
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ERROR(logger) << "Failed to open file: " << file_path;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        ERROR(logger) << "Failed to read file: " << file_path;
        return false;
    }

    return loadFromMemory(buffer.data(), buffer.size());
}

bool DataReader::loadFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        ERROR(logger) << "Invalid data or size";
        return false;
    }

    try {
        std::vector<uint8_t> decrypted_data;

        // 使用新的解密器
        if (m_decryptor) {
            try {
                decrypted_data = m_decryptor->parseAndDecrypt(data, size);
            } catch (const std::exception& e) {
                ERROR(logger) << "Decryption failed: " << e.what();
                return false;
            }
        } else {
            // 降级到旧的XOR解密
            decrypted_data = _xorDecrypt(data, size);
        }

        // 验证文件大小
        if (decrypted_data.size() < 16) { // MD5(16) + 至少一些数据
            ERROR(logger) << "File too small after decryption";
            return false;
        }

        // 验证校验和
        const uint8_t* checksum = decrypted_data.data();
        const uint8_t* compressed_data = checksum + 16;
        size_t compressed_size = decrypted_data.size() - 16;

        if (!_verifyChecksum(compressed_data, compressed_size, checksum)) {
            ERROR(logger) << "Checksum verification failed";
            return false;
        }

        // 解压缩数据
        auto uncompressed_data = _decompressData(compressed_data, compressed_size);

        // 解析表数据
        return _parseTableData(uncompressed_data.data(), uncompressed_data.size());

    } catch (const std::exception& e) {
        ERROR(logger) << "Exception while loading data: " << e.what();
        return false;
    }
}

TableData::ptr DataReader::getTable(const std::string& table_name) const {
    auto it = m_tables.find(table_name);
    if (it != m_tables.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> DataReader::getAllTableNames() const {
    std::vector<std::string> names;
    names.reserve(m_tables.size());

    for (const auto& pair : m_tables) {
        names.push_back(pair.first);
    }

    return names;
}

bool DataReader::hasTable(const std::string& table_name) const {
    return m_tables.find(table_name) != m_tables.end();
}

void DataReader::clear() {
    m_tables.clear();
}

std::vector<uint8_t> DataReader::_xorDecrypt(const uint8_t* data, size_t size) {
    std::vector<uint8_t> result(size);
    uint32_t key = ENCRYPT_KEY;

    for (size_t i = 0; i < size; ++i) {
        result[i] = data[i] ^ ((key >> (8 * (i % 4))) & 0xFF);
    }

    return result;
}

std::vector<uint8_t> DataReader::_decompressData(const uint8_t* data, size_t size) {
    // 初始化zlib流
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = size;
    stream.next_in = const_cast<Bytef*>(data);

    if (inflateInit(&stream) != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib inflate");
    }

    // 估算解压后的大小（压缩数据的4倍）
    size_t estimated_size = size * 4;
    std::vector<uint8_t> result(estimated_size);
    stream.avail_out = estimated_size;
    stream.next_out = result.data();

    int ret = inflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        inflateEnd(&stream);
        throw std::runtime_error("Failed to decompress data");
    }

    size_t actual_size = estimated_size - stream.avail_out;
    inflateEnd(&stream);

    result.resize(actual_size);
    return result;
}

bool DataReader::_verifyChecksum(const uint8_t* data, size_t size, const uint8_t* expected_checksum) {
    MD5_CTX ctx;
    if (!MD5_Init(&ctx)) {
        return false;
    }

    if (!MD5_Update(&ctx, data, size)) {
        return false;
    }

    unsigned char computed_checksum[MD5_DIGEST_LENGTH];
    if (!MD5_Final(computed_checksum, &ctx)) {
        return false;
    }

    return memcmp(computed_checksum, expected_checksum, MD5_DIGEST_LENGTH) == 0;
}

bool DataReader::_parseTableData(const uint8_t* data, size_t size) {
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    // 检查最小文件大小
    if (size < 16) { // 文件头(4+4+4+4)
        ERROR(logger) << "File too small for header";
        return false;
    }

    // 检查文件头
    if (memcmp(ptr, FILE_HEADER, 4) != 0) {
        ERROR(logger) << "Invalid file header";
        return false;
    }
    ptr += 4;

    // 读取版本号
    uint32_t version = *reinterpret_cast<const uint32_t*>(ptr);
    ptr += 4;
    if (version != 1 && version != 2) {
        ERROR(logger) << "Unsupported version: " << version;
        return false;
    }

    INFO(logger) << "Loading data version: " << version;

    // 读取表数量
    uint32_t table_count = *reinterpret_cast<const uint32_t*>(ptr);
    ptr += 4;

    // 跳过保留字段
    ptr += 4;

    INFO(logger) << "Loading " << table_count << " tables";

    // 解析每个表
    for (uint32_t i = 0; i < table_count; ++i) {
        if (ptr + 4 > end) {
            ERROR(logger) << "Unexpected end of data while reading table size";
            return false;
        }

        // 读取表大小
        uint32_t table_size = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += 4;

        if (ptr + table_size > end) {
            ERROR(logger) << "Table size exceeds file size";
            return false;
        }

        // 解析表
        // const uint8_t* table_start = ptr;
        const uint8_t* table_end = ptr + table_size;

        // 读取表名
        if (ptr + 4 > table_end) {
            ERROR(logger) << "Unexpected end of table data while reading table name length";
            return false;
        }
        uint32_t name_len = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += 4;

        if (ptr + name_len > table_end) {
            ERROR(logger) << "Table name exceeds table size";
            return false;
        }
        std::string table_name(reinterpret_cast<const char*>(ptr), name_len);
        ptr += name_len;

        // 创建表数据对象
        auto table = std::make_shared<TableData>();
        table->m_name = table_name;

        // 读取字段数量
        if (ptr + 4 > table_end) {
            ERROR(logger) << "Unexpected end of table data while reading field count";
            return false;
        }
        uint32_t field_count = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += 4;

        // 读取字段名
        table->m_headers.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            if (ptr + 4 > table_end) {
                ERROR(logger) << "Unexpected end of table data while reading field name length";
                return false;
            }
            uint32_t field_name_len = *reinterpret_cast<const uint32_t*>(ptr);
            ptr += 4;

            if (ptr + field_name_len > table_end) {
                ERROR(logger) << "Field name exceeds table size";
                return false;
            }
            std::string field_name(reinterpret_cast<const char*>(ptr), field_name_len);
            ptr += field_name_len;

            table->m_headers.push_back(field_name);
        }

        // 读取行数
        if (ptr + 4 > table_end) {
            ERROR(logger) << "Unexpected end of table data while reading row count";
            return false;
        }
        uint32_t row_count = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += 4;

        // 读取行数据
        table->m_rows.reserve(row_count);
        for (uint32_t j = 0; j < row_count; ++j) {
            RowData row;

            // 读取每个字段
            for (const auto& field_name : table->m_headers) {
                if (ptr >= table_end) {
                    ERROR(logger) << "Unexpected end of table data while reading field value";
                    return false;
                }

                FieldValue value = _readFieldValue(ptr, table_end);
                row[field_name] = value;
            }

            table->m_rows.push_back(std::move(row));
        }

        // 添加到表映射
        m_tables[table_name] = table;

        // 移动到下一个表
        ptr = table_end;

        INFO(logger) << "Loaded table '" << table_name
            << "' with " << field_count << " fields and " << row_count << " rows";
    }

    return true;
}

FieldValue DataReader::_readFieldValue(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr >= end) {
        return nullptr;
    }

    uint8_t type = *ptr++;

    switch (type) {
    case 0: // NULL
        return nullptr;

    case 1: // int8
        if (ptr + 1 > end)
            return nullptr;
        return static_cast<int8_t>(*ptr++);

    case 2: // int16
        if (ptr + 2 > end)
            return nullptr;
        {
            int16_t value = *reinterpret_cast<const int16_t*>(ptr);
            ptr += 2;
            return value;
        }

    case 3: // int32
        if (ptr + 4 > end)
            return nullptr;
        {
            int32_t value = *reinterpret_cast<const int32_t*>(ptr);
            ptr += 4;
            return value;
        }

    case 4: // int64
        if (ptr + 8 > end)
            return nullptr;
        {
            int64_t value = *reinterpret_cast<const int64_t*>(ptr);
            ptr += 8;
            return value;
        }

    case 5: // double
        if (ptr + 8 > end)
            return nullptr;
        {
            double value = *reinterpret_cast<const double*>(ptr);
            ptr += 8;
            return value;
        }

    case 6: // short string
        if (ptr >= end)
            return nullptr;
        {
            uint8_t len = *ptr++;
            if (ptr + len > end)
                return nullptr;
            std::string value(reinterpret_cast<const char*>(ptr), len);
            ptr += len;
            return value;
        }

    case 7: // long string
        if (ptr + 4 > end)
            return nullptr;
        {
            uint32_t len = *reinterpret_cast<const uint32_t*>(ptr);
            ptr += 4;
            if (ptr + len > end)
                return nullptr;
            std::string value(reinterpret_cast<const char*>(ptr), len);
            ptr += len;
            return value;
        }

    default:
        WARN(logger) << "Unknown field type: " << static_cast<int>(type);
        return nullptr;
    }
}

} // namespace chen::data
