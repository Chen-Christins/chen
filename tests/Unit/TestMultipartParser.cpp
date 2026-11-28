/**
 * @file TestMultipartParser.cpp
 * @brief MultipartParser 健壮性单元测试
 * @author Christins
 * @date 2026-11-28
 * @copyright GPL-3.0
 * @details 覆盖解析器的资源上限与文件名安全化：
 *          1. Content-Type 带 `; charset=...` 时必须能正确取出 boundary；
 *          2. header / 文本字段 / part 数量 / 总 body 超限必须失败，而不是无限吃内存；
 *          3. 文件名中的路径必须被剥离，`..` 这类名字直接拒绝；
 *          4. 同名文件不互相覆盖，流式分块与旧的一次性接口都要保内容正确。
 */
#include "../TestHarness.hpp"

#include "src/Parser/MultipartParser.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using chen::MultipartParser;

namespace {

std::string MakeTempDir(const std::string& name)
{
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("chen_multipart_test_" + name);
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

void CleanTempDir(const std::string& dir)
{
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

std::string ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string CreateBody(const std::string& boundary, const std::string& headers, const std::string& content, bool last)
{
    std::string body = "--" + boundary + "\r\n" + headers + "\r\n" + content + "\r\n--" + boundary;
    body += last ? "--\r\n" : "\r\n";
    return body;
}

std::string TextHeader(const std::string& name)
{
    return "Content-Disposition: form-data; name=\"" + name + "\"\r\n";
}

std::string FileHeader(const std::string& name, const std::string& filename)
{
    return "Content-Disposition: form-data; name=\"" + name + "\"; filename=\"" + filename
        + "\"\r\n"
          "Content-Type: application/octet-stream\r\n";
}

} // namespace

CHEN_TEST(multipartparser, boundary_with_parameters)
{
    const std::string dir = MakeTempDir("boundary");
    MultipartParser parser("multipart/form-data; boundary=XBOUNDARY; charset=utf-8");
    CHEN_CHECK_EQ(parser.getLimits().max_body_size, 64ull * 1024 * 1024);

    const std::string body = CreateBody("XBOUNDARY", TextHeader("a"), "hello", true);
    CHEN_CHECK(parser.feed(body, dir));
    CHEN_CHECK(parser.isComplete());
    CHEN_CHECK(!parser.isError());
    CHEN_CHECK_EQ(parser.getFields().size(), 1u);
    CHEN_CHECK_EQ(parser.getFields()[0].name, std::string("a"));
    CHEN_CHECK_EQ(parser.getFields()[0].value, std::string("hello"));
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, missing_boundary_fails_instead_of_silent_noop)
{
    const std::string dir = MakeTempDir("noboundary");
    MultipartParser parser("text/plain");
    // 旧实现会返回 true 且什么也不解析，调用方误以为上传成功
    CHEN_CHECK(!parser.feed("anything", dir));
    CHEN_CHECK(parser.isError());
    CHEN_CHECK(!parser.feed("more", dir));
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, header_size_limit_is_enforced)
{
    const std::string dir = MakeTempDir("header");
    MultipartParser parser("multipart/form-data; boundary=B");
    MultipartParser::Limits limits;
    limits.max_header_size = 128;
    parser.setLimits(limits);

    const std::string body = "--B\r\n" + std::string(1024, 'x');
    CHEN_CHECK(!parser.feed(body, dir));
    CHEN_CHECK(parser.isError());
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, field_size_limit_is_enforced)
{
    const std::string dir = MakeTempDir("field");
    MultipartParser parser("multipart/form-data; boundary=B");
    MultipartParser::Limits limits;
    limits.max_field_size = 64;
    limits.max_body_size = 4096;
    parser.setLimits(limits);

    // 文本字段在遇到下一个 boundary 前全部留在内存里，必须被上限截断
    const std::string body = "--B\r\n" + TextHeader("big") + "\r\n" + std::string(256, 'a');
    CHEN_CHECK(!parser.feed(body, dir));
    CHEN_CHECK(parser.isError());
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, body_size_limit_is_enforced)
{
    const std::string dir = MakeTempDir("body");
    MultipartParser parser("multipart/form-data; boundary=B");
    MultipartParser::Limits limits;
    limits.max_body_size = 64;
    parser.setLimits(limits);

    // 超限判断需要在拼接前完成，否则 1MB 数据已经进了 buffer_
    CHEN_CHECK(!parser.feed(std::string(1024, 'a'), dir));
    CHEN_CHECK(parser.isError());
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, part_count_limit_is_enforced)
{
    const std::string dir = MakeTempDir("parts");
    MultipartParser parser("multipart/form-data; boundary=B");
    MultipartParser::Limits limits;
    limits.max_parts = 1;
    parser.setLimits(limits);

    std::string body = CreateBody("B", TextHeader("a"), "1", false);
    body += CreateBody("B", TextHeader("b"), "2", true);
    CHEN_CHECK(!parser.feed(body, dir));
    CHEN_CHECK(parser.isError());
    CHEN_CHECK_EQ(parser.getFields().size(), 1u);
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, filename_path_is_stripped)
{
    const std::string dir = MakeTempDir("filename");
    MultipartParser parser("multipart/form-data; boundary=B");
    const std::string body = CreateBody("B", FileHeader("f", "..\\..\\evil.txt"), "DATA", true);

    CHEN_CHECK(parser.feed(body, dir));
    CHEN_CHECK(parser.isComplete());
    CHEN_CHECK_EQ(parser.getSavedFiles().size(), 1u);

    const MultipartParser::SavedFileInfo& info = parser.getSavedFiles()[0];
    CHEN_CHECK_EQ(info.filename, std::string("evil.txt"));
    // 落盘路径必须仍在 save_dir 内
    CHEN_CHECK_EQ(std::filesystem::path(info.path).parent_path().string(), dir);
    CHEN_CHECK_EQ(ReadFile(info.path), std::string("DATA"));
    CHEN_CHECK_EQ(info.size, static_cast<int64_t>(4));
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, dotdot_filename_is_rejected)
{
    const std::string dir = MakeTempDir("dotdot");
    MultipartParser parser("multipart/form-data; boundary=B");
    const std::string body = CreateBody("B", FileHeader("f", ".."), "DATA", true);

    CHEN_CHECK(!parser.feed(body, dir));
    CHEN_CHECK(parser.isError());
    CHEN_CHECK(parser.getSavedFiles().empty());
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, duplicate_filenames_do_not_overwrite)
{
    const std::string dir = MakeTempDir("duplicate");
    MultipartParser parser("multipart/form-data; boundary=B");

    std::string body = CreateBody("B", FileHeader("f1", "a.txt"), "first", false);
    body += CreateBody("B", FileHeader("f2", "a.txt"), "second", true);
    CHEN_CHECK(parser.feed(body, dir));
    CHEN_CHECK_EQ(parser.getSavedFiles().size(), 2u);

    const MultipartParser::SavedFileInfo& first = parser.getSavedFiles()[0];
    const MultipartParser::SavedFileInfo& second = parser.getSavedFiles()[1];
    CHEN_CHECK_NE(first.path, second.path);
    CHEN_CHECK_EQ(ReadFile(first.path), std::string("first"));
    CHEN_CHECK_EQ(ReadFile(second.path), std::string("second"));
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, streaming_chunks_keep_file_content)
{
    const std::string dir = MakeTempDir("streaming");
    MultipartParser parser("multipart/form-data; boundary=STREAM");

    std::string payload;
    for (int i = 0; i < 20; ++i) {
        payload += "0123456789";
    }
    const std::string body = CreateBody("STREAM", FileHeader("upload", "data.bin"), payload, true);

    // 在 boundary 中间断开，验证解析器能跨块续上
    size_t step = 17;
    bool ok = true;
    for (size_t off = 0; off < body.size() && ok; off += step) {
        ok = parser.feed(body.substr(off, step), dir);
    }
    CHEN_CHECK(ok);
    CHEN_CHECK(parser.isComplete());
    CHEN_CHECK_EQ(parser.getSavedFiles().size(), 1u);

    const MultipartParser::SavedFileInfo& info = parser.getSavedFiles()[0];
    CHEN_CHECK_EQ(info.size, static_cast<int64_t>(payload.size()));
    CHEN_CHECK_EQ(ReadFile(info.path), payload);
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, every_split_point_keeps_content)
{
    const std::string dir = MakeTempDir("split");
    const std::string payload = "0123456789abcdefghij";
    const std::string body = CreateBody("SPLIT", FileHeader("f", "s.bin"), payload, true);

    // 逐字节切分：boundary / CRLF 落在块边界时不能把垃圾写进文件
    for (size_t split = 1; split < body.size(); ++split) {
        MultipartParser parser("multipart/form-data; boundary=SPLIT");
        bool ok = parser.feed(body.substr(0, split), dir);
        ok = parser.feed(body.substr(split), dir) && ok;

        CHEN_CHECK(ok);
        CHEN_CHECK(parser.isComplete());
        CHEN_CHECK_EQ(parser.getSavedFiles().size(), 1u);
        CHEN_CHECK_EQ(ReadFile(parser.getSavedFiles()[0].path), payload);
        CleanTempDir(dir);
    }
}

CHEN_TEST(multipartparser, legacy_parse_saves_file)
{
    const std::string dir = MakeTempDir("legacy");
    MultipartParser parser("multipart/form-data; boundary=B");
    const std::string body = "--B\r\n" + FileHeader("f", "legacy.bin") + "\r\nDATA\r\n--B--\r\n";

    CHEN_CHECK(parser.parse(body, dir));
    CHEN_CHECK_EQ(ReadFile(dir + "/legacy.bin"), std::string("DATA"));
    CleanTempDir(dir);
}

CHEN_TEST(multipartparser, parse_to_memory_keeps_content_without_trailing_crlf)
{
    MultipartParser parser("multipart/form-data; boundary=B");
    // 最后一个 part 的 body 直接贴 boundary，没有 CRLF
    const std::string body = "--B\r\n" + FileHeader("f", "x.txt") + "\r\nDATA--B--\r\n";

    std::vector<MultipartParser::FilePart> parts = parser.parseToMemory(body);
    CHEN_CHECK_EQ(parts.size(), 1u);
    CHEN_CHECK_EQ(parts[0].filename, std::string("x.txt"));
    CHEN_CHECK_EQ(parts[0].content, std::string("DATA"));
}

CHEN_TEST_MAIN()
