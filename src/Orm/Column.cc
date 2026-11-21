#include "Column.hpp"

#include "../Log/Log.hpp"
#include "Util.hpp"

namespace chen {
namespace orm {

static Logger::ptr logger = LOG_NAME("orm");

std::string Column::getDTypeString()
{
    return TypeToString(dtype_);
}

std::string Column::getDefaultValueString()
{
    if (default_.empty()) {
        return "";
    }
    if (dtype_ <= TYPE_DOUBLE || dtype_ == TYPE_INT64 || dtype_ == TYPE_UINT64) {
        return default_;
    }
    if (dtype_ <= TYPE_BLOB) {
        return "\"" + default_ + "\"";
    }
    if (default_ == "current_timestamp") {
        return "time(0)";
    }
    return "chen::TimeUtil::Str2Time(\"" + default_ + "\")";
}

std::string Column::getSQLite3Default()
{
    if (dtype_ <= TYPE_UINT64) {
        if (default_.empty()) {
            return "0";
        }
        return default_;
    }
    if (dtype_ <= TYPE_BLOB) {
        if (default_.empty()) {
            return "''";
        }
        return "'" + default_ + "'";
    }
    if (default_.empty()) {
        return "'1980-01-01 00:00:00'";
    }
    return default_;
}

std::string Column::getMySQLDefault()
{
    // MySQL DATETIME 支持 1970-01-01，TIMESTAMP 不支持，需保留 1980 哨兵
    if (dtype_ == TYPE_TIMESTAMP && type_ == "datetime") {
        if (default_.empty()) {
            return "'1970-01-01 00:00:00'";
        }
        return default_;
    }
    return getSQLite3Default();
}

namespace {

/**
 * @brief 转义写入生成代码 C++ 字符串字面量中的 SQL 字符串内容
 *
 * 生成代码形如 conn->execute("... DEFAULT 'value'")，因此需要同时处理
 * SQL 单引号（双写）和 C++ 字符串字面量中的反斜杠/双引号/换行。
 */
std::string escapePostgreSQLStringLiteral(const std::string& v)
{
    std::string out;
    out.reserve(v.size() + 8);
    for (char c : v) {
        switch (c) {
        case '\'':
            out += "''";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

} // namespace

std::string Column::getPostgreSQLDefault()
{
    if (dtype_ <= TYPE_UINT64) {
        if (default_.empty()) {
            return "0";
        }
        return default_;
    }
    if (dtype_ <= TYPE_BLOB) {
        std::string value = "'" + escapePostgreSQLStringLiteral(default_) + "'";
        if (dtype_ == TYPE_BLOB) {
            value += "::bytea";
        }
        return value;
    }
    if (default_.empty()) {
        return "'1970-01-01 00:00:00'";
    }
    if (default_ == "current_timestamp") {
        return "CURRENT_TIMESTAMP";
    }
    return default_;
}

bool Column::init(const tinyxml2::XMLElement& node)
{
    if (!node.Attribute("name")) {
        ERROR(logger) << "Column name not exists";
        return false;
    }

    name_ = node.Attribute("name");
    if (!node.Attribute("type")) {
        ERROR(logger) << "column name=" << name_ << " type is null";
        return false;
    }

    type_ = node.Attribute("type");
    dtype_ = ParseType(type_);

    if (dtype_ == TYPE_NULL) {
        ERROR(logger) << "column name=" << name_ << " type=" << type_ << " is invalid";
        return false;
    }

    if (node.Attribute("desc")) {
        desc_ = node.Attribute("desc");
    }

    if (node.Attribute("default")) {
        default_ = node.Attribute("default");
    }

    if (node.Attribute("update")) {
        update_ = node.Attribute("update");
    }

    if (node.Attribute("length")) {
        length_ = node.IntAttribute("length");
    } else {
        length_ = 0;
    }
    auto_increment_ = node.BoolAttribute("auto_increment", false);
    return true;
}

std::string Column::getMemberDefine() const
{
    std::stringstream ss;
    ss << TypeToString(dtype_) << " " << GetAsMemberName(name_) << ";" << std::endl;
    return ss.str();
}

std::string Column::getGetFunDefine() const
{
    std::stringstream ss;
    std::string desc = desc_.empty() ? name_ : desc_;
    ss << "/**" << std::endl;
    ss << "     * @brief 获取" << desc << std::endl;
    ss << "     * @return " << desc << std::endl;
    ss << "     */" << std::endl;
    ss << "    const " << TypeToString(dtype_) << "& " << GetAsGetFunName(name_) << "() { return "
       << GetAsMemberName(name_) << "; }" << std::endl;
    return ss.str();
}

std::string Column::getSetFunDefine() const
{
    std::stringstream ss;
    std::string desc = desc_.empty() ? name_ : desc_;
    ss << "/**" << std::endl;
    ss << "     * @brief 设置" << desc << std::endl;
    ss << "     * @param v " << desc << std::endl;
    ss << "     */" << std::endl;
    ss << "    void " << GetAsSetFunName(name_) << "(const " << TypeToString(dtype_) << "& v);" << std::endl;
    return ss.str();
}

std::string Column::getSetFunImpl(const std::string& class_name, int idx) const
{
    std::stringstream ss;
    ss << "void " << GetAsClassName(class_name) << "::" << GetAsSetFunName(name_) << "(const " << TypeToString(dtype_)
       << "& v) {" << std::endl;
    ss << "    " << GetAsMemberName(name_) << " = v;" << std::endl;
    ss << "    m_flags |= (1ull << " << idx << ");" << std::endl;
    ss << "}" << std::endl;
    return ss.str();
}

Column::Type Column::ParseType(const std::string& v)
{
#define XX(a, b, c)       \
    if (#b == v) {        \
        return a;         \
    } else if (#c == v) { \
        return a;         \
    }
    XX(TYPE_INT8, int8_t, int8);
    XX(TYPE_UINT8, uint8_t, uint8);
    XX(TYPE_INT16, int16_t, int16);
    XX(TYPE_UINT16, uint16_t, uint16);
    XX(TYPE_INT32, int32_t, int32);
    XX(TYPE_UINT32, uint32_t, uint32);
    XX(TYPE_INT64, int64_t, int64);
    XX(TYPE_UINT64, uint64_t, uint64);
    XX(TYPE_FLOAT, float, float);
    XX(TYPE_DOUBLE, double, double);
    XX(TYPE_STRING, string, std::string);
    XX(TYPE_TEXT, text, std::string);
    XX(TYPE_BLOB, blob, blob);
    XX(TYPE_TIMESTAMP, timestamp, datetime);
#undef XX
    return TYPE_NULL;
}

std::string Column::TypeToString(Type type)
{
#define XX(a, b)     \
    if (a == type) { \
        return #b;   \
    }
    XX(TYPE_INT8, int8_t);
    XX(TYPE_UINT8, uint8_t);
    XX(TYPE_INT16, int16_t);
    XX(TYPE_UINT16, uint16_t);
    XX(TYPE_INT32, int32_t);
    XX(TYPE_UINT32, uint32_t);
    XX(TYPE_INT64, int64_t);
    XX(TYPE_UINT64, uint64_t);
    XX(TYPE_FLOAT, float);
    XX(TYPE_DOUBLE, double);
    XX(TYPE_BLOB, std::string);
    XX(TYPE_TEXT, std::string);
    XX(TYPE_STRING, std::string);
    XX(TYPE_TIMESTAMP, int64_t);
#undef XX
    return "null";
}

std::string Column::getSQLite3TypeString()
{
#define XX(a, b)                                                                                                       \
    if (a == dtype_) {                                                                                                 \
        return #b;      \
    }
    XX(TYPE_INT8, INTEGER);
    XX(TYPE_UINT8, INTEGER);
    XX(TYPE_INT16, INTEGER);
    XX(TYPE_UINT16, INTEGER);
    XX(TYPE_INT32, INTEGER);
    XX(TYPE_UINT32, INTEGER);
    XX(TYPE_INT64, INTEGER);
    XX(TYPE_UINT64, INTEGER);
    XX(TYPE_FLOAT, REAL);
    XX(TYPE_DOUBLE, REAL);
    XX(TYPE_BLOB, BLOB);
    XX(TYPE_TEXT, TEXT);
    XX(TYPE_STRING, TEXT);
    XX(TYPE_TIMESTAMP, TIMESTAMP);
#undef XX
    return "";
}

std::string Column::getMySQLTypeString()
{
#define XX(a, b)                                                                                                       \
    if (a == dtype_) {                                                                                                 \
        return #b;      \
    }
    XX(TYPE_INT8, tinyint);
    XX(TYPE_UINT8, tinyint unsigned);
    XX(TYPE_INT16, smallint);
    XX(TYPE_UINT16, smallint unsigned);
    XX(TYPE_INT32, int);
    XX(TYPE_UINT32, int unsigned);
    XX(TYPE_INT64, bigint);
    XX(TYPE_UINT64, bigint unsigned);
    XX(TYPE_FLOAT, float);
    XX(TYPE_DOUBLE, double);
    XX(TYPE_BLOB, blob);
    XX(TYPE_TEXT, text);
#undef XX
    if (dtype_ == TYPE_TIMESTAMP) {
        return type_ == "datetime" ? "datetime" : "timestamp";
    }
    if (dtype_ == TYPE_STRING) {
        return "varchar(" + std::to_string(length_ ? length_ : 128) + ")";
    }
    return "";
}

std::string Column::getPostgreSQLTypeString()
{
#define XX(a, b)                                                                                                       \
    if (a == dtype_) {                                                                                                 \
        return #b;      \
    }
    XX(TYPE_INT8, smallint);
    XX(TYPE_UINT8, smallint);
    XX(TYPE_INT16, smallint);
    XX(TYPE_UINT16, integer);
    XX(TYPE_INT32, integer);
    XX(TYPE_UINT32, bigint);
    XX(TYPE_INT64, bigint);
    XX(TYPE_UINT64, numeric(20, 0));
    XX(TYPE_FLOAT, real);
    XX(TYPE_DOUBLE, double precision);
    XX(TYPE_BLOB, bytea);
    XX(TYPE_TEXT, text);
#undef XX
    if (dtype_ == TYPE_TIMESTAMP) {
        return "timestamp without time zone";
    }
    if (dtype_ == TYPE_STRING) {
        return "character varying(" + std::to_string(length_ ? length_ : 128) + ")";
    }
    return "";
}

std::string Column::getBindString()
{
#define XX(a, b)                                                                                                       \
    if (a == dtype_) {                                                                                                 \
        return "bind" #b; \
    }
    XX(TYPE_INT8, Int8);
    XX(TYPE_UINT8, Uint8);
    XX(TYPE_INT16, Int16);
    XX(TYPE_UINT16, Uint16);
    XX(TYPE_INT32, Int32);
    XX(TYPE_UINT32, Uint32);
    XX(TYPE_INT64, Int64);
    XX(TYPE_UINT64, Uint64);
    XX(TYPE_FLOAT, Float);
    XX(TYPE_DOUBLE, Double);
    XX(TYPE_BLOB, Blob);
    XX(TYPE_STRING, String);
    XX(TYPE_TEXT, String);
    XX(TYPE_TIMESTAMP, Time);
#undef XX
    return "";
}

std::string Column::getGetString()
{
#define XX(a, b)                                                                                                       \
    if (a == dtype_) {                                                                                                 \
        return "get" #b; \
    }
    XX(TYPE_INT8, Int8);
    XX(TYPE_UINT8, Uint8);
    XX(TYPE_INT16, Int16);
    XX(TYPE_UINT16, Uint16);
    XX(TYPE_INT32, Int32);
    XX(TYPE_UINT32, Uint32);
    XX(TYPE_INT64, Int64);
    XX(TYPE_UINT64, Uint64);
    XX(TYPE_FLOAT, Float);
    XX(TYPE_DOUBLE, Double);
    XX(TYPE_BLOB, Blob);
    XX(TYPE_STRING, String);
    XX(TYPE_TEXT, String);
    XX(TYPE_TIMESTAMP, Time);
#undef XX
    return "";
}

} // namespace orm
} // namespace chen
