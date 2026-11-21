#include "Index.hpp"

#include "../Log/Log.hpp"
#include "../Util/StringUtil.hpp"
#include "../Util/Util.hpp"

namespace chen {
namespace orm {

static Logger::ptr logger = LOG_NAME("orm");

bool Index::init(const tinyxml2::XMLElement& node)
{
    if (!node.Attribute("name")) {
        ERROR(logger) << "index name not exists";
        return false;
    }

    name_ = node.Attribute("name");
    if (!node.Attribute("type")) {
        ERROR(logger) << "index name=" << name_ << " type is null";
        return false;
    }

    type_ = node.Attribute("type");
    dtype_ = ParseType(type_);
    if (dtype_ == TYPE_NULL) {
        ERROR(logger) << "index name=" << name_ << " type=" << type_ << " invalid(pk, index, uniq)";
        return false;
    }

    if (!node.Attribute("cols")) {
        ERROR(logger) << "index name=" << name_ << " cols is null";
    }
    std::string tmp = node.Attribute("cols");
    cols_ = StringUtil::Split(tmp, ',', false);

    if (node.Attribute("desc")) {
        desc_ = node.Attribute("desc");
    }
    return true;
}

Index::Type Index::ParseType(const std::string& v)
{
#define XX(a, b)  \
    if (v == b) { \
        return a; \
    }
    XX(TYPE_PK, "pk");
    XX(TYPE_UNIQ, "uniq");
    XX(TYPE_INDEX, "index");
#undef XX
    return TYPE_NULL;
}

std::string Index::TypeToString(Type v)
{
#define XX(a, b)  \
    if (v == a) { \
        return b; \
    }
    XX(TYPE_PK, "pk");
    XX(TYPE_UNIQ, "uniq");
    XX(TYPE_INDEX, "index");
#undef XX
    return "";
}

} // namespace orm
} // namespace chen
