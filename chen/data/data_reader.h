/**
 * @file data_reader.h
 * @brief 策划表数据读取模块
 * @details 读取加密的bin格式策划表文件
 * @author Christins
 * @date 2024-11-06
 */
#pragma once

#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <openssl/md5.h>
#include <zlib.h>

#include "../log/log.h"
#include "data_decryptor.h"

namespace chen::data {

/**
 * @brief 字段值类型定义
 */
using FieldValue = std::variant<
    std::nullptr_t,      // NULL值
    int8_t,              // 8位整数
    int16_t,             // 16位整数
    int32_t,             // 32位整数
    int64_t,             // 64位整数
    float,               // 单精度浮点
    double,              // 双精度浮点
    std::string          // 字符串
>;

/**
 * @brief 行数据类型定义
 */
using RowData = std::map<std::string, FieldValue>;

/**
 * @brief 表数据类型定义
 */
class TableData {
public:
    typedef std::shared_ptr<TableData> ptr;

    /**
     * @brief 获取表名
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief 获取字段列表
     */
    const std::vector<std::string>& getHeaders() const { return m_headers; }

    /**
     * @brief 获取行数
     */
    size_t getRowCount() const { return m_rows.size(); }

    /**
     * @brief 获取指定行数据
     * @param row_index 行索引（从0开始）
     * @return 行数据指针，如果索引无效返回nullptr
     */
    const RowData* getRow(size_t row_index) const {
        if (row_index >= m_rows.size()) {
            return nullptr;
        }
        return &m_rows[row_index];
    }

    /**
     * @brief 获取指定单元格的值
     * @tparam T 返回值类型
     * @param row_index 行索引
     * @param column_name 列名
     * @param default_value 默认值
     * @return 字段值
     */
    template <typename T>
    T getValue(size_t row_index, const std::string& column_name, const T& default_value = T{}) const {
        if (row_index >= m_rows.size()) {
            return default_value;
        }

        const RowData& row = m_rows[row_index];
        auto it = row.find(column_name);
        if (it == row.end()) {
            return default_value;
        }

        try {
            return std::get<T>(it->second);
        } catch (const std::bad_variant_access&) {
            WARN(LOG_NAME("data")) << "Type mismatch for field " << column_name
                << " in table " << m_name << " row " << row_index;
            return default_value;
        }
    }

    /**
     * @brief 检查字段是否存在
     */
    bool hasField(const std::string& field_name) const {
        return std::find(m_headers.begin(), m_headers.end(), field_name) != m_headers.end();
    }

    /**
     * @brief 遍历所有行
     * @param callback 回调函数，参数为行索引和行数据
     */
    void forEachRow(std::function<void(size_t, const RowData&)> callback) const {
        for (size_t i = 0; i < m_rows.size(); ++i) {
            callback(i, m_rows[i]);
        }
    }

private:
    friend class DataReader;

    std::string m_name;                           // 表名
    std::vector<std::string> m_headers;           // 字段列表
    std::vector<RowData> m_rows;                  // 行数据
};

/**
 * @brief 数据读取器类
 */
class DataReader {
public:
    typedef std::shared_ptr<DataReader> ptr;

    /**
     * @brief 构造函数
     * @param encryption_type 加密类型
     * @param password 解密密码
     */
    DataReader(const std::string& encryption_type, const std::string& password);

    /**
     * @brief 析构函数
     */
    ~DataReader();

    /**
     * @brief 加载加密的bin文件
     * @param file_path 文件路径
     * @return 是否加载成功
     */
    bool loadFromFile(const std::string& file_path);

    /**
     * @brief 从内存数据加载
     * @param data 数据指针
     * @param size 数据大小
     * @return 是否加载成功
     */
    bool loadFromMemory(const uint8_t* data, size_t size);

    /**
     * @brief 获取表数据
     * @param table_name 表名
     * @return 表数据指针，如果表不存在返回nullptr
     */
    TableData::ptr getTable(const std::string& table_name) const;

    /**
     * @brief 获取所有表名
     * @return 表名列表
     */
    std::vector<std::string> getAllTableNames() const;

    /**
     * @brief 检查表是否存在
     * @param table_name 表名
     * @return 是否存在
     */
    bool hasTable(const std::string& table_name) const;

    /**
     * @brief 获取表数量
     */
    size_t getTableCount() const { return m_tables.size(); }

    /**
     * @brief 清空所有数据
     */
    void clear();

private:
    /**
     * @brief XOR解密
     */
    std::vector<uint8_t> _xorDecrypt(const uint8_t* data, size_t size);

    /**
     * @brief 解压缩数据
     */
    std::vector<uint8_t> _decompressData(const uint8_t* data, size_t size);

    /**
     * @brief 验证校验和
     */
    bool _verifyChecksum(const uint8_t* data, size_t size, const uint8_t* expected_checksum);

    /**
     * @brief 解析表数据
     */
    bool _parseTableData(const uint8_t* data, size_t size);

    /**
     * @brief 读取字段值
     */
    FieldValue _readFieldValue(const uint8_t*& ptr, const uint8_t* end);

    // 加密密钥
    static const uint32_t ENCRYPT_KEY = 0xABCD1234;

    // 文件头标识
    static const char FILE_HEADER[4];

    std::map<std::string, TableData::ptr> m_tables;  // 表数据映射
    std::unique_ptr<DataDecryptor> m_decryptor;      // 解密器
};

/**
 * @brief 字段值访问辅助类
 * @details 提供更便捷的字段访问方式
 */
class FieldAccessor {
public:
    FieldAccessor(const RowData* row, const std::string& field_name)
        : m_row(row), m_field_name(field_name) {}

    /**
     * @brief 获取值，支持隐式转换
     */
    template <typename T>
    operator T() const {
        if (!m_row) {
            return T{};
        }

        auto it = m_row->find(m_field_name);
        if (it == m_row->end()) {
            return T{};
        }

        try {
            return std::get<T>(it->second);
        } catch (const std::bad_variant_access&) {
            return T{};
        }
    }

    /**
     * @brief 获取值，带默认值
     */
    template <typename T>
    T get(const T& default_value = T{}) const {
        return static_cast<T>(*this);
    }

    /**
     * @brief 检查字段是否为空
     */
    bool isNull() const {
        if (!m_row) return true;

        auto it = m_row->find(m_field_name);
        if (it == m_row->end()) return true;

        return std::holds_alternative<std::nullptr_t>(it->second);
    }

private:
    const RowData* m_row;
    std::string m_field_name;
};

/**
 * @brief 行访问辅助类
 * @details 提供更便捷的行访问方式
 */
class RowAccessor {
public:
    RowAccessor(const TableData* table, size_t row_index)
        : m_table(table), m_row_index(row_index) {}

    /**
     * @brief 获取字段访问器
     */
    FieldAccessor operator[](const std::string& field_name) const {
        const RowData* row = m_table ? m_table->getRow(m_row_index) : nullptr;
        return FieldAccessor(row, field_name);
    }

    /**
     * @brief 获取原始行数据
     */
    const RowData* getRawRow() const {
        return m_table ? m_table->getRow(m_row_index) : nullptr;
    }

    /**
     * @brief 检查行是否有效
     */
    bool isValid() const {
        return m_table && m_row_index < m_table->getRowCount();
    }

private:
    const TableData* m_table;
    size_t m_row_index;
};

} // namespace chen::data
