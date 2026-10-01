/**
 * @file fs_util.h
 * @brief 文件系统工具类
 * @author Christins
 * @date 2026-06-26
 */
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace chen {

class FSUtil {
public:
    /**
     * @brief 列出路径下的所有文件
     * @param files 返回所有的文件名称
     * @param path 路径
     * @param subfix 文件后缀
     */
    static void ListAllFile(std::vector<std::string>& files, const std::string& path, const std::string& subfix);

    /**
     * @brief 判断pid文件是否存在，也就是判断进程是否存在
     * @param pidfile pid文件信息
     * @return bool 判断进程是否存在
     */
    static bool IsRunningPidfile(const std::string& pidfile);

    /**
     * @brief 创建文件夹
     * @param dirname 文件名
     * @return bool 是否创建成功
     */
    static bool Mkdir(const std::string& dirname);

    /**
     * @brief 返回文件所在的文件夹名称
     * @param filename 文件名称
     * @return std::string 文件夹路径名称
     */
    static std::string Dirname(const std::string& filename);

    /**
     * @brief 打开文件以写入的模式
     * @param ofs 文件流
     * @param filename 要打开的文件名称
     * @param mode 打开文件的模式
     * @return bool 是否成功
     */
    static bool OpenForWrite(std::ofstream& ofs, const std::string& filename, std::ios_base::openmode mode);

    /**
     * @brief 取消文件的链接信息
     * @param filename 文件名称
     * @param exist
     * @return bool
     */
    static bool Unlink(const std::string& filename, bool exist = false);

    /**
     * @brief 返回文件名称
     * @param path 文件路径
     * @return std::string 文件名称
     */
    static std::string Basename(const std::string& path);

    /**
     * @brief 读取文件内容到字符串
     * @param filename 文件名称
     * @return std::string 文件内容字符串
     */
    static std::string ReadFileToString(const std::string& filename);

    /**
     * @brief 计算目录下所有文件的合计大小(不包含子目录)
     * @param path 目录路径
     * @return { int32_t 文件数量, uint64_t 目录大小 }
     */
    static std::pair<int32_t, uint64_t> DirSize(const std::string& path);

    /**
     * @brief 判断文件或目录是否存在
     * @param path 路径
     * @return bool
     */
    static bool Exists(const std::string& path);

    /**
     * @brief 判断是否为目录
     * @param path 路径
     * @return bool
     */
    static bool IsDirectory(const std::string& path);

    /**
     * @brief 获取单个文件大小
     * @param filename 文件名称
     * @return int64_t 文件大小，失败返回 -1
     */
    static int64_t FileSize(const std::string& filename);

    /**
     * @brief 获取当前工作目录
     * @return std::string 当前工作目录路径
     */
    static std::string GetCwd();
};

} // namespace chen
