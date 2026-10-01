/**
 * @file config.h
 * @brief 配置模块
 * @author Christins
 * @date 2024-11-06
 */
#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>

#include "../log/log.h"

namespace tinyxml2 {
class XMLElement;
}

namespace chen {

/**
 * @brief 配置系统的基类，提供主要的方法和参数
 * @details 为派生类提供toString、fromString的接口
 */
class ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;
    /**
     * @brief 构造函数
     * @param name 参数名称[0-9a~z_.]
     * @param description 配置的描述
     */
    ConfigVarBase(const std::string& name, const std::string& description = "")
        :m_name(name)
        ,m_description(description) {
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);
    }

    /**
     * @brief 析构函数
     */
    virtual ~ConfigVarBase() {}

    /**
     * @brief 获取配置名称name.xxxx点来区分层级
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief 获取配置的描述
     */
    const std::string& getDescription() const { return m_description; }

    /**
     * @brief 接口方法，将基础类型，stl容器，自定义类型转换为string类型
     */
    virtual std::string toString() = 0;

    /**
     * @brief 将string类型重新转换为源类型
     * @param val string类型
     */
    virtual bool fromString(const std::string& val) = 0;

    /**
     * @brief 返回类型名称，用于调试
     */
    virtual std::string getTypeName() const = 0;
private:
    /// 配置的名称
    std::string m_name;
    /// 配置的描述
    std::string m_description;
};

/**
 * @brief 类型转换的模板类
 * @tparam F 源类型
 * @tparam T 目标类型
 */
template <class F, class T>
class LexicalCast {
public:
    /**
     * @brief 类型转换(F -> T)
     * @param v 源类型
     * @return T将源F类型的v, 转换为目标类型T
     * @exception 类型转换失败，错误抛出异常
     */
    T operator()(const F& v) {
        return boost::lexical_cast<T>(v);
    }
};

// string 到 string 的特化，直接返回
template <>
class LexicalCast<std::string, std::string> {
public:
    std::string operator()(const std::string& v) {
        // 如果字符串以*开头，需要加引号避免被当作YAML别名
        if (!v.empty() && v[0] == '*') {
            return "\"" + v + "\"";
        }
        return v;
    }
};

// string -> bool 特化，兼容 YAML 的 true/false
template <>
class LexicalCast<std::string, bool> {
public:
    bool operator()(const std::string& v) {
        if (v == "true" || v == "1" || v == "True" || v == "TRUE") {
            return true;
        }
        if (v == "false" || v == "0" || v == "False" || v == "FALSE") {
            return false;
        }
        throw std::invalid_argument("Cannot convert '" + v + "' to bool");
    }
};

// bool -> string 特化，输出 YAML 规范的 true/false
template <>
class LexicalCast<bool, std::string> {
public:
    std::string operator()(const bool& v) {
        return v ? "true" : "false";
    }
};

/**
 * @brief 模板片特化
 * @details 将std::string类型转换为std::vector<T>类型
 */
