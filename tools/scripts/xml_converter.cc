#include "xml_converter.hpp"

#include <tinyxml2.h>
#include <chen/log/log.h>
#include <chen/util/fs_util.h>
#include <chen/util/macro.h>

namespace chen::converter {

static Logger::ptr logger = LOG_NAME("converter");

std::string XMLToHeaderConverter::convertType(const std::string& type) {
    if (type.empty()) {
        if (m_reportedUnknownType.insert(type).second) {
            ERROR(logger) << COLOR_RED << locationStr() << " Empty type (missing 'type' attribute)" << COLOR_RESET;
        }
        m_hasError = true;
        return type;
    }
    // Handle square bracket syntax [type] -> <type>
    if (type.find('[') != std::string::npos && type.find(']') != std::string::npos) {
        std::string converted = replaceAll(type, "[", "<");
        converted = replaceAll(converted, "]", ">");
        return convertType(converted); // Recursive call to process the converted type
    }

    // Basic types mapping
    static const std::map<std::string, std::string> basicTypes = {
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
        {"blob", "std::vector<uint8_t>"}
    };

    auto it = basicTypes.find(type);
    if (it != basicTypes.end()) {
        static const std::set<std::string> cstdintBasedTypes = {
            "int", "uint", "short", "ushort", "char", "byte",
            "ubyte", "tinyint", "tinyuint", "bigint", "biguint",
            "number", "varint", "timestamp", "datetime", "smallint",
            "smalluint", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64"
        };

        if (type == "string" || type == "stringvar") {
            requiredHeaders.insert("<string>");
        } else if (cstdintBasedTypes.count(type) > 0) {
            requiredHeaders.insert("<cstdint>");
        }

        // Add headers for specific types
        if (type == "bool") {
            // requiredHeaders.insert("<stdbool.h>");
        } else if (type == "bytes" || type == "blob") {
            requiredHeaders.insert("<vector>");
        }
        return it->second;
    }

    // Template types
    if (type.substr(0, 6) == "array<") {
        requiredHeaders.insert("<array>");
        return processTemplateType("std::array", type);
    }
    if (type.substr(0, 4) == "map<") {
        requiredHeaders.insert("<map>");
        return processTemplateType("std::map", type);
    }
    if (type.substr(0, 4) == "set<") {
        requiredHeaders.insert("<set>");
        return processTemplateType("std::set", type);
    }
    if (type.substr(0, 5) == "pair<") {
        requiredHeaders.insert("<utility>");
        return processTemplateType("std::pair", type);
    }
    if (type.substr(0, 5) == "list<") {
        requiredHeaders.insert("<list>");
        return processTemplateType("std::list", type);
    }
    if (type.substr(0, 6) == "tuple<") {
        requiredHeaders.insert("<tuple>");
        return processTupleType(type);
    }
    if (type.substr(0, 9) == "optional<") {
        requiredHeaders.insert("<optional>");
        return processTemplateType("std::optional", type);
    }
    if (type.substr(0, 8) == "variant<") {
        requiredHeaders.insert("<variant>");
        return processVariantType(type);
    }
    if (type.substr(0, 7) == "vector<") {
        requiredHeaders.insert("<vector>");
        return processTemplateType("std::vector", type);
    }

    // Custom types (enums, structs, unions) - check external types first
    if (isExternalType(type)) {
        return "tag" + toCamelCase(type);
    }

    for (const auto& group : macroGroupList) {
        if (group.name == type) {
            return "tag" + toCamelCase(type);
        }
    }
    for (const auto& structInfo : structList) {
        if (structInfo.name == type) {
            return "tag" + toCamelCase(type);
        }
    }
    for (const auto& unionInfo : unionList) {
        if (unionInfo.name == type) {
            return "tag" + toCamelCase(type);
        }
    }

    // Default fallback - try to convert basic types again for edge cases
    std::string trimmedType = trim(type);
    auto basicIt = basicTypes.find(trimmedType);
    if (basicIt != basicTypes.end()) {
        static const std::set<std::string> cstdintBasedTypes = {
            "int", "uint", "short", "ushort", "char", "byte", "ubyte",
            "tinyint", "tinyuint", "smallint", "smalluint", "bigint",
            "biguint", "number", "varint", "timestamp", "datetime",
            "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64"
        };

        if (trimmedType == "string" || trimmedType == "stringvar") {
            requiredHeaders.insert("<string>");
        } else if (cstdintBasedTypes.count(trimmedType) > 0) {
            requiredHeaders.insert("<cstdint>");
        }

        if (trimmedType == "bool") {
            requiredHeaders.insert("<stdbool.h>");
        } else if (trimmedType == "bytes" || trimmedType == "blob") {
            requiredHeaders.insert("<vector>");
        }
        return basicIt->second;
    }

    if (m_reportedUnknownType.insert(type).second) {
        ERROR(logger) << COLOR_RED << locationStr() << " Unknown type: " << type << COLOR_RESET;
    }
    m_hasError = true;
    return type;
}

std::string XMLToHeaderConverter::processTemplateType(const std::string& templateName, const std::string& type) {
    std::string content = type.substr(type.find('<') + 1, type.rfind('>') - type.find('<') - 1);
    std::vector<std::string> params = splitTemplateParams(content);

    std::string result = templateName + "<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        std::string param = convertType(trim(params[i]));
        if (m_hasError) {
            break;
        }
        // Check if this parameter is a macro name
        for (const auto& macro : macroList) {
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

std::string XMLToHeaderConverter::processTupleType(const std::string& type) {
    std::string content = type.substr(type.find('<') + 1, type.rfind('>') - type.find('<') - 1);
    std::vector<std::string> params = splitTemplateParams(content);

    std::string result = "std::tuple<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        std::string param = convertType(trim(params[i]));
        if (m_hasError) {
            break;
        }
        // Check if this parameter is a macro name
        for (const auto& macro : macroList) {
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

std::string XMLToHeaderConverter::processVariantType(const std::string& type) {
    std::string content = type.substr(type.find('<') + 1, type.rfind('>') - type.find('<') - 1);
    std::vector<std::string> params = splitTemplateParams(content);

    std::string result = "std::variant<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        std::string param = convertType(trim(params[i]));
        if (m_hasError) {
            break;
        }
        for (const auto& macro : macroList) {
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

std::vector<std::string> XMLToHeaderConverter::splitTemplateParams(const std::string& content) const {
    std::vector<std::string> params;
    std::string current;
    int bracketCount = 0;

    for (char c : content) {
        if (c == '<') {
            bracketCount++;
        } else if (c == '>') {
            bracketCount--;
            if (bracketCount < 0) {
                bracketCount = 0; // Prevent negative count
            }
        } else if (c == ',' && bracketCount == 0) {
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

std::string XMLToHeaderConverter::locationStr() const {
    return m_currentFile + ":" + std::to_string(m_currentLine);
}

std::string XMLToHeaderConverter::toCamelCase(const std::string& input) const {
    std::string result;
    bool capitalizeNext = false;

    for (char c : input) {
        if (c == '_') {
            capitalizeNext = true;
        } else {
            if (capitalizeNext) {
                result += toupper(c);
                capitalizeNext = false;
            } else {
                result += c;
            }
        }
    }

    return result;
}

std::string XMLToHeaderConverter::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");

    if (start == std::string::npos || end == std::string::npos) {
        return "";
    }

    return str.substr(start, end - start + 1);
}

std::string XMLToHeaderConverter::replaceAll(const std::string& str, const std::string& from, const std::string& to) const {
    std::string result = str;
    size_t pos = 0;

    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }

    return result;
}

bool XMLToHeaderConverter::parseBoolAttr(const char* attrValue) const {
    if (!attrValue) {
        return false;
    }

    std::string value = trim(attrValue);
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::string XMLToHeaderConverter::getScalarWriteExpr(const std::string& cppType, const std::string& valueExpr) const {
    if (cppType.rfind("std::vector<", 0) == 0
        || cppType.rfind("std::list<", 0) == 0
        || cppType.rfind("std::set<", 0) == 0
        || cppType.rfind("std::unordered_set<", 0) == 0
        || cppType.rfind("std::map<", 0) == 0
        || cppType.rfind("std::unordered_map<", 0) == 0
        || cppType.rfind("std::array<", 0) == 0) {
        return "chen::rpc::Serializer::Write(ba, " + valueExpr + ");";
    }
    if (cppType == "int8_t") {
        return "ba->writeFint8(" + valueExpr + ");";
    }
    if (cppType == "uint8_t") {
        return "ba->writeFuint8(" + valueExpr + ");";
    }
    if (cppType == "int16_t") {
        return "ba->writeFint16(" + valueExpr + ");";
    }
    if (cppType == "uint16_t") {
        return "ba->writeFuint16(" + valueExpr + ");";
    }
    if (cppType == "int32_t") {
        return "ba->writeFint32(" + valueExpr + ");";
    }
    if (cppType == "uint32_t") {
        return "ba->writeFuint32(" + valueExpr + ");";
    }
    if (cppType == "int64_t") {
        return "ba->writeFint64(" + valueExpr + ");";
    }
    if (cppType == "uint64_t") {
        return "ba->writeFuint64(" + valueExpr + ");";
    }
    if (cppType == "float") {
        return "ba->writeFloat(" + valueExpr + ");";
    }
    if (cppType == "double") {
        return "ba->writeDouble(" + valueExpr + ");";
    }
    if (cppType == "bool") {
        return "ba->writeFint8(" + valueExpr + " ? 1 : 0);";
    }
    if (cppType == "std::string") {
        return "ba->writeStringVint(" + valueExpr + ");";
    }
    return "chen::rpc::Serialization<" + cppType + ">::write(ba, " + valueExpr + ");";
}

std::string XMLToHeaderConverter::getScalarReadExpr(const std::string& cppType, const std::string& valueExpr) const {
    if (cppType.rfind("std::vector<", 0) == 0
        || cppType.rfind("std::list<", 0) == 0
        || cppType.rfind("std::set<", 0) == 0
        || cppType.rfind("std::unordered_set<", 0) == 0
        || cppType.rfind("std::map<", 0) == 0
        || cppType.rfind("std::unordered_map<", 0) == 0
        || cppType.rfind("std::array<", 0) == 0) {
        return "chen::rpc::Serializer::Read(ba, " + valueExpr + ");";
    }
    if (cppType == "int8_t") {
        return valueExpr + " = ba->readFint8();";
    }
    if (cppType == "uint8_t") {
        return valueExpr + " = ba->readFuint8();";
    }
    if (cppType == "int16_t") {
        return valueExpr + " = ba->readFint16();";
    }
    if (cppType == "uint16_t") {
        return valueExpr + " = ba->readFuint16();";
    }
    if (cppType == "int32_t") {
        return valueExpr + " = ba->readFint32();";
    }
    if (cppType == "uint32_t") {
        return valueExpr + " = ba->readFuint32();";
    }
    if (cppType == "int64_t") {
        return valueExpr + " = ba->readFint64();";
    }
    if (cppType == "uint64_t") {
        return valueExpr + " = ba->readFuint64();";
    }
    if (cppType == "float") {
        return valueExpr + " = ba->readFloat();";
    }
    if (cppType == "double") {
        return valueExpr + " = ba->readDouble();";
    }
    if (cppType == "bool") {
        return valueExpr + " = ba->readFint8() != 0;";
    }
    if (cppType == "std::string") {
        return valueExpr + " = ba->readStringVint();";
    }
    return "chen::rpc::Serialization<" + cppType + ">::read(ba, " + valueExpr + ");";
}

void XMLToHeaderConverter::generateStructSerialization(std::ofstream& out, const StructInfo& structInfo) {
    std::string structTag = "tag" + toCamelCase(structInfo.name);

    out << "/**" << std::endl;
    out << " * @brief RPC序列化适配器 - 自动生成" << std::endl;
    out << " */" << std::endl;
    out << "template <>" << std::endl;
    out << "struct chen::rpc::Serialization<" << structTag << "> {" << std::endl;
    out << "    static void write(ByteArray::ptr ba, const " << structTag << "& u) {" << std::endl;

    for (const EntryInfo& entry : structInfo.entries) {
        m_currentLine = entry.lineNum;
        std::string cppType = convertType(entry.type);
        if (m_hasError) {
            return;
        }
        if (entry.isArray) {
            out << "        ba->writeUint32(static_cast<uint32_t>(u." << entry.name << ".size()));" << std::endl;
            out << "        for (const auto& item : u." << entry.name << ") {" << std::endl;
            out << "            " << getScalarWriteExpr(cppType, "item") << std::endl;
            out << "        }" << std::endl;
        } else {
            out << "        " << getScalarWriteExpr(cppType, "u." + entry.name) << std::endl;
        }
    }

    out << "    }" << std::endl;
    out << std::endl;
    out << "    static void read(ByteArray::ptr ba, " << structTag << "& u) {" << std::endl;

    for (const EntryInfo& entry : structInfo.entries) {
        m_currentLine = entry.lineNum;
        std::string cppType = convertType(entry.type);
        if (m_hasError) {
            return;
        }
        if (entry.isArray) {
            if (entry.size.empty()) {
                out << "        {" << std::endl;
                out << "            uint32_t count = ba->readUint32();" << std::endl;
                out << "            u." << entry.name << ".clear();" << std::endl;
                out << "            u." << entry.name << ".reserve(count);" << std::endl;
                out << "            for (uint32_t i = 0; i < count; ++i) {" << std::endl;
                out << "                " << cppType << " item{};" << std::endl;
                out << "                " << getScalarReadExpr(cppType, "item") << std::endl;
                out << "                u." << entry.name << ".push_back(item);" << std::endl;
                out << "            }" << std::endl;
                out << "        }" << std::endl;
            } else {
                out << "        {" << std::endl;
                out << "            uint32_t count = ba->readUint32();" << std::endl;
                out << "            uint32_t fixed_size = static_cast<uint32_t>(u." << entry.name << ".size());" << std::endl;
                out << "            uint32_t fill = count < fixed_size ? count : fixed_size;" << std::endl;
                out << "            for (uint32_t i = 0; i < fill; ++i) {" << std::endl;
                out << "                " << cppType << " item{};" << std::endl;
                out << "                " << getScalarReadExpr(cppType, "item") << std::endl;
                out << "                u." << entry.name << "[i] = item;" << std::endl;
                out << "            }" << std::endl;
                out << "            for (uint32_t i = fill; i < count; ++i) {" << std::endl;
                out << "                " << cppType << " item{};" << std::endl;
                out << "                " << getScalarReadExpr(cppType, "item") << std::endl;
                out << "            }" << std::endl;
                out << "        }" << std::endl;
            }
        } else {
            out << "        " << getScalarReadExpr(cppType, "u." + entry.name) << std::endl;
        }
    }

    out << "    }" << std::endl;
    out << "};" << std::endl;
    out << std::endl;
}

bool XMLToHeaderConverter::parseXML(const std::string& filename) {
    // Store the current XML file directory for resolving relative includes
    size_t lastSlash = filename.find_last_of("/\\");
    currentXMLDir = (lastSlash != std::string::npos) ? filename.substr(0, lastSlash) : "";

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS) {
        ERROR(logger) << COLOR_RED << locationStr() << " Error loading XML file: " << filename << COLOR_RESET;
        return false;
    }

    m_currentFile = filename;

    tinyxml2::XMLElement* metalib = doc.FirstChildElement("metalib");
    if (!metalib) {
        ERROR(logger) << COLOR_RED << locationStr() << " No metalib element found" << COLOR_RESET;
        return false;
    }

    // Get metalib description
    metalibDesc = metalib->Attribute("desc") ? metalib->Attribute("desc") : "";

    tinyxml2::XMLElement* header = metalib->FirstChildElement("header");
    if (!header) {
        ERROR(logger) << COLOR_RED << locationStr() << " No header element found" << COLOR_RESET;
        return false;
    }

    // Get header info
    headerName = header->Attribute("name") ? header->Attribute("name") : "PROTOCOL";
    headerDesc = header->Attribute("desc") ? header->Attribute("desc") : "";
    m_headerPack = parseBoolAttr(header->Attribute("pack"));

    // Parse includes first
    for (tinyxml2::XMLElement* include = header->FirstChildElement("include"); include; include = include->NextSiblingElement("include")) {
        std::string includeFile = include->Attribute("file") ? include->Attribute("file") : "";
        std::string includePrefix = include->Attribute("prefix") ? include->Attribute("prefix") : "";

        if (!includeFile.empty()) {
            parseIncludeFile(includeFile, includePrefix);
        }
    }

    // Walk elements in original order to preserve declaration ordering for structs/unions/enums
    for (tinyxml2::XMLElement* child = header->FirstChildElement(); child; child = child->NextSiblingElement()) {
        const char* tagName = child->Name();
        if (!tagName) {
            continue;
        }
        m_currentLine = child->GetLineNum();

        std::string tag = tagName;

        if (tag == "macro" && child->Parent() == header) {
            MacroInfo info;
            info.name = child->Attribute("name") ? child->Attribute("name") : "";
            info.value = child->Attribute("value") ? child->Attribute("value") : "";
            info.type = child->Attribute("type") ? child->Attribute("type") : "";
            info.desc = child->Attribute("desc") ? child->Attribute("desc") : "";
            if (info.name.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Macro is missing required 'name' attribute" << COLOR_RESET;
                m_hasError = true;
            }
            if (info.value.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Macro '" << info.name << "' is missing required 'value' attribute" << COLOR_RESET;
                m_hasError = true;
            }
            if (info.type.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Macro '" << info.name << "' is missing required 'type' attribute" << COLOR_RESET;
                m_hasError = true;
            }
            macroList.push_back(info);
            continue;
        }

        if (tag == "macrogroup") {
            MacroGroupInfo groupInfo;
            groupInfo.name = child->Attribute("name") ? child->Attribute("name") : "";
            groupInfo.desc = child->Attribute("desc") ? child->Attribute("desc") : "";
            if (groupInfo.name.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Macrogroup is missing required 'name' attribute" << COLOR_RESET;
                m_hasError = true;
            }

            for (tinyxml2::XMLElement* macro = child->FirstChildElement("macro"); macro; macro = macro->NextSiblingElement("macro")) {
                MacroInfo info;
                m_currentLine = macro->GetLineNum();
                info.name = macro->Attribute("name") ? macro->Attribute("name") : "";
                info.value = macro->Attribute("value") ? macro->Attribute("value") : "";
                info.desc = macro->Attribute("desc") ? macro->Attribute("desc") : "";
                if (info.name.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Macrogroup '" << groupInfo.name << "' contains a macro with missing 'name' attribute" << COLOR_RESET;
                    m_hasError = true;
                }
                if (info.value.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Macrogroup '" << groupInfo.name << "' macro '" << info.name << "' is missing required 'value' attribute" << COLOR_RESET;
                    m_hasError = true;
                }
                groupInfo.macros.push_back(info);
            }

            macroGroupList.push_back(groupInfo);
            elementOrder.push_back({ElementKind::MacroGroup, macroGroupList.size() - 1});
            continue;
        }

        if (tag == "struct") {
            StructInfo structInfo;
            structInfo.name = child->Attribute("name") ? child->Attribute("name") : "";
            structInfo.desc = child->Attribute("desc") ? child->Attribute("desc") : "";
            structInfo.needSerialization = parseBoolAttr(child->Attribute("serialize"))
                || parseBoolAttr(child->Attribute("serialization"))
                || parseBoolAttr(child->Attribute("rpc"));
            structInfo.pack = parseBoolAttr(child->Attribute("pack"));
            if (structInfo.name.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Struct is missing required 'name' attribute" << COLOR_RESET;
                m_hasError = true;
            }

            for (tinyxml2::XMLElement* entry = child->FirstChildElement("entry"); entry; entry = entry->NextSiblingElement("entry")) {
                EntryInfo entryInfo;
                m_currentLine = entry->GetLineNum();
                entryInfo.lineNum = m_currentLine;
                entryInfo.name = entry->Attribute("name") ? entry->Attribute("name") : "";
                entryInfo.type = entry->Attribute("type") ? decodeHTMLEntities(entry->Attribute("type")) : "";
                entryInfo.desc = entry->Attribute("desc") ? entry->Attribute("desc") : "";

                if (entryInfo.name.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Struct '" << structInfo.name << "' contains an entry with missing 'name' attribute" << COLOR_RESET;
                    m_hasError = true;
                }
                if (entryInfo.type.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Struct '" << structInfo.name << "' entry '" << entryInfo.name << "' is missing required 'type' attribute" << COLOR_RESET;
                    m_hasError = true;
                }

                const char* arrayAttr = entry->Attribute("array");
                entryInfo.isArray = (arrayAttr && std::string(arrayAttr) == "true");

                if (entryInfo.isArray) {
                    entryInfo.size = entry->Attribute("size") ? entry->Attribute("size") : "";
                    if (entryInfo.size.empty()) {
                        requiredHeaders.insert("<vector>");
                    } else {
                        requiredHeaders.insert("<array>");
                    }
                }

                if (!entryInfo.type.empty()) {
                    convertType(entryInfo.type);
                }

                structInfo.entries.push_back(entryInfo);
            }

            structList.push_back(structInfo);
            elementOrder.push_back({ElementKind::Struct, structList.size() - 1});
            continue;
        }

        if (tag == "union") {
            UnionInfo unionInfo;
            unionInfo.name = child->Attribute("name") ? child->Attribute("name") : "";
            unionInfo.desc = child->Attribute("desc") ? child->Attribute("desc") : "";
            if (unionInfo.name.empty()) {
                ERROR(logger) << COLOR_RED << locationStr() << " Union is missing required 'name' attribute" << COLOR_RESET;
                m_hasError = true;
            }

            for (tinyxml2::XMLElement* entry = child->FirstChildElement("entry"); entry; entry = entry->NextSiblingElement("entry")) {
                EntryInfo entryInfo;
                m_currentLine = entry->GetLineNum();
                entryInfo.lineNum = m_currentLine;
                entryInfo.name = entry->Attribute("name") ? entry->Attribute("name") : "";
                entryInfo.type = entry->Attribute("type") ? decodeHTMLEntities(entry->Attribute("type")) : "";
                entryInfo.desc = entry->Attribute("desc") ? entry->Attribute("desc") : "";

                if (entryInfo.name.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Union '" << unionInfo.name << "' contains an entry with missing 'name' attribute" << COLOR_RESET;
                    m_hasError = true;
                }
                if (entryInfo.type.empty()) {
                    ERROR(logger) << COLOR_RED << locationStr() << " Union '" << unionInfo.name << "' entry '" << entryInfo.name << "' is missing required 'type' attribute" << COLOR_RESET;
                    m_hasError = true;
                }

                const char* arrayAttr = entry->Attribute("array");
                entryInfo.isArray = (arrayAttr && std::string(arrayAttr) == "true");

                if (entryInfo.isArray) {
                    entryInfo.size = entry->Attribute("size") ? entry->Attribute("size") : "";
                    if (entryInfo.size.empty()) {
                        requiredHeaders.insert("<vector>");
                    } else {
                        requiredHeaders.insert("<array>");
                    }
                }

                if (!entryInfo.type.empty()) {
                    convertType(entryInfo.type);
                }

                unionInfo.entries.push_back(entryInfo);
            }

            unionList.push_back(unionInfo);
            elementOrder.push_back({ElementKind::Union, unionList.size() - 1});
        }
    }

    return true;
}

void XMLToHeaderConverter::generateHeader(const std::string& outputFile) {
    std::ofstream out(outputFile);
    if (!out.is_open()) {
        ERROR(logger) << COLOR_RED << m_currentFile << ":0"
                << " Error opening output file: " << outputFile << COLOR_RESET;
        return;
    }

    bool hasSerializableStruct = false;
    for (const auto& structInfo : structList) {
        if (structInfo.needSerialization) {
            hasSerializableStruct = true;
            break;
        }
    }
    if (hasSerializableStruct) {
        requiredHeaders.insert("<chen/rpc/serializer.h>");
    }

    // Generate header guard
    std::string guardName = "__" + toUpper(headerName) + "_H__";

    // Write header
    out << "/**" << std::endl;
    out << " * @brief " << headerDesc << std::endl;
    out << " * @note " << metalibDesc << std::endl;
    out << " */" << std::endl;
    out << "#ifndef " << guardName << std::endl;
    out << "#define " << guardName << std::endl;
    out << std::endl;
    // Write required headers
    for (const std::string& header : requiredHeaders) {
        out << "#include " << header << std::endl;
    }
    out << std::endl;

    if (m_headerPack) {
        out << "#pragma pack(push, 1)" << std::endl;
    }
    out << std::endl;

    // Write standalone macros (defines)
    for (const auto& macro : macroList) {
        std::string defineName = toUpper(macro.name);
        std::string value = macro.value;

        if (macro.type == "string") {
            value = "\"" + value + "\"";
        }

        if (!macro.desc.empty()) {
            out << "#define " << defineName << " " << value << " /* " << macro.desc << " */" << std::endl;
        } else {
            out << "#define " << defineName << " " << value << std::endl;
        }
    }
    out << std::endl;

    // Write remaining elements following XML order (macros already emitted above)
    for (const auto& element : elementOrder) {
        switch (element.kind) {
        case ElementKind::MacroGroup: {
            const auto& group = macroGroupList[element.index];
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
                std::string enumName = toUpper(macro.name);
                if (!macro.desc.empty()) {
                    if (i == group.macros.size() - 1) {
                        out << "    " << enumName << " = " << macro.value << " /* " << macro.desc << " */" << std::endl;
                    } else {
                        out << "    " << enumName << " = " << macro.value << ", /* " << macro.desc << " */" << std::endl;
                    }
                } else {
                    if (i == group.macros.size() - 1) {
                        out << "    " << enumName << " = " << macro.value << std::endl;
                    } else {
                        out << "    " << enumName << " = " << macro.value << "," << std::endl;
                    }
                }
            }

            out << "};" << std::endl;
            out << std::endl;
            break;
        }
        case ElementKind::Struct: {
            const auto& structInfo = structList[element.index];
            if (isExternalType(structInfo.name)) {
                break;
            }

            if (structInfo.pack) {
                out << "#pragma pack(push, 1)" << std::endl;
            }

            if (!structInfo.desc.empty()) {
                out << "/**" << std::endl;
                out << " * @brief " << structInfo.desc << std::endl;
                out << " */" << std::endl;
            }

            out << "struct tag" << toCamelCase(structInfo.name) << " {" << std::endl;

            for (const EntryInfo& entry : structInfo.entries) {
                m_currentLine = entry.lineNum;
                std::string cppType = convertType(entry.type);
                if (m_hasError) {
                    break;
                }

                if (entry.isArray) {
                    if (!entry.size.empty()) {
                        bool found = false;
                        for (const auto& macro : macroList) {
                            if (macro.name == entry.size) {
                                std::string sizeConst = toUpper(entry.size);
                                cppType = "std::array<" + cppType + ", " + sizeConst + ">";
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            cppType = "std::array<" + cppType + ", " + entry.size + ">";
                        }
                    } else {
                        cppType = "std::vector<" + cppType + ">";
                    }
                }

                if (!entry.desc.empty()) {
                    out << "    " << cppType << " " << entry.name << "; /* " << entry.desc << " */" << std::endl;
                } else {
                    out << "    " << cppType << " " << entry.name << ";" << std::endl;
                }
            }

            out << "};" << std::endl;

            if (structInfo.pack) {
                out << "#pragma pack(pop)" << std::endl;
            }
            out << std::endl;

            if (structInfo.needSerialization) {
                generateStructSerialization(out, structInfo);
            }
            break;
        }
        case ElementKind::Union: {
            const auto& unionInfo = unionList[element.index];
            if (isExternalType(unionInfo.name)) {
                break;
            }

            if (!unionInfo.desc.empty()) {
                out << "/**" << std::endl;
                out << " * @brief " << unionInfo.desc << std::endl;
                out << " */" << std::endl;
            }

            out << "union tag" << toCamelCase(unionInfo.name) << " {" << std::endl;

            for (const EntryInfo& entry : unionInfo.entries) {
                m_currentLine = entry.lineNum;
                std::string cppType = convertType(entry.type);
                if (m_hasError) {
                    break;
                }

                if (entry.isArray) {
                    if (!entry.size.empty()) {
                        bool found = false;
                        for (const auto& macro : macroList) {
                            if (macro.name == entry.size) {
                                std::string sizeConst = toUpper(entry.size);
                                cppType = "std::array<" + cppType + ", " + sizeConst + ">";
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            cppType = "std::array<" + cppType + ", " + entry.size + ">";
                        }
                    } else {
                        cppType = "std::vector<" + cppType + ">";
                    }
                }

                if (!entry.desc.empty()) {
                    out << "    " << cppType << " " << entry.name << "; /* " << entry.desc << " */" << std::endl;
                } else {
                    out << "    " << cppType << " " << entry.name << ";" << std::endl;
                }
            }

            out << "};" << std::endl;
            out << std::endl;
            break;
        }
        }
    }

    if (m_headerPack) {
        out << "#pragma pack(pop)" << std::endl;
    }

    out << "#endif // " << guardName << std::endl;

    out.close();
    
    if (!hasError()) {
        INFO(logger) << "Header file generated successfully: " << outputFile;
    } else {
        ERROR(logger) << COLOR_RED << "Errors occurred during header generation. Please check the logs." << COLOR_RESET;
    }
}

std::string XMLToHeaderConverter::toUpper(const std::string& str) {
    std::string result = str;
    std::ranges::transform(result, result.begin(), ::toupper);
    return result;
}

void XMLToHeaderConverter::parseIncludeFile(const std::string& includeFile, const std::string& prefix) {
    tinyxml2::XMLDocument doc;
    std::string fullPath = includeFile;

    // Resolve relative path
    if (includeFile[0] != '/' && includeFile[0] != '.') {
        // Relative path without leading ./
        // Combine with current XML directory
        if (!currentXMLDir.empty()) {
            fullPath = currentXMLDir + "/" + includeFile;
        } else {
            fullPath = includeFile;
        }
    } else if (includeFile[0] == '.' && includeFile[1] == '/') {
        // Relative path with ./
        if (!currentXMLDir.empty()) {
            fullPath = currentXMLDir + includeFile.substr(1);
        } else {
            fullPath = includeFile.substr(2);
        }
    }

    if (doc.LoadFile(fullPath.c_str()) != tinyxml2::XML_SUCCESS) {
        WARN(logger) << fullPath << " Could not load include file";
        return;
    }

    tinyxml2::XMLElement* metalib = doc.FirstChildElement("metalib");
    if (!metalib) {
        WARN(logger) << fullPath << " Include file missing metalib";
        return;
    }

    tinyxml2::XMLElement* header = metalib->FirstChildElement("header");
    if (!header) {
        WARN(logger) << fullPath << " Include file missing header";
        return;
    }

    // Collect macro groups from included file
    for (tinyxml2::XMLElement* group = header->FirstChildElement("macrogroup"); group; group = group->NextSiblingElement("macrogroup")) {
        std::string groupName = group->Attribute("name") ? group->Attribute("name") : "";
        if (!groupName.empty()) {
            includedTypes.insert(groupName);
            std::string headerFile = extractHeaderName(fullPath);
            externalTypeMapping[groupName] = headerFile;
            requiredHeaders.insert("\"" + headerFile + "\"");
        }
    }

    // Collect structs from included file
    for (tinyxml2::XMLElement* structElem = header->FirstChildElement("struct"); structElem; structElem = structElem->NextSiblingElement("struct")) {
        std::string structName = structElem->Attribute("name") ? structElem->Attribute("name") : "";
        if (!structName.empty()) {
            includedTypes.insert(structName);
            std::string headerFile = extractHeaderName(fullPath);
            externalTypeMapping[structName] = headerFile;
            requiredHeaders.insert("\"" + headerFile + "\"");
        }
    }

    // Collect unions from included file
    for (tinyxml2::XMLElement* unionElem = header->FirstChildElement("union"); unionElem; unionElem = unionElem->NextSiblingElement("union")) {
        std::string unionName = unionElem->Attribute("name") ? unionElem->Attribute("name") : "";
        if (!unionName.empty()) {
            includedTypes.insert(unionName);
            std::string headerFile = extractHeaderName(fullPath);
            externalTypeMapping[unionName] = headerFile;
            requiredHeaders.insert("\"" + headerFile + "\"");
        }
    }
}

std::string XMLToHeaderConverter::extractHeaderName(const std::string& xmlPath) {
    // Convert XML file path to header file name
    // e.g., "resources.xml" -> "resources.h"
    size_t lastSlash = xmlPath.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos) ? xmlPath.substr(lastSlash + 1) : xmlPath;

    size_t lastDot = filename.find_last_of(".");
    if (lastDot != std::string::npos) {
        filename = filename.substr(0, lastDot);
    }

    return filename + ".h";
}

bool XMLToHeaderConverter::isExternalType(const std::string& type) {
    return includedTypes.find(type) != includedTypes.end();
}

bool XMLToHeaderConverter::hasError() const {
    return m_hasError;
}

std::string XMLToHeaderConverter::decodeHTMLEntities(const std::string& str) const {
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

int main(int argc, char* argv[]) {
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

        bool allSuccess = true;
        for (const auto& file : files) {
            std::string basename = chen::FSUtil::Basename(file);
            size_t lastDot = basename.find_last_of('.');
            if (lastDot != std::string::npos) {
                basename = basename.substr(0, lastDot);
            }
            std::string outputFile = output + "/" + basename + ".h";

            XMLToHeaderConverter converter;
            if (!converter.parseXML(file)) {
                ERROR(logger) << COLOR_RED << file << ":0 Failed to parse XML file" << COLOR_RESET;
                allSuccess = false;
                continue;
            }
            converter.generateHeader(outputFile);
            if (converter.hasError()) {
                ERROR(logger) << COLOR_RED
                      << "Failed to generate header due to type conversion errors in " << file << COLOR_RESET;
                allSuccess = false;
                return 1;
            }
        }

        return allSuccess ? 0 : 1;
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