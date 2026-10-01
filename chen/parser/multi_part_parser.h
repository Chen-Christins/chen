/**
 * @file multi_part_parser.h
 * @brief multipart/form-data 解析器
 * @author Christins
 * @date 2026-06-21
 * @copyright GPL-3.0
 */
#pragma once

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

    MultipartParser(const std::string& content_type);

    // ============ 流式接口 ============

    /**
     * @brief 分块喂数据，文件部分实时写入 save_dir
     * @param data 本次收到的数据块
     * @param save_dir 文件保存目录
     * @return false 表示解析出错
     */
    bool feed(const std::string& data, const std::string& save_dir);

    /**
     * @brief 是否已收到结束边界（--boundary--）
     */
    bool isComplete() const { return m_complete; }

    /// 文本字段
    struct FormField {
        std::string name;
        std::string value;
    };
    const std::vector<FormField>& getFields() const { return m_fields; }

    /// 已保存到磁盘的文件信息
    struct SavedFileInfo {
        std::string field_name;   ///< 表单字段名
        std::string filename;     ///< 原始文件名
        std::string content_type; ///< 文件 MIME 类型
        std::string path;         ///< 磁盘完整路径
        int64_t size;             ///< 文件字节数
    };
    const std::vector<SavedFileInfo>& getSavedFiles() const { return m_files; }

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

    /// 尝试从缓冲区中解析出一个完整的 part，返回解析的字节数（0 表示数据不够）
    size_t tryParsePart(const std::string& save_dir);

    /// 当前解析状态
    enum State {
        PREAMBLE,   ///< 第一个 boundary 之前
        HEADERS,    ///< 正在解析 part 头部
        BODY,       ///< 正在解析 part body
        COMPLETE    ///< 已收到结束边界
    };

    std::string m_boundary;
    std::string m_buffer;
    State m_state = PREAMBLE;
    bool m_complete = false;

    // 当前 part
    bool     m_curIsFile = false;
    std::string m_curFieldName;
    std::string m_curFileName;
    std::string m_curContentType;
    std::ofstream m_curFileStream;
    std::string m_curFieldValue;
    int64_t  m_curFileSize = 0;

    // 结果
    std::vector<FormField> m_fields;
    std::vector<SavedFileInfo> m_files;
};

} // namespace chen
