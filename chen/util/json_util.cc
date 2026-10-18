#include "json_util.h"

namespace chen {

bool JsonUtil::FromString(Json::Value& json, const std::string& v)
{
    Json::Reader reader;
    return reader.parse(v, json);
}

std::string JsonUtil::ToString(const Json::Value& json, bool is_styled)
{
    if (!is_styled) {
        Json::FastWriter w;
        return w.write(json);
    } else {
        Json::StyledWriter w;
        return w.write(json);
    }
}

} // namespace chen
