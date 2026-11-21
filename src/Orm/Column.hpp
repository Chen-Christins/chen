/**
 * @file Column.hpp
 * @brief 数据库的列
 * @author Christins
 * @date 2025-05-08
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <string>

#include <tinyxml2.h>

namespace chen {
namespace orm {

class Column {
    friend class Table;

public:
    typedef std::shared_ptr<Column> ptr;
    enum Type {
        TYPE_NULL = 0,
        TYPE_INT8,
        TYPE_UINT8,
        TYPE_INT16,
        TYPE_UINT16,
        TYPE_INT32,
        TYPE_UINT32,
        TYPE_FLOAT,
        TYPE_DOUBLE,
        TYPE_INT64,
        TYPE_UINT64,
        TYPE_STRING,
        TYPE_TEXT,
        TYPE_BLOB,
        TYPE_TIMESTAMP,
    };

    const std::string& getName() const { return name_; }
    const std::string& getType() const { return type_; }
    const std::string& getDesc() const { return desc_; }
    const std::string& getDefault() const { return default_; }

    std::string getDefaultValueString();
    std::string getSQLite3Default();
    std::string getMySQLDefault();
    std::string getPostgreSQLDefault();

    bool isAutoIncrement() const { return auto_increment_; }
    Type getDType() const { return dtype_; }

    bool init(const tinyxml2::XMLElement& node);

    std::string getMemberDefine() const;
    std::string getGetFunDefine() const;
    std::string getSetFunDefine() const;
    std::string getSetFunImpl(const std::string& class_name, int idx) const;
    int getIndex() const { return index_; }

    static Type ParseType(const std::string& v);
    static std::string TypeToString(Type type);

    std::string getDTypeString();
    std::string getSQLite3TypeString();
    std::string getMySQLTypeString();
    std::string getPostgreSQLTypeString();

    std::string getBindString();
    std::string getGetString();
    const std::string& getUpdate() const { return update_; }

private:
    std::string name_;
    std::string type_;
    std::string default_;
    std::string update_;
    std::string desc_;
    int index_;

    bool auto_increment_;
    Type dtype_;
    int length_;
};

} // namespace orm
} // namespace chen
