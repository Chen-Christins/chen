#include "MultipartParser.hpp"

#include <filesystem>
#include <fstream>

#include "../Log/Log.hpp"
#include "../Util/FSUtil.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

namespace {

/// 已消费前缀超过该阈值就回收一次 buffer_，避免每个 part 都搬移整块内存
constexpr size_t kCompactThreshold = 64 * 1024;

/**
 * @brief 从 Content-Type 中提取 boundary
 *
 * 兼容 `boundary=abc`、`boundary="abc"`、`boundary=abc; charset=utf-8`
 * 以及尾部空白。旧实现直接 substr 到结尾，带参数的 Content-Type 会
 * 得到一个带 "; charset=..." 后缀的 boundary，导致永远匹配不到。
 */
std::string ExtractBoundary(const std::string& content_type, size_t max_length)
{
    static const std::string kKey = "boundary=";
    size_t pos = content_type.find(kKey);
    if (pos == std::string::npos) {
        return "";
    }
    size_t start = pos + kKey.length();
    if (start >= content_type.size()) {
        return "";
    }

    std::string value;
    if (content_type[start] == '"') {
        size_t end = content_type.find('"', start + 1);
        if (end == std::string::npos) {
            return "";
        }
        value = content_type.substr(start + 1, end - start - 1);
    } else {
        size_t end = content_type.find(';', start);
        if (end == std::string::npos) {
            end = content_type.size();
        }
        value = content_type.substr(start, end - start);
        size_t last = value.find_last_not_of(" \t\r\n");
        if (last == std::string::npos) {
            return "";
        }
        value.erase(last + 1);
    }

    if (value.empty() || value.size() > max_length) {
        return "";
    }
    return value;
}

/// 去掉路径部分并校验文件名，拒绝空名、"." 与 ".."
bool SanitizeFilename(const std::string& raw, std::string& out)
{
    size_t slash = raw.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? raw : raw.substr(slash + 1);
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    if (name.find('\0') != std::string::npos) {
        return false;
    }
    // 控制字符替换掉，避免生成不可见/不可运维的文件名
    for (char& ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f) {
            ch = '_';
        }
    }
    out.swap(name);
    return true;
}

/// 同名文件不覆盖：name.txt → name.1.txt
std::string MakeUniquePath(const std::string& dir, const std::string& name)
{
    std::string path = dir + "/" + name;
    if (!FSUtil::Exists(path)) {
        return path;
    }

    std::filesystem::path p(name);
    const std::string stem = p.stem().string();
    const std::string ext = p.extension().string();
    for (int i = 1; i < 1000; ++i) {
        std::string candidate = dir + "/" + stem + "." + std::to_string(i) + ext;
        if (!FSUtil::Exists(candidate)) {
            return candidate;
        }
    }
    return "";
}

std::string ExtractHeaderValue(const std::string& headers, const std::string& key)
{
    size_t pos = headers.find(key);
    if (pos == std::string::npos) {
        return "";
    }
    pos += key.size();
    size_t end = headers.find("\r\n", pos);
    if (end == std::string::npos) {
        return "";
    }
    return headers.substr(pos, end - pos);
}

} // namespace

MultipartParser::MultipartParser(const std::string& content_type)
{
    boundary_ = ExtractBoundary(content_type, limits_.max_boundary_length);
}

// ============ 流式接口 ============

bool MultipartParser::feed(const std::string& data, const std::string& save_dir)
{
    if (failed_) {
        return false;
    }
    if (state_ == COMPLETE) {
        return true;
    }
    if (boundary_.empty()) {
        ERROR(logger) << "MultipartParser: boundary is missing or invalid, cannot parse body";
        failed_ = true;
        return false;
    }

    // 先判断上限再拼接：max_body_size 约束整个请求的累计字节数，
    // 避免多 part 请求绕过单 part 上限把磁盘/内存撑爆
    if (total_received_ > limits_.max_body_size || data.size() > limits_.max_body_size - total_received_) {
        ERROR(logger) << "MultipartParser: body exceeds " << limits_.max_body_size << " bytes";
        failed_ = true;
        return false;
    }
    total_received_ += data.size();

    buffer_ += data;

    while (state_ != COMPLETE) {
        size_t consumed = tryParsePart(save_dir);
        if (failed_) {
            return false;
        }
        if (consumed == 0) {
            break; // 数据不够，等下一块
        }
        offset_ += consumed;
    }

    // 回收已消费的前缀：把 O(n²) 的整块搬移摊薄成每 64KB 一次
    if (offset_ > 0 && (offset_ >= kCompactThreshold || offset_ == buffer_.size())) {
        buffer_.erase(0, offset_);
        offset_ = 0;
    }

    return !failed_;
}

