#include "XmlConverter.hpp"

#include <src/Log/Log.hpp>
#include <src/Util/FSUtil.hpp>
#include <src/Util/Macro.hpp>
#include <tinyxml2.h>

namespace chen::converter {

static Logger::ptr logger = LOG_NAME("converter");

std::string XMLToHeaderConverter::convertType(const std::string& type)
{
    if (type.empty()) {
        if (reported_unknown_type_.insert(type).second) {
            ERROR(logger) << COLOR_RED << locationStr() << " Empty type (missing 'type' attribute)" << COLOR_RESET;
        }
        has_error_ = true;
        return type;
    }
    // Handle square bracket syntax [type] -> <type>
    if (type.find('[') != std::string::npos && type.find(']') != std::string::npos) {
        std::string converted = replaceAll(type, "[", "<");
        converted = replaceAll(converted, "]", ">");
        return convertType(converted); // Recursive call to process the converted type
    }

    // Basic types mapping
    static const std::map<std::string, std::string> basic_types = {
        {"int8", "int8_t"},
        {"uint8", "uint8_t"},
        {"int16", "int16_t"},
        {"uint16", "uint16_t"},
        {"int32", "int32_t"},
        {"uint32", "uint32_t"},
        {"int64", "int64_t"},
        {"uint64", "uint64_t"},
        {"int", "int32_t"},
        {"uint", "uint32_t"},
        {"short", "int16_t"},
        {"ushort", "uint16_t"},
        {"smallint", "int16_t"},
        {"smalluint", "uint16_t"},
        {"char", "int8_t"},
        {"byte", "int8_t"},
        {"tinyint", "int8_t"},
        {"tinyuint", "uint8_t"},
        {"ubyte", "uint8_t"},
        {"bigint", "int64_t"},
        {"biguint", "uint64_t"},
        {"bool", "bool"},
        {"string", "std::string"},
        {"stringvar", "std::string"},
        {"number", "int32_t"},
        {"float", "float"},
        {"double", "double"},
        {"decimal", "double"},
        {"varint", "int32_t"},
        {"timestamp", "int64_t"},
        {"datetime", "int64_t"},
        {"bytes", "std::vector<uint8_t>"},
        {"blob", "std::vector<uint8_t>"},
    };

    auto it = basic_types.find(type);
    if (it != basic_types.end()) {
        static const std::set<std::string> cstdint_based_types = {"int", "uint", "short", "ushort", "char", "byte",
            "ubyte", "tinyint",  "tinyuint", "bigint",    "biguint", "number", "varint", "timestamp",
                                                                "datetime", "smallint", "smalluint", "int8",    "uint8",  "int16",  "uint16",
                                                                "int32",    "uint32",   "int64",     "uint64"};

        if (type == "string" || type == "stringvar") {
            required_headers_.insert("<string>");
        } else if (cstdint_based_types.count(type) > 0) {
            required_headers_.insert("<cstdint>");
        }

        // Add headers for specific types
        if (type == "bool") {
            // required_headers_.insert("<stdbool.h>");
        } else if (type == "bytes" || type == "blob") {
            required_headers_.insert("<vector>");
        }
        return it->second;
    }

    // Template types
    if (type.substr(0, 6) == "array<") {
        required_headers_.insert("<array>");
        return processTemplateType("std::array", type);
    }
    if (type.substr(0, 4) == "map<") {
        required_headers_.insert("<map>");
        return processTemplateType("std::map", type);
    }
    if (type.substr(0, 4) == "set<") {
        required_headers_.insert("<set>");
        return processTemplateType("std::set", type);
    }
    if (type.substr(0, 5) == "pair<") {
        required_headers_.insert("<utility>");
        return processTemplateType("std::pair", type);
    }
    if (type.substr(0, 5) == "list<") {
        required_headers_.insert("<list>");
        return processTemplateType("std::list", type);
    }
    if (type.substr(0, 6) == "tuple<") {
        required_headers_.insert("<tuple>");
        return processTupleType(type);
    }
    if (type.substr(0, 9) == "optional<") {
        required_headers_.insert("<optional>");
        return processTemplateType("std::optional", type);
    }
    if (type.substr(0, 8) == "variant<") {
        required_headers_.insert("<variant>");
        return processVariantType(type);
    }
    if (type.substr(0, 7) == "vector<") {
        required_headers_.insert("<vector>");
        return processTemplateType("std::vector", type);
    }

    // Custom types (enums, structs, unions) - check external types first
    if (isExternalType(type)) {
        return "tag" + toCamelCase(type);
    }

    for (const auto& group : macro_group_list_) {
        if (group.name == type) {
            return "tag" + toCamelCase(type);
        }
    }
    for (const auto& struct_info : struct_list_) {
        if (struct_info.name == type) {
            return "tag" + toCamelCase(type);
        }
    }
    for (const auto& union_info : union_list_) {
        if (union_info.name == type) {
            return "tag" + toCamelCase(type);
        }
    }

    // Default fallback - try to convert basic types again for edge cases
    std::string trimmed_type = trim(type);
    auto basic_it = basic_types.find(trimmed_type);
    if (basic_it != basic_types.end()) {
        static const std::set<std::string> cstdint_based_types = {"int", "uint", "short", "ushort", "char", "byte",
            "ubyte", "tinyint", "tinyuint",  "smallint", "smalluint", "bigint", "biguint", "number",
                                                                "varint",  "timestamp", "datetime", "int8",      "uint8",  "int16",   "uint16",
                                                                "int32",   "uint32",    "int64",    "uint64"};

        if (trimmed_type == "string" || trimmed_type == "stringvar") {
            required_headers_.insert("<string>");
        } else if (cstdint_based_types.count(trimmed_type) > 0) {
            required_headers_.insert("<cstdint>");
        }

        if (trimmed_type == "bool") {
            required_headers_.insert("<stdbool.h>");
        } else if (trimmed_type == "bytes" || trimmed_type == "blob") {
            required_headers_.insert("<vector>");
        }
        return basic_it->second;
    }

    if (reported_unknown_type_.insert(type).second) {
        ERROR(logger) << COLOR_RED << locationStr() << " Unknown type: " << type << COLOR_RESET;
    }
    has_error_ = true;
    return type;
}

std::string XMLToHeaderConverter::processTemplateType(const std::string& template_name, const std::string& type)
{
    std::string content = type.substr(type.find('<') + 1, type.rfind('>') - type.find('<') - 1);
    std::vector<std::string> params = splitTemplateParams(content);

    std::string result = template_name + "<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        std::string param = convertType(trim(params[i]));
        if (has_error_) {
            break;
        }
        // Check if this parameter is a macro name
        for (const auto& macro : macro_list_) {
            if (macro.name == trim(params[i])) {
                param = toUpper(macro.name);
                break;
            }
        }
        result += param;
    }
    result += ">";
    return result;
}

