/**
 * @file MultipartParser.hpp
 * @brief multipart/form-data 解析器
 * @author Christins
 * @date 2026-06-21
 * @copyright GPL-3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace chen {

/**
 * @brief multipart/form-data 解析器
 *
 * 支持两种模式：
 *   1. parse() / parseToMemory() — 一次性解析完整 body
 *   2. feed() 流式解析 — 分块喂数据，文件字段直接写磁盘
 */
class MultipartParser {
public:
    typedef std::shared_ptr<MultipartParser> ptr;

    /**
     * @brief 解析上限
     *
     * multipart 解析的输入完全来自对端，所有尺寸都必须有硬上限，
     * 否则一个构造过的请求就能把工作区内存撑爆（尤其是文本字段要
     * 攒到下一个 boundary 才能提取）。
     */
    struct Limits {
        size_t max_body_size = 64ull * 1024 * 1024; ///< 整个 body 的累计上限
        size_t max_header_size = 16ull * 1024;      ///< 单个 part 头部上限
        size_t max_field_size = 8ull * 1024 * 1024; ///< 单个文本字段上限
        size_t max_file_size = 64ull * 1024 * 1024; ///< 单个文件上限
        size_t max_parts = 1024;                    ///< part 数量上限
        size_t max_boundary_length = 70;            ///< boundary 长度上限（RFC 2046）
    };

    MultipartParser(const std::string& content_type);

    /// 调整解析上限（单测与小流量入口可调小）
    void setLimits(const Limits& limits) { limits_ = limits; }
    const Limits& getLimits() const { return limits_; }

    // ============ 流式接口 ============

    /**
     * @brief 分块喂数据，文件部分实时写入 save_dir
     * @param data 本次收到的数据块
     * @param save_dir 文件保存目录
     * @return false 表示解析出错（超过 Limits、文件名非法、写盘失败等），此后应停止投喂
     */
    bool feed(const std::string& data, const std::string& save_dir);

    /**
     * @brief 是否已收到结束边界（--boundary--）
     */
    bool isComplete() const { return complete_; }

    /// 是否已经解析失败（失败后 feed 恒返回 false）
    bool isError() const { return failed_; }

    /// 文本字段
    struct FormField {
        std::string name;
        std::string value;
    };
    const std::vector<FormField>& getFields() const { return fields_; }

    /// 已保存到磁盘的文件信息
    struct SavedFileInfo {
        std::string field_name;   ///< 表单字段名
        std::string filename;     ///< 原始文件名
        std::string content_type; ///< 文件 MIME 类型
        std::string path;         ///< 磁盘完整路径
        int64_t size;             ///< 文件字节数
    };
    const std::vector<SavedFileInfo>& getSavedFiles() const { return files_; }

    // ============ 旧接口（向后兼容） ============

    /// 解析并保存文件到目录，返回 true 表示有文件被保存
    bool parse(const std::string& data, const std::string& save_dir);

    struct FilePart {
        std::string filename;
        std::string field_name;
        std::string content;
        std::string content_type;
    };
    std::vector<FilePart> parseToMemory(const std::string& data);

private:
    std::string extractFilename(const std::string& headers);
    std::string extractFieldName(const std::string& headers);

    /// 从缓冲区解析一个 part，返回消费的字节数（0 表示数据不够；失败时 failed_ 为 true）
    size_t tryParsePart(const std::string& save_dir);

    /// 当前解析状态
    enum State {
        PREAMBLE, ///< 第一个 boundary 之前
        HEADERS,  ///< 正在解析 part 头部
        BODY,     ///< 正在解析 part body
        COMPLETE  ///< 已收到结束边界
    };

    std::string boundary_;
    std::string buffer_;
    State state_ = PREAMBLE;
    bool complete_ = false;
    bool failed_ = false;
    size_t offset_ = 0;         ///< buffer_ 中已消费的前缀长度，避免反复 erase 搬移
    size_t total_received_ = 0; ///< 累计收到的 body 字节数

    // 当前 part
    bool cur_is_file_ = false;
    std::string cur_field_name_;
    std::string cur_file_name_;
    std::string cur_content_type_;
    std::ofstream cur_file_stream_;
    std::string cur_field_value_;
    std::string cur_file_path_; ///< 实际落盘路径（已去掉目录并做重名处理）
    int64_t cur_file_size_ = 0;

    // 结果
    std::vector<FormField> fields_;
    std::vector<SavedFileInfo> files_;

    Limits limits_;
};

} // namespace chen