size_t MultipartParser::tryParsePart(const std::string& save_dir)
{
    switch (state_) {
    case PREAMBLE: {
        // 跳过第一个 --boundary 之前的所有数据（理论上没有，但必须设上限）
        std::string delim = "--" + boundary_;
        size_t pos = buffer_.find(delim, offset_);
        if (pos == std::string::npos) {
            if (buffer_.size() - offset_ > limits_.max_header_size) {
                ERROR(logger) << "MultipartParser: preamble exceeds " << limits_.max_header_size << " bytes";
                failed_ = true;
            }
            return 0;
        }
        size_t consumed = pos + delim.length() - offset_;
        size_t cursor = offset_ + consumed;
        if (buffer_.size() >= cursor + 2 && buffer_.compare(cursor, 2, "\r\n") == 0) {
            consumed += 2;
        }
        state_ = HEADERS;
        return consumed;
    }
    case HEADERS: {
        // 查找 header 结束位置 \r\n\r\n
        size_t header_end = buffer_.find("\r\n\r\n", offset_);
        if (header_end == std::string::npos) {
            if (buffer_.size() - offset_ > limits_.max_header_size) {
                ERROR(logger) << "MultipartParser: part header exceeds " << limits_.max_header_size << " bytes";
                failed_ = true;
            }
            return 0;
        }
        if (header_end - offset_ > limits_.max_header_size) {
            ERROR(logger) << "MultipartParser: part header exceeds " << limits_.max_header_size << " bytes";
            failed_ = true;
            return 0;
        }
        if (fields_.size() + files_.size() >= limits_.max_parts) {
            ERROR(logger) << "MultipartParser: part count exceeds " << limits_.max_parts;
            failed_ = true;
            return 0;
        }

        std::string headers = buffer_.substr(offset_, header_end - offset_);
        cur_file_name_ = extractFilename(headers);
        cur_field_name_ = extractFieldName(headers);
        cur_content_type_ = ExtractHeaderValue(headers, "Content-Type: ");
        cur_is_file_ = !cur_file_name_.empty();
        cur_file_size_ = 0;
        cur_field_value_.clear();
        cur_file_path_.clear();

        if (cur_is_file_) {
            std::string safe_name;
            if (!SanitizeFilename(cur_file_name_, safe_name)) {
                ERROR(logger) << "MultipartParser: rejected unsafe filename: " << cur_file_name_;
                failed_ = true;
                return 0;
            }

            if (!FSUtil::Mkdir(save_dir)) {
                ERROR(logger) << "MultipartParser: create directory " << save_dir << " failed";
                failed_ = true;
                return 0;
            }

            cur_file_path_ = MakeUniquePath(save_dir, safe_name);
            if (cur_file_path_.empty()) {
                ERROR(logger) << "MultipartParser: too many duplicate files named " << safe_name;
                failed_ = true;
                return 0;
            }

            if (!FSUtil::OpenForWrite(cur_file_stream_, cur_file_path_, std::ios::binary)) {
                ERROR(logger) << "MultipartParser: failed to open " << cur_file_path_;
                failed_ = true;
                return 0;
            }
        }

        state_ = BODY;
        return header_end + 4 - offset_; // 包括 \r\n\r\n
    }
    case BODY: {
        std::string boundary_str = "--" + boundary_;
        size_t pos = buffer_.find(boundary_str, offset_);

        if (pos == std::string::npos) {
            // 还没找到下一个 boundary
            if (cur_is_file_) {
                if (cur_file_stream_.is_open()) {
                    // 文件数据：只写可以确定属于内容的字节。必须多留 4 个字节，
                    // 否则 boundary（"--" + boundary）以及它前面的 CRLF 可能在
                    // 跨块时被提前写进文件，导致内容多出垃圾字节、甚至漏掉 boundary。
                    size_t keep = boundary_.size() + 4;
                    size_t avail = buffer_.size() - offset_;
                    size_t safe = avail > keep ? avail - keep : 0;
                    if (safe > 0) {
                        if (cur_file_size_ + static_cast<int64_t>(safe) > static_cast<int64_t>(limits_.max_file_size)) {
                            ERROR(logger) << "MultipartParser: file " << cur_file_name_ << " exceeds "
                                          << limits_.max_file_size << " bytes";
                            failed_ = true;
                            return 0;
                        }
                        cur_file_stream_.write(buffer_.data() + offset_, static_cast<std::streamsize>(safe));
                        if (!cur_file_stream_) {
                            ERROR(logger) << "MultipartParser: write failed for " << cur_file_path_;
                            failed_ = true;
                            return 0;
                        }
                        cur_file_size_ += static_cast<int64_t>(safe);
                        offset_ += safe;
                    }
                }
            } else if (buffer_.size() - offset_ > limits_.max_field_size) {
                // 文本字段必须攒到 boundary 才能提取，没有上限就是一个内存放大器
                ERROR(logger) << "MultipartParser: text field " << cur_field_name_ << " exceeds "
                              << limits_.max_field_size << " bytes";
                failed_ = true;
            }
            return 0;
        }

        // boundary 后面的 "--" / "\r\n" 还没收全时不能提交这个 part：
        // 一旦提交再回到 BODY 状态，下一块数据会把同一个字段/文件重复 push 一遍
        size_t cursor = pos + boundary_str.length();
        if (buffer_.size() < cursor + 2) {
            return 0;
        }

        // 找到 boundary，提取 body 内容（去掉 boundary 前的 \r\n）
        size_t content_end = pos;
        if (pos >= offset_ + 2 && buffer_.compare(pos - 2, 2, "\r\n") == 0) {
            content_end = pos - 2;
        }
        const size_t body_len = content_end - offset_;

        if (cur_is_file_) {
            if (cur_file_stream_.is_open()) {
                if (cur_file_size_ + static_cast<int64_t>(body_len) > static_cast<int64_t>(limits_.max_file_size)) {
                    ERROR(logger) << "MultipartParser: file " << cur_file_name_ << " exceeds " << limits_.max_file_size
                                  << " bytes";
                    failed_ = true;
                    return 0;
                }
                if (body_len > 0) {
                    cur_file_stream_.write(buffer_.data() + offset_, static_cast<std::streamsize>(body_len));
                }
                cur_file_stream_.close();
                if (!cur_file_stream_) {
                    ERROR(logger) << "MultipartParser: write failed for " << cur_file_path_;
                    failed_ = true;
                    return 0;
                }
                cur_file_size_ += static_cast<int64_t>(body_len);
            }
            files_.push_back({cur_field_name_, cur_file_name_, cur_content_type_, cur_file_path_, cur_file_size_});
            cur_file_path_.clear();
        } else {
            if (cur_field_value_.size() + body_len > limits_.max_field_size) {
                ERROR(logger) << "MultipartParser: text field " << cur_field_name_ << " exceeds "
                              << limits_.max_field_size << " bytes";
                failed_ = true;
                return 0;
            }
            cur_field_value_.append(buffer_.data() + offset_, body_len);
            fields_.push_back({cur_field_name_, cur_field_value_});
            cur_field_value_.clear();
        }

        size_t consumed = cursor - offset_;

        // 检查是否结束边界 --boundary--
        if (buffer_.compare(cursor, 2, "--") == 0) {
            state_ = COMPLETE;
            complete_ = true;
            return consumed + 2;
        }

        // 跳过 boundary 后的 \r\n
        if (buffer_.compare(cursor, 2, "\r\n") == 0) {
            consumed += 2;
        }

        state_ = HEADERS;
        return consumed;
    }
    case COMPLETE:
        return buffer_.size() - offset_;
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

    if (!FSUtil::Mkdir(save_dir)) {
        ERROR(logger) << "Failed to create directory: " << save_dir;
        return false;
    }

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
            std::string safe_name;
            if (!SanitizeFilename(filename, safe_name)) {
                ERROR(logger) << "Unsafe filename: " << filename;
                return false;
            }
            std::string full_path = MakeUniquePath(save_dir, safe_name);
            if (full_path.empty()) {
                ERROR(logger) << "Too many duplicate files named " << safe_name;
                return false;
            }

            size_t content_end = next_boundary;
            if (next_boundary >= body_start + 2 && data.compare(next_boundary - 2, 2, "\r\n") == 0) {
                content_end = next_boundary - 2;
            }

            std::string file_content = data.substr(body_start, content_end - body_start);
            std::ofstream file;
            if (FSUtil::OpenForWrite(file, full_path, std::ios::binary)) {
                file.write(file_content.c_str(), static_cast<std::streamsize>(file_content.size()));
                file.close();
                if (!file) {
                    ERROR(logger) << "Failed to write file: " << full_path;
                    return false;
                }
                INFO(logger) << "File saved: " << full_path;
                files_saved = true;
            } else {
                ERROR(logger) << "Failed to open file: " << full_path;
                return false;
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
        f.content_type = ExtractHeaderValue(headers, "Content-Type: ");

        size_t content_end = next_boundary;
        if (next_boundary >= body_start + 2 && data.compare(next_boundary - 2, 2, "\r\n") == 0) {
            content_end = next_boundary - 2;
        }
        f.content = data.substr(body_start, content_end - body_start);

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