std::string XMLToHeaderConverter::processTupleType(const std::string& type)
{
    std::string content = type.substr(type.find('<') + 1, type.rfind('>') - type.find('<') - 1);
    std::vector<std::string> params = splitTemplateParams(content);

    std::string result = "std::tuple<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        std::string param = convertType(trim(params[i]));
        if (has_error_) {
            break;
        }
        // Check if this parameter is a macro name
        for (const auto& macro : macro_list_) {
            if (macro.name == trim(params[i])) {
                param = toUpper(macro.name);
                break;
            }
        }
        result += param;
    }
    result += ">";
    return result;
}

std::string XMLToHeaderConverter::processVariantType(const std::string& type)
{
    std::string content = type.substr(type.find('<') + 1, type.rfind('>') - type.find('<') - 1);
    std::vector<std::string> params = splitTemplateParams(content);

    std::string result = "std::variant<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        std::string param = convertType(trim(params[i]));
        if (has_error_) {
            break;
        }
        for (const auto& macro : macro_list_) {
            if (macro.name == trim(params[i])) {
                param = toUpper(macro.name);
                break;
            }
        }
        result += param;
    }
    result += ">";
    return result;
}

std::vector<std::string> XMLToHeaderConverter::splitTemplateParams(const std::string& content) const
{
    std::vector<std::string> params;
    std::string current;
    int bracket_count = 0;

    for (char c : content) {
        if (c == '<') {
            bracket_count++;
        } else if (c == '>') {
            bracket_count--;
            if (bracket_count < 0) {
                bracket_count = 0; // Prevent negative count
            }
        } else if (c == ',' && bracket_count == 0) {
            params.push_back(trim(current));
            current.clear();
            continue;
        }
        current += c;
    }

    if (!current.empty()) {
        params.push_back(trim(current));
    }

    return params;
}

std::string XMLToHeaderConverter::locationStr() const
{
    return current_file_ + ":" + std::to_string(current_line_);
}

std::string XMLToHeaderConverter::toCamelCase(const std::string& input) const
{
    std::string result;
    bool capitalize_next = false;

    for (char c : input) {
        if (c == '_') {
            capitalize_next = true;
        } else {
            if (capitalize_next) {
                result += toupper(c);
                capitalize_next = false;
            } else {
                result += c;
            }
        }
    }

    return result;
}

std::string XMLToHeaderConverter::trim(const std::string& str) const
{
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");

    if (start == std::string::npos || end == std::string::npos) {
        return "";
    }

    return str.substr(start, end - start + 1);
}

std::string XMLToHeaderConverter::replaceAll(const std::string& str, const std::string& from, const std::string& to) const
{
    std::string result = str;
    size_t pos = 0;

    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }

    return result;
}

