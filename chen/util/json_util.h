/**
 * @file json_util.h
 * @brief json相关的工具函数
 * @author Christins
 * @date 2025-05-05
 * @copyright GPL-3.0
 */
#pragma once

#include <json/json.h>

namespace chen {

class JsonUtil {
public:
    /**
     * @brief 将std::string -> Json
     * @param json Json数据
     * @param v 返回的字符串 std::string
     * @return bool 是否转换成功
     */
    static bool FromString(Json::Value& json, const std::string& v);

    /**
     * @brief 将Json -> std::string
     * @param json 源json数据
     * @return std::string 返回转换后的字符串
     */
    static std::string ToString(const Json::Value& json);
};

}
