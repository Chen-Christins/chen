#include "multi_part_parser.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "../log/log.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

MultipartParser::MultipartParser(const std::string& content_type) {
    size_t pos = content_type.find("boundary=");
    if (pos != std::string::npos) {
        m_boundary = content_type.substr(pos + 9);
        if (!m_boundary.empty() && m_boundary[0] == '"') {
            m_boundary = m_boundary.substr(1, m_boundary.length() - 2);
        }
    }
}

// ============ 流式接口 ============

bool MultipartParser::feed(const std::string& data, const std::string& save_dir) {
    if (m_state == COMPLETE) {
        return true;
    }

    m_buffer += data;

    while (!m_buffer.empty() && m_state != COMPLETE) {
        size_t consumed = tryParsePart(save_dir);
        if (consumed == 0) {
            break;  // 数据不够，等下一块
        }
        m_buffer.erase(0, consumed);
    }

    return true;
}

size_t MultipartParser::tryParsePart(const std::string& save_dir) {
    switch (m_state) {
    case PREAMBLE: {
        // 跳过第一个 --boundary 之前的所有数据（理论上没有）
        std::string delim = "--" + m_boundary;
        size_t pos = m_buffer.find(delim);
        if (pos == std::string::npos) {
            return 0;
        }
        size_t consumed = pos + delim.length();
        if (m_buffer.size() >= consumed + 2 && m_buffer.substr(consumed, 2) == "\r\n") {
            consumed += 2;
        }
        m_state = HEADERS;
        return consumed;
    }
    case HEADERS: {
        // 查找 header 结束位置 \r\n\r\n
        size_t header_end = m_buffer.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            return 0;
        }

        std::string headers = m_buffer.substr(0, header_end);
        m_curFileName    = extractFilename(headers);
        m_curFieldName   = extractFieldName(headers);
        m_curContentType = "";
        m_curIsFile      = !m_curFileName.empty();
        m_curFileSize    = 0;
        m_curFieldValue.clear();

        // 提取 Content-Type
        std::string ct_key = "Content-Type: ";
        size_t ct_pos = headers.find(ct_key);
        if (ct_pos != std::string::npos) {
            ct_pos += ct_key.length();
            size_t ct_end = headers.find("\r\n", ct_pos);
            if (ct_end != std::string::npos) {
                m_curContentType = headers.substr(ct_pos, ct_end - ct_pos);
            }
        }

        if (m_curIsFile) {
            std::filesystem::create_directories(save_dir);
            m_curFileStream.open(save_dir + "/" + m_curFileName, std::ios::binary);
        }

        m_state = BODY;
        return header_end + 4;  // 包括 \r\n\r\n
    }
    case BODY: {
        std::string boundary_str = "--" + m_boundary;
        size_t pos = m_buffer.find(boundary_str);

        if (pos == std::string::npos) {
            // 还没找到下一个 boundary
            if (m_curIsFile && m_curFileStream.is_open()) {
                // 文件数据：安全地写大部分到磁盘（保留 boundary 长度的尾部以免截断）
                size_t safe = m_buffer.size() > m_boundary.size()
                    ? m_buffer.size() - m_boundary.size() : 0;
                if (safe > 0) {
                    m_curFileStream.write(m_buffer.data(), safe);
                    m_curFileSize += safe;
                    m_buffer.erase(0, safe);
                }
            }
            // 文本字段：数据留在 buffer 里，等找到 boundary 再一起提取
            return 0;
        }

        // 找到 boundary，提取 body 内容（去掉 boundary 前的 \r\n）
        size_t content_end = pos;
        bool has_trailing_crlf = (pos >= 2 && m_buffer.substr(pos - 2, 2) == "\r\n");
        if (has_trailing_crlf) {
            content_end = pos - 2;
        }

        if (m_curIsFile) {
            if (m_curFileStream.is_open()) {
                m_curFileStream.write(m_buffer.data(), content_end);
                m_curFileSize += content_end;
                m_curFileStream.close();
            }
            m_files.push_back({m_curFieldName, m_curFileName, m_curContentType,
                save_dir + "/" + m_curFileName, m_curFileSize});
        } else {
            std::string text_content(m_buffer.data(), content_end);
            m_curFieldValue += text_content;
            m_fields.push_back({m_curFieldName, m_curFieldValue});
            m_curFieldValue.clear();
        }

        size_t consumed = pos + boundary_str.length();

        // 检查是否结束边界 --boundary--
        if (m_buffer.size() >= consumed + 2
            && m_buffer.substr(consumed, 2) == "--") {
            m_state  = COMPLETE;
            m_complete = true;
            return consumed + 2;
        }

        // 跳过 boundary 后的 \r\n
        if (m_buffer.size() >= consumed + 2
            && m_buffer.substr(consumed, 2) == "\r\n") {
            consumed += 2;
        }

        m_state = HEADERS;
        return consumed;
    }
    case COMPLETE:
        return m_buffer.size();
    }
    return 0;
}