bool XMLToHeaderConverter::parseBoolAttr(const char* attr_value) const
{
    if (!attr_value) {
        return false;
    }

    std::string value = trim(attr_value);
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::string XMLToHeaderConverter::getScalarWriteExpr(const std::string& cpp_type, const std::string& value_expr) const
{
    if (cpp_type.rfind("std::vector<", 0) == 0 || cpp_type.rfind("std::list<", 0) == 0
        || cpp_type.rfind("std::set<", 0) == 0 || cpp_type.rfind("std::unordered_set<", 0) == 0
        || cpp_type.rfind("std::map<", 0) == 0 || cpp_type.rfind("std::unordered_map<", 0) == 0
        || cpp_type.rfind("std::array<", 0) == 0) {
        return "chen::rpc::Serializer::Write(ba, " + value_expr + ");";
    }
    if (cpp_type == "int8_t") {
        return "ba->writeFint8(" + value_expr + ");";
    }
    if (cpp_type == "uint8_t") {
        return "ba->writeFuint8(" + value_expr + ");";
    }
    if (cpp_type == "int16_t") {
        return "ba->writeFint16(" + value_expr + ");";
    }
    if (cpp_type == "uint16_t") {
        return "ba->writeFuint16(" + value_expr + ");";
    }
    if (cpp_type == "int32_t") {
        return "ba->writeFint32(" + value_expr + ");";
    }
    if (cpp_type == "uint32_t") {
        return "ba->writeFuint32(" + value_expr + ");";
    }
    if (cpp_type == "int64_t") {
        return "ba->writeFint64(" + value_expr + ");";
    }
    if (cpp_type == "uint64_t") {
        return "ba->writeFuint64(" + value_expr + ");";
    }
    if (cpp_type == "float") {
        return "ba->writeFloat(" + value_expr + ");";
    }
    if (cpp_type == "double") {
        return "ba->writeDouble(" + value_expr + ");";
    }
    if (cpp_type == "bool") {
        return "ba->writeFint8(" + value_expr + " ? 1 : 0);";
    }
    if (cpp_type == "std::string") {
        return "ba->writeStringVint(" + value_expr + ");";
    }
    return "chen::rpc::Serialization<" + cpp_type + ">::write(ba, " + value_expr + ");";
}

std::string XMLToHeaderConverter::getScalarReadExpr(const std::string& cpp_type, const std::string& value_expr) const
{
    if (cpp_type.rfind("std::vector<", 0) == 0 || cpp_type.rfind("std::list<", 0) == 0
        || cpp_type.rfind("std::set<", 0) == 0 || cpp_type.rfind("std::unordered_set<", 0) == 0
        || cpp_type.rfind("std::map<", 0) == 0 || cpp_type.rfind("std::unordered_map<", 0) == 0
        || cpp_type.rfind("std::array<", 0) == 0) {
        return "chen::rpc::Serializer::Read(ba, " + value_expr + ");";
    }
    if (cpp_type == "int8_t") {
        return value_expr + " = ba->readFint8();";
    }
    if (cpp_type == "uint8_t") {
        return value_expr + " = ba->readFuint8();";
    }
    if (cpp_type == "int16_t") {
        return value_expr + " = ba->readFint16();";
    }
    if (cpp_type == "uint16_t") {
        return value_expr + " = ba->readFuint16();";
    }
    if (cpp_type == "int32_t") {
        return value_expr + " = ba->readFint32();";
    }
    if (cpp_type == "uint32_t") {
        return value_expr + " = ba->readFuint32();";
    }
    if (cpp_type == "int64_t") {
        return value_expr + " = ba->readFint64();";
    }
    if (cpp_type == "uint64_t") {
        return value_expr + " = ba->readFuint64();";
    }
    if (cpp_type == "float") {
        return value_expr + " = ba->readFloat();";
    }
    if (cpp_type == "double") {
        return value_expr + " = ba->readDouble();";
    }
    if (cpp_type == "bool") {
        return value_expr + " = ba->readFint8() != 0;";
    }
    if (cpp_type == "std::string") {
        return value_expr + " = ba->readStringVint();";
    }
    return "chen::rpc::Serialization<" + cpp_type + ">::read(ba, " + value_expr + ");";
}

void XMLToHeaderConverter::generateStructSerialization(std::ofstream& out, const StructInfo& struct_info)
{
    std::string struct_tag = "tag" + toCamelCase(struct_info.name);

    out << "/**" << std::endl;
    out << " * @brief RPC序列化适配器 - 自动生成" << std::endl;
    out << " */" << std::endl;
    out << "template <>" << std::endl;
    out << "struct chen::rpc::Serialization<" << struct_tag << "> {" << std::endl;
    out << "    static void write(ByteArray::ptr ba, const " << struct_tag << "& u) {" << std::endl;

    for (const EntryInfo& entry : struct_info.entries) {
        current_line_ = entry.line_num;
        std::string cpp_type = convertType(entry.type);
        if (has_error_) {
            return;
        }
        if (entry.is_array) {
            out << "        ba->writeUint32(static_cast<uint32_t>(u." << entry.name << ".size()));" << std::endl;
            out << "        for (const auto& item : u." << entry.name << ") {" << std::endl;
            out << "            " << getScalarWriteExpr(cpp_type, "item") << std::endl;
            out << "        }" << std::endl;
        } else {
            out << "        " << getScalarWriteExpr(cpp_type, "u." + entry.name) << std::endl;
        }
    }

    out << "    }" << std::endl;
    out << std::endl;
    out << "    static void read(ByteArray::ptr ba, " << struct_tag << "& u) {" << std::endl;

    for (const EntryInfo& entry : struct_info.entries) {
        current_line_ = entry.line_num;
        std::string cpp_type = convertType(entry.type);
        if (has_error_) {
            return;
        }
        if (entry.is_array) {
            if (entry.size.empty()) {
                out << "        {" << std::endl;
                out << "            uint32_t count = ba->readUint32();" << std::endl;
                out << "            u." << entry.name << ".clear();" << std::endl;
                out << "            u." << entry.name << ".reserve(count);" << std::endl;
                out << "            for (uint32_t i = 0; i < count; ++i) {" << std::endl;
                out << "                " << cpp_type << " item{};" << std::endl;
                out << "                " << getScalarReadExpr(cpp_type, "item") << std::endl;
                out << "                u." << entry.name << ".push_back(item);" << std::endl;
                out << "            }" << std::endl;
                out << "        }" << std::endl;
            } else {
                out << "        {" << std::endl;
                out << "            uint32_t count = ba->readUint32();" << std::endl;
                out << "            uint32_t fixed_size = static_cast<uint32_t>(u." << entry.name << ".size());" << std::endl;
                out << "            uint32_t fill = count < fixed_size ? count : fixed_size;" << std::endl;
                out << "            for (uint32_t i = 0; i < fill; ++i) {" << std::endl;
                out << "                " << cpp_type << " item{};" << std::endl;
                out << "                " << getScalarReadExpr(cpp_type, "item") << std::endl;
                out << "                u." << entry.name << "[i] = item;" << std::endl;
                out << "            }" << std::endl;
                out << "            for (uint32_t i = fill; i < count; ++i) {" << std::endl;
                out << "                " << cpp_type << " item{};" << std::endl;
                out << "                " << getScalarReadExpr(cpp_type, "item") << std::endl;
                out << "            }" << std::endl;
                out << "        }" << std::endl;
            }
        } else {
            out << "        " << getScalarReadExpr(cpp_type, "u." + entry.name) << std::endl;
        }
    }

    out << "    }" << std::endl;
    out << "};" << std::endl;
    out << std::endl;
}

bool XMLToHeaderConverter::parseXML(const std::string& filename)
{
    // Store the current XML file directory for resolving relative includes
    size_t last_slash = filename.find_last_of("/\\");
    current_xml_dir_ = (last_slash != std::string::npos) ? filename.substr(0, last_slash) : "";

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS) {
        ERROR(logger) << COLOR_RED << locationStr() << " Error loading XML file: " << filename << COLOR_RESET;
        return false;
    }

    current_file_ = filename;

    tinyxml2::XMLElement* metalib = doc.FirstChildElement("metalib");
    if (!metalib) {
        ERROR(logger) << COLOR_RED << locationStr() << " No metalib element found" << COLOR_RESET;
        return false;
    }

    // Get metalib description
    metalib_desc_ = metalib->Attribute("desc") ? metalib->Attribute("desc") : "";

    tinyxml2::XMLElement* header = metalib->FirstChildElement("header");
    if (!header) {
        ERROR(logger) << COLOR_RED << locationStr() << " No header element found" << COLOR_RESET;
        return false;
    }

    // Get header info
    header_name_ = header->Attribute("name") ? header->Attribute("name") : "PROTOCOL";
    header_desc_ = header->Attribute("desc") ? header->Attribute("desc") : "";
    header_pack_ = parseBoolAttr(header->Attribute("pack"));

    // Parse includes first
    for (tinyxml2::XMLElement* include = header->FirstChildElement("include"); include; include = include->NextSiblingElement("include")) {
        std::string include_file = include->Attribute("file") ? include->Attribute("file") : "";
        std::string include_prefix = include->Attribute("prefix") ? include->Attribute("prefix") : "";

        if (!include_file.empty()) {
            parseIncludeFile(include_file, include_prefix);
        }
    }

    // Walk elements in original order to preserve declaration ordering for structs/unions/enums
    for (tinyxml2::XMLElement* child = header->FirstChildElement(); child; child = child->NextSiblingElement()) {
        const char* tag_name = child->Name();
        if (!tag_name) {
            continue;
        }
        current_line_ = child->GetLineNum();

        std::string tag = tag_name;

        if (tag == "macro" && child->Parent() == header) {
            MacroInfo info;
            info.name = child->Attribute("name") ? child->Attribute("name") : "";
            info.value = child->Attribute("value") ? child->Attribute("value") : "";
            info.type = child->Attribute("type") ? child->Attribute("type") : "";
            info.desc = child->Attribute("desc") ? child->Attribute("desc") : "";
            if (info.name.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Macro is missing required 'name' attribute" << COLOR_RESET;
                has_error_ = true;
            }
            if (info.value.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Macro '" << info.name << "' is missing required 'value' attribute" << COLOR_RESET;
                has_error_ = true;
            }
            if (info.type.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Macro '" << info.name << "' is missing required 'type' attribute" << COLOR_RESET;
                has_error_ = true;
            }
            macro_list_.push_back(info);
            continue;
        }

        if (tag == "macrogroup") {
            MacroGroupInfo group_info;
            group_info.name = child->Attribute("name") ? child->Attribute("name") : "";
            group_info.desc = child->Attribute("desc") ? child->Attribute("desc") : "";
            if (group_info.name.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Macrogroup is missing required 'name' attribute" << COLOR_RESET;
                has_error_ = true;
            }

            for (tinyxml2::XMLElement* macro = child->FirstChildElement("macro"); macro; macro = macro->NextSiblingElement("macro")) {
                MacroInfo info;
                current_line_ = macro->GetLineNum();
                info.name = macro->Attribute("name") ? macro->Attribute("name") : "";
                info.value = macro->Attribute("value") ? macro->Attribute("value") : "";
                info.desc = macro->Attribute("desc") ? macro->Attribute("desc") : "";
                if (info.name.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Macrogroup '" << group_info.name
                                  << "' contains a macro with missing 'name' attribute" << COLOR_RESET;
                    has_error_ = true;
                }
                if (info.value.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Macrogroup '" << group_info.name << "' macro '"
                                  << info.name << "' is missing required 'value' attribute" << COLOR_RESET;
                    has_error_ = true;
                }
                group_info.macros.push_back(info);
            }

            macro_group_list_.push_back(group_info);
            element_order_.push_back({ElementKind::MacroGroup, macro_group_list_.size() - 1});
            continue;
        }

        if (tag == "struct") {
            StructInfo struct_info;
            struct_info.name = child->Attribute("name") ? child->Attribute("name") : "";
            struct_info.desc = child->Attribute("desc") ? child->Attribute("desc") : "";
            struct_info.need_serialization = parseBoolAttr(child->Attribute("serialize"))
                || parseBoolAttr(child->Attribute("serialization")) || parseBoolAttr(child->Attribute("rpc"));
            struct_info.pack = parseBoolAttr(child->Attribute("pack"));
            if (struct_info.name.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Struct is missing required 'name' attribute" << COLOR_RESET;
                has_error_ = true;
            }

            for (tinyxml2::XMLElement* entry = child->FirstChildElement("entry"); entry; entry = entry->NextSiblingElement("entry")) {
                EntryInfo entry_info;
                current_line_ = entry->GetLineNum();
                entry_info.line_num = current_line_;
                entry_info.name = entry->Attribute("name") ? entry->Attribute("name") : "";
                entry_info.type = entry->Attribute("type") ? decodeHTMLEntities(entry->Attribute("type")) : "";
                entry_info.desc = entry->Attribute("desc") ? entry->Attribute("desc") : "";

                if (entry_info.name.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Struct '" << struct_info.name
                                  << "' contains an entry with missing 'name' attribute" << COLOR_RESET;
                    has_error_ = true;
                }
                if (entry_info.type.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Struct '" << struct_info.name << "' entry '"
                                  << entry_info.name << "' is missing required 'type' attribute" << COLOR_RESET;
                    has_error_ = true;
                }

                const char* array_attr = entry->Attribute("array");
                entry_info.is_array = (array_attr && std::string(array_attr) == "true");

                if (entry_info.is_array) {
                    entry_info.size = entry->Attribute("size") ? entry->Attribute("size") : "";
                    if (entry_info.size.empty()) {
                        required_headers_.insert("<vector>");
                    } else {
                        required_headers_.insert("<array>");
                    }
                }

                if (!entry_info.type.empty()) {
                    convertType(entry_info.type);
                }

                struct_info.entries.push_back(entry_info);
            }

            struct_list_.push_back(struct_info);
            element_order_.push_back({ElementKind::Struct, struct_list_.size() - 1});
            continue;
        }

        if (tag == "union") {
            UnionInfo union_info;
            union_info.name = child->Attribute("name") ? child->Attribute("name") : "";
            union_info.desc = child->Attribute("desc") ? child->Attribute("desc") : "";
            if (union_info.name.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Union is missing required 'name' attribute" << COLOR_RESET;
                has_error_ = true;
            }

            for (tinyxml2::XMLElement* entry = child->FirstChildElement("entry"); entry; entry = entry->NextSiblingElement("entry")) {
                EntryInfo entry_info;
                current_line_ = entry->GetLineNum();
                entry_info.line_num = current_line_;
                entry_info.name = entry->Attribute("name") ? entry->Attribute("name") : "";
                entry_info.type = entry->Attribute("type") ? decodeHTMLEntities(entry->Attribute("type")) : "";
                entry_info.desc = entry->Attribute("desc") ? entry->Attribute("desc") : "";

                if (entry_info.name.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Union '" << union_info.name
                                  << "' contains an entry with missing 'name' attribute" << COLOR_RESET;
                    has_error_ = true;
                }
                if (entry_info.type.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Union '" << union_info.name << "' entry '"
                                  << entry_info.name << "' is missing required 'type' attribute" << COLOR_RESET;
                    has_error_ = true;
                }

                const char* array_attr = entry->Attribute("array");
                entry_info.is_array = (array_attr && std::string(array_attr) == "true");

                if (entry_info.is_array) {
                    entry_info.size = entry->Attribute("size") ? entry->Attribute("size") : "";
                    if (entry_info.size.empty()) {
                        required_headers_.insert("<vector>");
                    } else {
                        required_headers_.insert("<array>");
                    }
                }

                if (!entry_info.type.empty()) {
                    convertType(entry_info.type);
                }

                union_info.entries.push_back(entry_info);
            }

            union_list_.push_back(union_info);
            element_order_.push_back({ElementKind::Union, union_list_.size() - 1});
        }
    }

    return true;
}

