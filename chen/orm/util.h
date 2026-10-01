/**
 * @file util.h
 * @brief orm工具函数
 * @author Christins
 * @date 2025-05-09
 * @copyright GPL-3.0
 */
#pragma once

#include <string>

#include <tinyxml2.h>

namespace chen {
namespace orm {

std::string GetAsClassName(const std::string& v);
std::string GetAsMemberName(const std::string& v);
std::string GetAsGetFunName(const std::string& v);
std::string GetAsSetFunName(const std::string& v);
std::string XmlToString(const tinyxml2::XMLElement& node);
std::string GetAsDefineMacro(const std::string& v);
std::string GetAsVariable(const std::string& v);

}
}
