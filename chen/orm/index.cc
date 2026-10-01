#include "index.h"

#include "../log/log.h"
#include "../util/string_util.h"
#include "../util/util.h"

namespace chen {
namespace orm {

static Logger::ptr logger = LOG_NAME("orm");

bool Index::init(const tinyxml2::XMLElement& node) {
    if (!node.Attribute("name")) {
        ERROR(logger) << "index name not exists";
        return false;
    }

    m_name = node.Attribute("name");
    if (!node.Attribute("type")) {
        ERROR(logger) << "index name=" << m_name << " type is null";
        return false;
    }

    m_type = node.Attribute("type");
    m_dtype = ParseType(m_type);
    if (m_dtype == TYPE_NULL) {
        ERROR(logger) << "index name=" << m_name << " type=" << m_type
            << " invalid(pk, index, uniq)";
        return false;
    }

    if (!node.Attribute("cols")) {
        ERROR(logger) << "index name=" << m_name << " cols is null";
    }
    std::string tmp = node.Attribute("cols");
    m_cols = StringUtil::Split(tmp, ',', false);

    if (node.Attribute("desc")) {
        m_desc = node.Attribute("desc");
    }
    return true;
}

Index::Type Index::ParseType(const std::string& v) {
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

std::string Index::TypeToString(Type v) {
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

}
}
