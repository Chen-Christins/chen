#include "MultipartParser.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "../Log/Log.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

MultipartParser::MultipartParser(const std::string& content_type)
{
    size_t pos = content_type.find("boundary=");
    if (pos != std::string::npos) {
        boundary_ = content_type.substr(pos + 9);
        if (!boundary_.empty() && boundary_[0] == '"') {
            boundary_ = boundary_.substr(1, boundary_.length() - 2);
        }
    }
}

// ============ 流式接口 ============

bool MultipartParser::feed(const std::string& data, const std::string& save_dir)
{
    if (state_ == COMPLETE) {
        return true;
    }

    buffer_ += data;

    while (!buffer_.empty() && state_ != COMPLETE) {
        size_t consumed = tryParsePart(save_dir);
        if (consumed == 0) {
            break; // 数据不够，等下一块
        }
        buffer_.erase(0, consumed);
    }

    return true;
}

size_t MultipartParser::tryParsePart(const std::string& save_dir)
{
    switch (state_) {
    case PREAMBLE: {
        // 跳过第一个 --boundary 之前的所有数据（理论上没有）
        std::string delim = "--" + boundary_;
        size_t pos = buffer_.find(delim);
        if (pos == std::string::npos) {
            return 0;
        }
        size_t consumed = pos + delim.length();
        if (buffer_.size() >= consumed + 2 && buffer_.substr(consumed, 2) == "\r\n") {
            consumed += 2;
        }
        state_ = HEADERS;
        return consumed;
    }
    case HEADERS: {
        // 查找 header 结束位置 \r\n\r\n
        size_t header_end = buffer_.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            return 0;
        }

        std::string headers = buffer_.substr(0, header_end);
        cur_file_name_ = extractFilename(headers);
        cur_field_name_ = extractFieldName(headers);
        cur_content_type_ = "";
        cur_is_file_ = !cur_file_name_.empty();
        cur_file_size_ = 0;
        cur_field_value_.clear();

        // 提取 Content-Type
        std::string ct_key = "Content-Type: ";
        size_t ct_pos = headers.find(ct_key);
        if (ct_pos != std::string::npos) {
            ct_pos += ct_key.length();
            size_t ct_end = headers.find("\r\n", ct_pos);
            if (ct_end != std::string::npos) {
                cur_content_type_ = headers.substr(ct_pos, ct_end - ct_pos);
            }
        }

        if (cur_is_file_) {
            std::filesystem::create_directories(save_dir);
            cur_file_stream_.open(save_dir + "/" + cur_file_name_, std::ios::binary);
        }

        state_ = BODY;
        return header_end + 4; // 包括 \r\n\r\n
    }
    case BODY: {
        std::string boundary_str = "--" + boundary_;
        size_t pos = buffer_.find(boundary_str);

        if (pos == std::string::npos) {
            // 还没找到下一个 boundary
            if (cur_is_file_ && cur_file_stream_.is_open()) {
                // 文件数据：安全地写大部分到磁盘（保留 boundary 长度的尾部以免截断）
                size_t safe = buffer_.size() > boundary_.size() ? buffer_.size() - boundary_.size() : 0;
                if (safe > 0) {
                    cur_file_stream_.write(buffer_.data(), safe);
                    cur_file_size_ += safe;
                    buffer_.erase(0, safe);
                }
            }
            // 文本字段：数据留在 buffer 里，等找到 boundary 再一起提取
            return 0;
        }

        // 找到 boundary，提取 body 内容（去掉 boundary 前的 \r\n）
        size_t content_end = pos;
        bool has_trailing_crlf = (pos >= 2 && buffer_.substr(pos - 2, 2) == "\r\n");
        if (has_trailing_crlf) {
            content_end = pos - 2;
        }

        if (cur_is_file_) {
            if (cur_file_stream_.is_open()) {
                cur_file_stream_.write(buffer_.data(), content_end);
                cur_file_size_ += content_end;
                cur_file_stream_.close();
            }
            files_.push_back(
                {cur_field_name_, cur_file_name_, cur_content_type_, save_dir + "/" + cur_file_name_, cur_file_size_});
        } else {
            std::string text_content(buffer_.data(), content_end);
            cur_field_value_ += text_content;
            fields_.push_back({cur_field_name_, cur_field_value_});
            cur_field_value_.clear();
        }

        size_t consumed = pos + boundary_str.length();

        // 检查是否结束边界 --boundary--
        if (buffer_.size() >= consumed + 2 && buffer_.substr(consumed, 2) == "--") {
            state_ = COMPLETE;
            complete_ = true;
            return consumed + 2;
        }

        // 跳过 boundary 后的 \r\n
        if (buffer_.size() >= consumed + 2 && buffer_.substr(consumed, 2) == "\r\n") {
            consumed += 2;
        }

        state_ = HEADERS;
        return consumed;
    }
    case COMPLETE:
        return buffer_.size();
    }
    return 0;
}

// ============ 旧接口（向后兼容） ============

bool MultipartParser::parse(const std::string& data, const std::string& save_dir)
{
    if (boundary_.empty()) {
        ERROR(logger) << "Boundary not found";
        return false;
    }

    std::filesystem::create_directories(save_dir);

    std::string delimiter = "--" + boundary_;
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
            if (next_boundary - 2 >= body_start && data.substr(next_boundary - 2, 2) == "\r\n") {
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

std::vector<MultipartParser::FilePart> MultipartParser::parseToMemory(const std::string& data)
{
    std::vector<FilePart> files;
    if (boundary_.empty()) {
        return files;
    }

    std::string delimiter = "--" + boundary_;
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
        f.filename = filename;
        f.field_name = extractFieldName(headers);

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
        if (next_boundary - 2 >= body_start && data.substr(next_boundary - 2, 2) == "\r\n") {
            f.content = data.substr(body_start, content_length);
        } else {
            f.content = data.substr(body_start, next_boundary - body_start);
        }

        files.push_back(f);
        pos = next_boundary;
    }
    return files;
}

std::string MultipartParser::extractFilename(const std::string& headers)
{
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

std::string MultipartParser::extractFieldName(const std::string& headers)
{
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