// ============ 旧接口（向后兼容） ============

bool MultipartParser::parse(const std::string& data, const std::string& save_dir) {
    if (m_boundary.empty()) {
        ERROR(logger) << "Boundary not found";
        return false;
    }

    std::filesystem::create_directories(save_dir);

    std::string delimiter = "--" + m_boundary;
    size_t pos = 0;
    bool files_saved = false;

    while ((pos = data.find(delimiter, pos)) != std::string::npos) {
        pos += delimiter.length();
        if (pos + 2 <= data.length() && data.substr(pos, 2) == "--") {
            break;
        }
        if (pos + 2 <= data.length() && data.substr(pos, 2) == "\r\n") {
            pos += 2;
        }

        size_t header_end = data.find("\r\n\r\n", pos);
        if (header_end == std::string::npos) {
            ERROR(logger) << "Header end not found";
            return false;
        }

        std::string headers = data.substr(pos, header_end - pos);
        size_t body_start = header_end + 4;
        size_t next_boundary = data.find(delimiter, body_start);
        if (next_boundary == std::string::npos) {
            ERROR(logger) << "Next boundary not found";
            return false;
        }

        std::string filename = extractFilename(headers);
        if (!filename.empty()) {
            std::string full_path = save_dir + "/" + filename;
            size_t content_length = next_boundary - body_start - 2;
            if (next_boundary - 2 >= body_start
                && data.substr(next_boundary - 2, 2) == "\r\n") {
                content_length = next_boundary - body_start - 2;
            } else {
                content_length = next_boundary - body_start;
            }

            std::string file_content = data.substr(body_start, content_length);
            std::ofstream file(full_path, std::ios::binary);
            if (file.is_open()) {
                file.write(file_content.c_str(), file_content.size());
                file.close();
                INFO(logger) << "File saved: " << full_path;
                files_saved = true;
            } else {
                ERROR(logger) << "Failed to open file: " << full_path;
            }
        }

        pos = next_boundary;
    }
    return files_saved;
}

std::vector<MultipartParser::FilePart> MultipartParser::parseToMemory(const std::string& data) {
    std::vector<FilePart> files;
    if (m_boundary.empty()) {
        return files;
    }

    std::string delimiter = "--" + m_boundary;
    size_t pos = 0;

    while ((pos = data.find(delimiter, pos)) != std::string::npos) {
        pos += delimiter.length();
        if (pos + 2 <= data.length() && data.substr(pos, 2) == "--") {
            break;
        }
        if (pos + 2 <= data.length() && data.substr(pos, 2) == "\r\n") {
            pos += 2;
        }

        size_t header_end = data.find("\r\n\r\n", pos);
        if (header_end == std::string::npos) {
            break;
        }

        std::string headers = data.substr(pos, header_end - pos);
        size_t body_start = header_end + 4;
        size_t next_boundary = data.find(delimiter, body_start);
        if (next_boundary == std::string::npos) {
            break;
        }

        std::string filename = extractFilename(headers);
        FilePart f;
        f.filename    = filename;
        f.field_name  = extractFieldName(headers);

        std::string ct_key = "Content-Type: ";
        size_t ct_pos = headers.find(ct_key);
        if (ct_pos != std::string::npos) {
            ct_pos += ct_key.length();
            size_t ct_end = headers.find("\r\n", ct_pos);
            if (ct_end != std::string::npos) {
                f.content_type = headers.substr(ct_pos, ct_end - ct_pos);
            }
        }

        size_t content_length = next_boundary - body_start - 2;
        if (next_boundary - 2 >= body_start
            && data.substr(next_boundary - 2, 2) == "\r\n") {
            f.content = data.substr(body_start, content_length);
        } else {
            f.content = data.substr(body_start, next_boundary - body_start);
        }

        files.push_back(f);
        pos = next_boundary;
    }
    return files;
}

std::string MultipartParser::extractFilename(const std::string& headers) {
    std::string key = "filename=\"";
    size_t pos = headers.find(key);
    if (pos == std::string::npos) {
        return "";
    }
    pos += key.length();
    size_t end = headers.find("\"", pos);
    if (end == std::string::npos) {
        return "";
    }
    std::string filename = headers.substr(pos, end - pos);
    size_t slash = filename.find_last_of("/\\");
    if (slash != std::string::npos) {
        filename = filename.substr(slash + 1);
    }
    return filename;
}

std::string MultipartParser::extractFieldName(const std::string& headers) {
    std::string key = "name=\"";
    size_t pos = headers.find(key);
    if (pos == std::string::npos) {
        return "";
    }
    pos += key.length();
    size_t end = headers.find("\"", pos);
    if (end == std::string::npos) {
        return "";
    }
    return headers.substr(pos, end - pos);
}

} // namespace chen
