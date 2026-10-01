#include "json_util.h"

namespace chen {

bool JsonUtil::FromString(Json::Value& json, const std::string& v) {
    Json::Reader reader;
    return reader.parse(v, json);
}

std::string JsonUtil::ToString(const Json::Value& json) {
    Json::FastWriter w;
    return w.write(json);
}

}
