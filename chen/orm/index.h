/**
 * @file index.h
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
    const std::string& getName() const { return m_name; }
    /**
     * @brief 获取索引类型字符串
     * @return const std::string&
     */
    const std::string& getType() const { return m_type; }
    /**
     * @brief 获取索引描述
     * @return const std::string&
     */
    const std::string& getDesc() const { return m_desc; }
    /**
     * @brief 获取索引列
     * @return const std::vector<std::string>&
     */
    const std::vector<std::string>& getCols() const { return m_cols; }
    /**
     * @brief 获取索引类型
     * @return Type 
     */
    Type getDType() const { return m_dtype; }
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
    bool isPK() const { return m_type == "pk"; }
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
    std::string m_name;
    /// 索引类型
    std::string m_type;
    /// 索引描述
    std::string m_desc;
    /// 列名称
    std::vector<std::string> m_cols;

    Type m_dtype;
};

}
}
