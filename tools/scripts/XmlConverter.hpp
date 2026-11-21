/**
 * @file XmlConverter.hpp
 * @brief Header file for XML to C++ header conversion utility.
 * @author Christins (chen.christins@qq.com)
 * @date 2026-08-09
 * @copyright GPL-3.0
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace chen::converter {

struct MacroInfo {
    std::string name;
    std::string value;
    std::string type;
    std::string desc;
};

struct MacroGroupInfo {
    std::string name;
    std::string desc;
    std::vector<MacroInfo> macros;
};

struct EntryInfo {
    std::string name;
    std::string type;
    bool is_array = false;
    std::string size;
    std::string desc;
    int line_num = 0;
};

struct StructInfo {
    std::string name;
    std::string desc;
    bool need_serialization = false;
    bool pack = false;
    std::vector<EntryInfo> entries;
};

struct UnionInfo {
    std::string name;
    std::string desc;
    std::vector<EntryInfo> entries;
};

class XMLToHeaderConverter {
public:
    XMLToHeaderConverter() = default;

    std::string convertType(const std::string& type);

    std::string processTemplateType(const std::string& template_name, const std::string& type);

    std::string processTupleType(const std::string& type);

    std::string processVariantType(const std::string& type);

    std::vector<std::string> splitTemplateParams(const std::string& content) const;

    std::string locationStr() const;

    std::string toCamelCase(const std::string& input) const;

    std::string trim(const std::string& str) const;

    std::string replaceAll(const std::string& str, const std::string& from, const std::string& to) const;

    bool parseBoolAttr(const char* attr_value) const;

    std::string getScalarWriteExpr(const std::string& cpp_type, const std::string& value_expr) const;

    std::string getScalarReadExpr(const std::string& cpp_type, const std::string& value_expr) const;

    void generateStructSerialization(std::ofstream& out, const StructInfo& struct_info);

    bool parseXML(const std::string& filename);

    void generateHeader(const std::string& output_file);

    std::string toUpper(const std::string& str);

    void parseIncludeFile(const std::string& include_file, const std::string& prefix);

    std::string extractHeaderName(const std::string& xml_path);

    bool isExternalType(const std::string& type);

    bool hasError() const;

    std::string decodeHTMLEntities(const std::string& str) const;

private:
    std::set<std::string> required_headers_;
    std::vector<MacroInfo> macro_list_;
    std::vector<MacroGroupInfo> macro_group_list_;
    std::vector<StructInfo> struct_list_;
    std::vector<UnionInfo> union_list_;
    enum class ElementKind {
        MacroGroup,
        Struct,
        Union
    };
    struct ElementOrder {
        ElementKind kind;
        size_t index;
    };
    std::vector<ElementOrder> element_order_; // Preserve declaration order for non-macro elements
    std::string metalib_desc_;
    std::string header_name_;
    std::string header_desc_;
    std::set<std::string> included_types_;                     // 记录已包含的类型
    std::map<std::string, std::string> external_type_mapping_; // 外部类型映射
    std::string current_xml_dir_;                              // 当前XML文件所在目录
    bool header_pack_ = false;                                 // 文件级别 pack 属性
    bool has_error_ = false;                                   // 转换是否出错
    std::set<std::string> reported_unknown_type_;              // 已报告过的未知类型，避免重复
    std::string current_file_;
    int current_line_ = 0;
};

} // namespace chen::converter
