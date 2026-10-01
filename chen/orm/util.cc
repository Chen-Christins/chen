#include "util.h"

#include <algorithm>
#include <sstream>

#include "../util/util.h"

namespace chen {
namespace orm {

std::string GetAsClassName(const std::string& v) {
    auto vs = StringUtil::Split(v, '_');
    std::stringstream ss;
    for (auto& i : vs) {
        i[0] = std::toupper(i[0]);
        ss << i;
    }
    return ss.str();
}

std::string GetAsMemberName(const std::string& v) {
    auto class_name = GetAsClassName(v);
    class_name[0] = std::tolower(class_name[0]);
    return "m_" + class_name;
}

std::string GetAsGetFunName(const std::string& v) {
    auto class_name = GetAsClassName(v);
    return "get" + class_name;
}

std::string GetAsSetFunName(const std::string& v) {
    auto class_name = GetAsClassName(v);
    return "set" + class_name;
}

std::string XmlToString(const tinyxml2::XMLElement& node) {
    return "";
}

std::string GetAsDefineMacro(const std::string& v) {
    std::string str = StringUtil::Replace(v, '.', '_');
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

std::string GetAsVariable(const std::string& v) {
    std::string str = v;
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

}
}