void XMLToHeaderConverter::generateHeader(const std::string& output_file)
{
    std::ofstream out(output_file);
    if (!out.is_open()) {
        ERROR(logger) << COLOR_RED << current_file_ << ":0"
                      << " Error opening output file: " << output_file << COLOR_RESET;
        return;
    }

    bool has_serializable_struct = false;
    for (const auto& struct_info : struct_list_) {
        if (struct_info.need_serialization) {
            has_serializable_struct = true;
            break;
        }
    }
    if (has_serializable_struct) {
        required_headers_.insert("<Chen/Rpc/Serializer.hpp>");
    }

    // Generate header guard
    std::string guard_name = "__" + toUpper(header_name_) + "_H__";

    // Write header
    out << "/**" << std::endl;
    out << " * @brief " << header_desc_ << std::endl;
    out << " * @note " << metalib_desc_ << std::endl;
    out << " */" << std::endl;
    out << "#ifndef " << guard_name << std::endl;
    out << "#define " << guard_name << std::endl;
    out << std::endl;
    // Write required headers
    for (const std::string& header : required_headers_) {
        out << "#include " << header << std::endl;
    }
    out << std::endl;

    if (header_pack_) {
        out << "#pragma pack(push, 1)" << std::endl;
    }
    out << std::endl;

    // Write standalone macros (defines)
    for (const auto& macro : macro_list_) {
        std::string define_name = toUpper(macro.name);
        std::string value = macro.value;

        if (macro.type == "string") {
            value = "\"" + value + "\"";
        }

        if (!macro.desc.empty()) {
            out << "#define " << define_name << " " << value << " /* " << macro.desc << " */" << std::endl;
        } else {
            out << "#define " << define_name << " " << value << std::endl;
        }
    }
    out << std::endl;

    // Write remaining elements following XML order (macros already emitted above)
    for (const auto& element : element_order_) {
        switch (element.kind) {
        case ElementKind::MacroGroup: {
            const auto& group = macro_group_list_[element.index];
            if (isExternalType(group.name)) {
                break;
            }

            if (!group.desc.empty()) {
                out << "/**" << std::endl;
                out << " * @brief " << group.desc << std::endl;
                out << " */" << std::endl;
            }

            out << "enum tag" << toCamelCase(group.name) << " {" << std::endl;

            for (size_t i = 0; i < group.macros.size(); ++i) {
                const MacroInfo& macro = group.macros[i];
                std::string enum_name = toUpper(macro.name);
                if (!macro.desc.empty()) {
                    if (i == group.macros.size() - 1) {
                        out << "    " << enum_name << " = " << macro.value << " /* " << macro.desc << " */"
                            << std::endl;
                    } else {
                        out << "    " << enum_name << " = " << macro.value << ", /* " << macro.desc << " */"
                            << std::endl;
                    }
                } else {
                    if (i == group.macros.size() - 1) {
                        out << "    " << enum_name << " = " << macro.value << std::endl;
                    } else {
                        out << "    " << enum_name << " = " << macro.value << "," << std::endl;
                    }
                }
            }

            out << "};" << std::endl;
            out << std::endl;
            break;
        }
        case ElementKind::Struct: {
            const auto& struct_info = struct_list_[element.index];
            if (isExternalType(struct_info.name)) {
                break;
            }

            if (struct_info.pack) {
                out << "#pragma pack(push, 1)" << std::endl;
            }

            if (!struct_info.desc.empty()) {
                out << "/**" << std::endl;
                out << " * @brief " << struct_info.desc << std::endl;
                out << " */" << std::endl;
            }

            out << "struct tag" << toCamelCase(struct_info.name) << " {" << std::endl;

            for (const EntryInfo& entry : struct_info.entries) {
                current_line_ = entry.line_num;
                std::string cpp_type = convertType(entry.type);
                if (has_error_) {
                    break;
                }

                if (entry.is_array) {
                    if (!entry.size.empty()) {
                        bool found = false;
                        for (const auto& macro : macro_list_) {
                            if (macro.name == entry.size) {
                                std::string size_const = toUpper(entry.size);
                                cpp_type = "std::array<" + cpp_type + ", " + size_const + ">";
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            cpp_type = "std::array<" + cpp_type + ", " + entry.size + ">";
                        }
                    } else {
                        cpp_type = "std::vector<" + cpp_type + ">";
                    }
                }

                if (!entry.desc.empty()) {
                    out << "    " << cpp_type << " " << entry.name << "; /* " << entry.desc << " */" << std::endl;
                } else {
                    out << "    " << cpp_type << " " << entry.name << ";" << std::endl;
                }
            }

            out << "};" << std::endl;

            if (struct_info.pack) {
                out << "#pragma pack(pop)" << std::endl;
            }
            out << std::endl;

            if (struct_info.need_serialization) {
                generateStructSerialization(out, struct_info);
            }
            break;
        }
        case ElementKind::Union: {
            const auto& union_info = union_list_[element.index];
            if (isExternalType(union_info.name)) {
                break;
            }

            if (!union_info.desc.empty()) {
                out << "/**" << std::endl;
                out << " * @brief " << union_info.desc << std::endl;
                out << " */" << std::endl;
            }

            out << "union tag" << toCamelCase(union_info.name) << " {" << std::endl;

            for (const EntryInfo& entry : union_info.entries) {
                current_line_ = entry.line_num;
                std::string cpp_type = convertType(entry.type);
                if (has_error_) {
                    break;
                }

                if (entry.is_array) {
                    if (!entry.size.empty()) {
                        bool found = false;
                        for (const auto& macro : macro_list_) {
                            if (macro.name == entry.size) {
                                std::string size_const = toUpper(entry.size);
                                cpp_type = "std::array<" + cpp_type + ", " + size_const + ">";
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            cpp_type = "std::array<" + cpp_type + ", " + entry.size + ">";
                        }
                    } else {
                        cpp_type = "std::vector<" + cpp_type + ">";
                    }
                }

                if (!entry.desc.empty()) {
                    out << "    " << cpp_type << " " << entry.name << "; /* " << entry.desc << " */" << std::endl;
                } else {
                    out << "    " << cpp_type << " " << entry.name << ";" << std::endl;
                }
            }

            out << "};" << std::endl;
            out << std::endl;
            break;
        }
        }
    }

    if (header_pack_) {
        out << "#pragma pack(pop)" << std::endl;
    }

    out << "#endif // " << guard_name << std::endl;

    out.close();

    if (!hasError()) {
        INFO(logger) << "Header file generated successfully: " << output_file;
    } else {
        ERROR(logger) << COLOR_RED << "Errors occurred during header generation. Please check the logs." << COLOR_RESET;
    }
}

std::string XMLToHeaderConverter::toUpper(const std::string& str)
{
    std::string result = str;
    std::ranges::transform(result, result.begin(), ::toupper);
    return result;
}

void XMLToHeaderConverter::parseIncludeFile(const std::string& include_file, const std::string& prefix)
{
    tinyxml2::XMLDocument doc;
    std::string full_path = include_file;

    // Resolve relative path
    if (include_file[0] != '/' && include_file[0] != '.') {
        // Relative path without leading ./
        // Combine with current XML directory
        if (!current_xml_dir_.empty()) {
            full_path = current_xml_dir_ + "/" + include_file;
        } else {
            full_path = include_file;
        }
    } else if (include_file[0] == '.' && include_file[1] == '/') {
        // Relative path with ./
        if (!current_xml_dir_.empty()) {
            full_path = current_xml_dir_ + include_file.substr(1);
        } else {
            full_path = include_file.substr(2);
        }
    }

    if (doc.LoadFile(full_path.c_str()) != tinyxml2::XML_SUCCESS) {
        WARN(logger) << full_path << " Could not load include file";
        return;
    }

    tinyxml2::XMLElement* metalib = doc.FirstChildElement("metalib");
    if (!metalib) {
        WARN(logger) << full_path << " Include file missing metalib";
        return;
    }

    tinyxml2::XMLElement* header = metalib->FirstChildElement("header");
    if (!header) {
        WARN(logger) << full_path << " Include file missing header";
        return;
    }

    // Collect macro groups from included file
    for (tinyxml2::XMLElement* group = header->FirstChildElement("macrogroup"); group; group = group->NextSiblingElement("macrogroup")) {
        std::string group_name = group->Attribute("name") ? group->Attribute("name") : "";
        if (!group_name.empty()) {
            included_types_.insert(group_name);
            std::string header_file = extractHeaderName(full_path);
            external_type_mapping_[group_name] = header_file;
            required_headers_.insert("\"" + header_file + "\"");
        }
    }

    // Collect structs from included file
    for (tinyxml2::XMLElement* struct_elem = header->FirstChildElement("struct"); struct_elem;
        struct_elem = struct_elem->NextSiblingElement("struct")) {
        std::string struct_name = struct_elem->Attribute("name") ? struct_elem->Attribute("name") : "";
        if (!struct_name.empty()) {
            included_types_.insert(struct_name);
            std::string header_file = extractHeaderName(full_path);
            external_type_mapping_[struct_name] = header_file;
            required_headers_.insert("\"" + header_file + "\"");
        }
    }

    // Collect unions from included file
    for (tinyxml2::XMLElement* union_elem = header->FirstChildElement("union"); union_elem;
        union_elem = union_elem->NextSiblingElement("union")) {
        std::string union_name = union_elem->Attribute("name") ? union_elem->Attribute("name") : "";
        if (!union_name.empty()) {
            included_types_.insert(union_name);
            std::string header_file = extractHeaderName(full_path);
            external_type_mapping_[union_name] = header_file;
            required_headers_.insert("\"" + header_file + "\"");
        }
    }
}

std::string XMLToHeaderConverter::extractHeaderName(const std::string& xml_path)
{
    // Convert XML file path to header file name
    // e.g., "resources.xml" -> "resources.h"
    size_t last_slash = xml_path.find_last_of("/\\");
    std::string filename = (last_slash != std::string::npos) ? xml_path.substr(last_slash + 1) : xml_path;

    size_t last_dot = filename.find_last_of(".");
    if (last_dot != std::string::npos) {
        filename = filename.substr(0, last_dot);
    }

    return filename + ".h";
}

bool XMLToHeaderConverter::isExternalType(const std::string& type)
{
    return included_types_.find(type) != included_types_.end();
}

bool XMLToHeaderConverter::hasError() const
{
    return has_error_;
}

std::string XMLToHeaderConverter::decodeHTMLEntities(const std::string& str) const
{
    std::string result = str;
    size_t pos = 0;

    // Decode &lt; to <
    while ((pos = result.find("&lt;", pos)) != std::string::npos) {
        result.replace(pos, 4, "<");
        pos += 1;
    }

    // Reset pos and decode &gt; to >
    pos = 0;
    while ((pos = result.find("&gt;", pos)) != std::string::npos) {
        result.replace(pos, 4, ">");
        pos += 1;
    }

    // Reset pos and decode &amp; to &
    pos = 0;
    while ((pos = result.find("&amp;", pos)) != std::string::npos) {
        result.replace(pos, 5, "&");
        pos += 1;
    }

    return result;
}

} // namespace chen::converter

int main(int argc, char* argv[])
{
    using namespace chen::converter;

    if (argc != 3) {
        WARN(logger) << "\n" << "Usage: " << argv[0] << " <input.xml> <output.h>";
        WARN(logger) << "       " << argv[0] << " <input_dir> <output_dir>";
        return 1;
    }

    std::string input = argv[1];
    std::string output = argv[2];

    // Batch mode: input is a directory
    if (chen::FSUtil::IsDirectory(input)) {
        std::vector<std::string> files;
        chen::FSUtil::ListAllFile(files, input, ".xml");

        if (files.empty()) {
            WARN(logger) << "No .xml files found in directory: " << input;
            return 0;
        }

        chen::FSUtil::Mkdir(output);

        bool all_success = true;
        for (const auto& file : files) {
            std::string basename = chen::FSUtil::Basename(file);
            size_t last_dot = basename.find_last_of('.');
            if (last_dot != std::string::npos) {
                basename = basename.substr(0, last_dot);
            }
            std::string output_file = output + "/" + basename + ".h";

            XMLToHeaderConverter converter;
            if (!converter.parseXML(file)) {
                ERROR(logger) << COLOR_RED << file << ":0 Failed to parse XML file" << COLOR_RESET;
                all_success = false;
                continue;
            }
            converter.generateHeader(output_file);
            if (converter.hasError()) {
                ERROR(logger) << COLOR_RED << "Failed to generate header due to type conversion errors in " << file << COLOR_RESET;
                all_success = false;
                return 1;
            }
        }

        return all_success ? 0 : 1;
    }

    // Single file mode
    XMLToHeaderConverter converter;

    if (!converter.parseXML(input)) {
        ERROR(logger) << COLOR_RED << input << ":0 Failed to parse XML file" << COLOR_RESET;
        return 1;
    }

    converter.generateHeader(output);

    if (converter.hasError()) {
        ERROR(logger) << COLOR_RED << "Failed to generate header due to type conversion errors" << COLOR_RESET;
        return 1;
    }

    return 0;
}