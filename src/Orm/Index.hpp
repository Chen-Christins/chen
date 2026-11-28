/**
 * @file Index.hpp
 * @brief orm解析xml数据生成sql数据
 * @author Christins
 * @date 2025-05-08
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <tinyxml2.h>

namespace chen {
namespace orm {

class Index {
public:
    // 索引类型
    enum Type {
        TYPE_NULL = 0,
        TYPE_PK,
        TYPE_UNIQ,
        TYPE_INDEX
    };
    typedef std::shared_ptr<Index> ptr;
    /**
     * @brief 获取索引名称
     * @return const std::string&
     */
    const std::string& getName() const { return name_; }
    /**
     * @brief 获取索引类型字符串
     * @return const std::string&
     */
    const std::string& getType() const { return type_; }
    /**
     * @brief 获取索引描述
     * @return const std::string&
     */
    const std::string& getDesc() const { return desc_; }
    /**
     * @brief 获取索引列
     * @return const std::vector<std::string>&
     */
    const std::vector<std::string>& getCols() const { return cols_; }
    /**
     * @brief 获取索引类型
     * @return Type
     */
    Type getDType() const { return dtype_; }
    /**
     * @brief 初始化索引
     * @param node
     * @return bool
     */
    bool init(const tinyxml2::XMLElement& node);
    /**
     * @brief 是否为主键索引
     * @return bool
     */
    bool isPK() const { return type_ == "pk"; }
    /**
     * @brief 解析索引类型(string -> Type)
     * @param v
     * @return Type
     */
    static Type ParseType(const std::string& v);
    /**
     * @brief 解析索引类型(Type -> string)
     * @param v
     * @return std::string
     */
    static std::string TypeToString(Type v);

private:
    /// 索引名称
    std::string name_;
    /// 索引类型
    std::string type_;
    /// 索引描述
    std::string desc_;
    /// 列名称
    std::vector<std::string> cols_;

    Type dtype_;
};

} // namespace orm
} // namespace chen
