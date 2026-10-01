/**
 * @file xml_converter.hpp
 * @brief Header file for XML to C++ header conversion utility.
 * @author Christins (chen.christins@icloud.com)
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
    bool isArray = false;
    std::string size;
    std::string desc;
    int lineNum = 0;
};

struct StructInfo {
    std::string name;
    std::string desc;
    bool needSerialization = false;
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

    std::string processTemplateType(const std::string& templateName, const std::string& type);

    std::string processTupleType(const std::string& type);

    std::string processVariantType(const std::string& type);

    std::vector<std::string> splitTemplateParams(const std::string& content) const;

    std::string locationStr() const;

    std::string toCamelCase(const std::string& input) const;

    std::string trim(const std::string& str) const;

    std::string replaceAll(const std::string& str, const std::string& from, const std::string& to) const;

    bool parseBoolAttr(const char* attrValue) const;

    std::string getScalarWriteExpr(const std::string& cppType, const std::string& valueExpr) const;

    std::string getScalarReadExpr(const std::string& cppType, const std::string& valueExpr) const;

    void generateStructSerialization(std::ofstream& out, const StructInfo& structInfo);

    bool parseXML(const std::string& filename);

    void generateHeader(const std::string& outputFile);

    std::string toUpper(const std::string& str);

    void parseIncludeFile(const std::string& includeFile, const std::string& prefix);

    std::string extractHeaderName(const std::string& xmlPath);

    bool isExternalType(const std::string& type);

    bool hasError() const;

    std::string decodeHTMLEntities(const std::string& str) const;
private:
    std::set<std::string> requiredHeaders;
    std::vector<MacroInfo> macroList;
    std::vector<MacroGroupInfo> macroGroupList;
    std::vector<StructInfo> structList;
    std::vector<UnionInfo> unionList;
    enum class ElementKind { MacroGroup, Struct, Union };
    struct ElementOrder {
        ElementKind kind;
        size_t index;
    };
    std::vector<ElementOrder> elementOrder;  // Preserve declaration order for non-macro elements
    std::string metalibDesc;
    std::string headerName;
    std::string headerDesc;
    std::set<std::string> includedTypes;  // 记录已包含的类型
    std::map<std::string, std::string> externalTypeMapping;  // 外部类型映射
    std::string currentXMLDir;  // 当前XML文件所在目录
    bool m_headerPack = false;  // 文件级别 pack 属性
    bool m_hasError = false;    // 转换是否出错
    std::set<std::string> m_reportedUnknownType;  // 已报告过的未知类型，避免重复
    std::string m_currentFile;
    int m_currentLine = 0;
};

} // namespace chen::converter