template <class T>
class LexicalCast<std::string, std::vector<T>> {
public:
    std::vector<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::vector<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 模板片特化
 * @details 将std::vector<T>类型转换为std::string类型
 */
template <class T>
class LexicalCast<std::vector<T>, std::string> {
public:
    std::string operator()(const std::vector<T>& v) {
        YAML::Node node;
        for (auto& i : v) {
            std::string str = LexicalCast<T, std::string>()(i);
            // 对于字符串类型，使用YAML::Load以保持正确的格式
            node.push_back(YAML::Load(str));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 模板片特化
 * @details 将std::string类型转换为std::list<T>类型
 */
template <class T>
class LexicalCast<std::string, std::list<T>> {
public:
    std::list<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::list<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 模板片特化
 * @details 将std::list<T>类型转换为std::string类型
 */
template <class T>
class LexicalCast<std::list<T>, std::string> {
public:
    std::string operator()(const std::list<T>& v) {
        YAML::Node node;
        for (auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 模板片特化
 * @details 将std::string类型转换为std::set<T>类型
 */
template <class T>
class LexicalCast<std::string, std::set<T>> {
public:
    std::set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::set<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 模板片特化
 * @details 将std::set<T>类型转换为std::string类型
 */
template <class T>
class LexicalCast<std::set<T>, std::string> {
public:
    std::string operator()(const std::set<T>& v) {
        YAML::Node node;
        for (auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 模板片特化
 * @details 将std::string类型转换为std::unordered_set<T>类型
 */
template <class T>
class LexicalCast<std::string, std::unordered_set<T>> {
public:
    std::unordered_set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_set<T> vec;
        std::stringstream ss;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 模板片特化
 * @details 将std::unordered_set<T>类型转换为std::string类型
 */
template <class T>
class LexicalCast<std::unordered_set<T>, std::string> {
public:
    std::string operator()(const std::unordered_set<T>& v) {
        YAML::Node node;
        for (auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 模板片特化
 * @details 将std::string类型转换为std::map<std::string, T>类型
 */
template <class T>
class LexicalCast<std::string, std::map<std::string, T>> {
public:
    std::map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for (auto it = node.begin(); it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert({it->first.Scalar(), LexicalCast<std::string, T>()(ss.str())});
        }
        return vec;
    }
};

/**
 * @brief 模板片特化
 * @details 将std::map<std::string, T>类型转换为std::string类型
 */
template <class T>
class LexicalCast<std::map<std::string, T>, std::string> {
public:
    std::string operator()(const std::map<std::string, T>& v) {
        YAML::Node node;
        for (auto& [x, y] : v) {
            node[x] = YAML::Load(LexicalCast<T, std::string>()(y));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 模板片特化
 * @details 将std::string类型
 *          转换为std::unordered_map<std::string, T>类型
 */
template <class T>
class LexicalCast<std::string, std::unordered_map<std::string, T>> {
public:
    std::unordered_map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_map<std::string, T> vec;
        std::stringstream ss;
        for (auto it = node.begin(); it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert({it->first.Scalar(), LexicalCast<std::string, T>()(ss.str())});
        }
        return vec;
    }
};

/**
 * @brief 模板片特化
 * @details 将std::unordered_map<std::string, T>类型
 *          转换为std::string类型
 */
template <class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
    std::string operator()(const std::unordered_map<std::string, T>& v) {
        YAML::Node node;
        for (auto& [x, y] : v) {
            node[x] = YAML::Load(LexicalCast<T, std::string>()(y));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 配置参数模板子类，保存对应类型的参数值
 * @tparam T 参数的具体类型
 * @tparam FromStr 从std::string转换为T类型的仿函数
 * @tparam ToStr 从T转换为std::string类型的仿函数
 */
template <class T, class FromStr = LexicalCast<std::string, T>
    ,class ToStr = LexicalCast<T, std::string>>
class ConfigVar : public ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVar> ptr;
    typedef std::function<void(const T& old_value, const T& new_value)> on_change_cb;

    /**
     * @brief 构造函数，通过参数名，参数值描述构造ConfigVar
     * @param name 参数名称[0-9a~z_.]
     * @param default_value 默认参数值
     * @param description 参数的描述
     */
    ConfigVar(const std::string& name, const T& default_value
            ,const std::string& description = "")
        :ConfigVarBase(name, description)
        ,m_val(default_value) {
    }

    /**
     * @brief 获取T类型的值
     */
    const T getValue() {
        std::shared_lock lock(m_mutex);
        return m_val;
    }

    /**
     * @brief 设置当前参数的值
     * @details 如果参数的值有发生变化,则通知对应的注册回调函数
     */
    void setValue(const T& v) {
        {
            std::shared_lock lock(m_mutex);
            if (v == m_val) {
                return ;
            }
            for (auto& [idx, task] : m_cbs) {
                task(m_val, v);
            }
        }
        std::unique_lock lock(m_mutex);
        m_val = v;
    }

    /**
     * @brief 将参数值转换为Yaml string
     * @exception 转换失败抛出异常
     */
    std::string toString() override {
        try {
            std::unique_lock lock(m_mutex);
            return ToStr()(m_val);
        } catch (std::exception& e) {
            ERROR(LOG_NAME("system")) << "ConfigVar::toString exception" << e.what()
                << " convert: string to " << typeid(m_val).name()
                << " to string";
        }
        return "";
    }

    /**
     * @brief 将Yaml string转换为当前参数值类型
     * @exception 转换失败抛出异常
     */
    bool fromString(const std::string& val) override {
        try {
            setValue(FromStr()(val));
        } catch (std::exception& e) {
            ERROR(LOG_NAME("system")) << "ConfigVar::fromString exception" << e.what()
                << " convert: string to " << typeid(m_val).name() << " - " << val;
        }
        return false;
    }

    /**
     * @brief 获取类型名称
     */
    std::string getTypeName() const override { return typeid(T).name(); }

    /**
     * @brief 添加变换回调函数
     * @param cb 回调函数
     * @return uint64_t 返回回调函数对应的唯一id，用于删除回调
     */
    uint64_t addListener(on_change_cb cb) {
        static uint64_t s_fun_id = 0;
        std::unique_lock lock(m_mutex);
        ++s_fun_id;
        m_cbs[s_fun_id] = cb;
        return s_fun_id;
    }

    /**
     * @brief 删除id为key的回调函数
     * @param key 回调函数列表中存储的编号
     */
    void delListener(uint64_t key) {
        std::unique_lock lock(m_mutex);
        m_cbs.erase(key);
    }

    /**
     * @brief 获取编号为key的回调函数
     * @param key 回调函数组的编号
     * @return on_change_cb 回调函数，否则返回nullptr
     */
    on_change_cb getListener(uint64_t key) {
        std::shared_lock lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it->second;
    }

    /**
     * @brief 清空所有变更回调函数
     */
    void clearListener() {
        std::unique_lock lock(m_mutex);
        m_cbs.clear();
    }

private:
    /// c++17 读写锁
    std::shared_mutex m_mutex;
    T m_val;
    /// 变更回调函数组，uint64_t key, 要求唯一，一般可以用hash
    std::map<uint64_t, on_change_cb> m_cbs;
};

/**
 * @brief ConfigVar的管理类
 * @details 提供便捷的方法创建/访问ConfigVar
 */
class Config {
public:
    typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;
    /**
     * @brief 获取/创建对应参数名的默认参数
     * @tparam T 默认参数类型
     * @param name 参数名称[0-9a~z_.]
     * @param default_value 默认参数值
     * @details 获取参数名为name的配置参数,如果存在直接返回,
     *          如果不存在,创建参数配置并用default_value赋值
     * @param description 配置描述
     * @return ConfigVar<T>::ptr 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
     * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
     */
    template <class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name
            ,const T& default_value, const std::string& description = "") {
        std::unique_lock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it != GetDatas().end()) {
            /* 将ConfigVarBase 向下转换 -> ConfigVar */
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
            if (tmp) {
                // 改为TRACE日志，避免频繁打印INFO日志
                TRACE(LOG_NAME("system")) << "Lookup name=" << name << " exists";
                return tmp;
            } else {
                /* 类型不对，也就是向下转换的时候，抛出异常 */
                ERROR(LOG_NAME("system")) << "Lookup name=" << name << " exists but type not "
                    << typeid(T).name() << " real_type=" << it->second->getTypeName()
                    << " " << it->second->toString();
                return nullptr;
            }
        }

        if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789")
                != std::string::npos) {
            ERROR(LOG_NAME("system")) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }

        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }

    /**
     * @brief 查找配置参数
     * @tparam T 参数类型
     * @param name 配置参数名称
     * @return ConfigVar<T>::ptr 返回配置参数名为name的配置参数
     */
    template <class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name) {
        std::shared_lock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it == GetDatas().end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
    }

    /**
     * @brief 使用YAML::Node初始化配置模块
     */
    static void LoadFromYaml(const YAML::Node& root);

    /**
     * @brief 使用XML::Element初始化配置模块
     * @details 将XML元素树转换为YAML节点后复用LoadFromYaml的加载流程
     */
    static void LoadFromXml(const tinyxml2::XMLElement& root);

    /**
     * @brief 加载单个配置文件
     * @param filepath 配置文件绝对路径
     * @param force 强制加载（忽略 mtime 缓存）
     * @return bool 是否加载成功
     */
    static bool LoadFromFile(const std::string& filepath, bool force = false);

    /**
     * @brief 从文件夹中加载配置文件
     * @details 递归加载目录下所有 .yml 和 .xml 配置文件
     */
    static void LoadFromConfDir(const std::string& path, bool force = false);

    /**
     * @brief 查找配置参数,返回配置参数的基类
     * @param name 配置参数名称
     * @return ConfigVarBase::ptr 查找配置参数，返回配置参数的基类
     */
    static ConfigVarBase::ptr LookupBase(const std::string& name);

	static void Visit(std::function<void(ConfigVarBase::ptr)> callback);
private:
    /**
    * @brief 返回所有配置项
    */
    static ConfigVarMap& GetDatas() {
        static ConfigVarMap s_datas;
        return s_datas;
    }
    /**
     * @brief 创建静态读写锁
     */
    static std::shared_mutex& GetMutex() {
        static std::shared_mutex s_mutex;
        return s_mutex;
    }
};

}
