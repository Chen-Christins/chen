/**
 * @file env.h
 * @brief 参数解析
 * @author Christins
 * @date 2025-03-01
 */
#pragma once

#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

#include "singleton.h"

namespace chen {

class Env {
public:
    /**
     * @brief 环境变量初始化
     * @details 将参数值放入m_args中
     * @param argc 参数个数
     * @param argv 参数值
     * @return bool 是否初始化成功
     */
    bool init(int argc, char** argv);
    
    /**
     * @brief 添加一个环境变量
     * @param key 环境变量的名称
     * @param val 环境变量的值
     */
    void add(const std::string& key, const std::string& val);

    /**
     * @brief 判断是否存在某个环境变量
     * @param key 环境变量的名称
     * @return bool 是否存在
     */
    bool has(const std::string& key);

    /**
     * @brief 删除某个环境变量
     * @param key 环境变量的名称
     */
    void del(const std::string& key);

    /**
     * @brief 获取某个环境变量的值
     * @param key 环境变量的名称
     * @param val 当找不到时返回的默认值
     * @return std::string 返回环境变量的值
     */
    std::string get(const std::string& key, const std::string& val = "");

    /**
     * @brief 添加帮助信息
     * @param key 环境变量的名称
     * @param desc 环境变量的描述
     */
    void addHelp(const std::string& key, const std::string& desc);

    /**
     * @brief 删除帮助信息
     * @param key 环境变量的名称
     */
    void removeHelp(const std::string& key);

    /**
     * @brief 打印帮助信息
     */
    void printHelp();

    /**
     * @brief 获取可执行文件的名称
     */
    const std::string& getExe() const { return m_exe; }

    /**
     * @brief 获取可执行文件所在的路径
     */
    const std::string& getCwd() const { return m_cwd; }

    /**
     * @brief 设置环境变量
     * @param key 环境变量的名称
     * @param val 环境变量的值
     * @return bool 是否设置成功
     */
    bool setEnv(const std::string& key, const std::string& val);

    /**
     * @brief 根据环境变量的名称，获取环境变量的值
     * @param key 环境变量的名称
     * @param default_val 当找不到时直接返回默认值
     * @return std::string 返回值类型
     */
    std::string getEnv(const std::string& key, const std::string& default_val = "");

    /**
     * @brief 获取path所在的绝对路径
     * @param path 传入的path
     * @return std::string 返回值类型
     */
    std::string getAbsolutePath(const std::string& path) const;

    /**
     * @brief 根据yaml配置文件，获取绝对的工作路径
     * @param path 
     * @return std::string 
     */
    std::string getAbsoluteWorkPath(const std::string& path) const;

    /**
     * @brief 获取配置文件夹路径
     * @return std::string 返回值类型
     */
    std::string getConfigPath();
private:
    /// 读写锁
    std::shared_mutex m_mutex;
    /// 参数信息
    std::map<std::string, std::string> m_args;
    /// 程序的帮助信息
    std::vector<std::pair<std::string, std::string>> m_helps;
    /// 可执行程序名称
    std::string m_program;
    /// 可执行文件
    std::string m_exe;
    /// 可执行文件所在的路径
    std::string m_cwd;
};

typedef Singleton<Env> EnvMgr;

}
