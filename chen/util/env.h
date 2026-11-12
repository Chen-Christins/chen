/**
 * @file env.h
 * @brief 参数解析
 * @author Christins
 * @date 2025-03-01
 * @copyright GPL-3.0
 */
#pragma once

#include <cstddef>
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

    /**
     * @brief 设置进程标题的基础名
     * @details 最终 ps / /proc/pid/cmdline 显示为 "<base>: <role>"；
     *          传空则回退到可执行文件名。必须在 init 之后调用。
     * @param name 基础名，例如 "TechHaven-Server"
     */
    void setProcTitleBase(const std::string& name);

    /**
     * @brief 设置进程标题的角色部分
     * @details 最终显示为 "<base>: <role>"，例如 "worker process"；传空则只显示基础名。
     * @param role 角色名
     */
    void setProcTitleRole(const std::string& role);

    /**
     * @brief 直接设置完整的进程标题
     * @details 不改变基础名和角色，后续调用 setProcTitleBase/setProcTitleRole 会重新组合覆盖它。
     * @param title 完整标题，超出可复写区域的部分会被截断
     */
    void setProcTitle(const std::string& title);

    /**
     * @brief 获取当前进程标题
     */
    std::string getProcTitle();

    /**
     * @brief 设置 /proc/pid/comm（top、pkill 默认读取的名字）
     * @details 只影响调用线程，且最长 15 字节，超出会被截断。
     * @param name 线程名
     */
    void setProcComm(const std::string& name);

private:
    /**
     * @brief 记录 argv/environ 中可复写的标题区域
     * @details 必须在 main 早期、任何 setenv 之前调用，由 init 负责。
     */
    void initProcTitle(int argc, char** argv);

    /**
     * @brief 按基础名 + 角色组合出当前标题（调用方需持有 m_mutex）
     */
    std::string buildProcTitleLocked() const;

    /**
     * @brief 把标题写入 argv 区域并清空剩余字节（调用方需持有 m_mutex）
     */
    void writeProcTitleLocked(const std::string& title);

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
    /// 可复写的进程标题区域起始地址（argv[0]）
    char* m_titleBuf = nullptr;
    /// 可复写的进程标题区域字节数
    std::size_t m_titleMax = 0;
    /// 可执行文件名，作为基础名的兜底
    std::string m_titleDefault;
    /// 用户设置的基础名，为空时使用 m_titleDefault
    std::string m_titleBase;
    /// 用户设置的角色名
    std::string m_titleRole;
    /// 当前完整进程标题
    std::string m_title;
};

typedef Singleton<Env> EnvMgr;

} // namespace chen
