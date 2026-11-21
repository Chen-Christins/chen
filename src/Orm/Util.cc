#include "Util.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "../Util/Util.hpp"

namespace chen {
namespace orm {

std::string GetAsClassName(const std::string& v)
{
    auto vs = StringUtil::Split(v, '_');
    std::stringstream ss;
    for (auto& i : vs) {
        i[0] = std::toupper(i[0]);
        ss << i;
    }
    return ss.str();
}

std::string GetAsMemberName(const std::string& v)
{
    std::string s;
    s.reserve(v.size() + 4);
    for (size_t i = 0; i < v.size(); ++i) {
        char c = v[i];
        if (std::isupper(static_cast<unsigned char>(c))) {
            if (i != 0 && v[i - 1] != '_') {
                s += '_';
            }
            s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            s += c;
        }
    }
    return s + "_";
}

std::string GetAsGetFunName(const std::string& v)
{
    auto class_name = GetAsClassName(v);
    return "get" + class_name;
}

std::string GetAsSetFunName(const std::string& v)
{
    auto class_name = GetAsClassName(v);
    return "set" + class_name;
}

std::string XmlToString(const tinyxml2::XMLElement& node)
{
    return "";
}

std::string GetAsDefineMacro(const std::string& v)
{
    std::string str = StringUtil::Replace(v, '.', '_');
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

std::string GetAsVariable(const std::string& v)
{
    std::string str = v;
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

} // namespace orm
} // namespace chen
