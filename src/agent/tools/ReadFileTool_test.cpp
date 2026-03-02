// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/tools/ReadFileTool.hpp>

using namespace endo::agent;

namespace
{

class TempDir
{
  public:
    TempDir(): _path(std::filesystem::temp_directory_path() / "endo_test_read_file")
    {
        std::filesystem::create_directories(_path);
    }

    ~TempDir() { std::filesystem::remove_all(_path); }

    [[nodiscard]] auto path() const -> std::filesystem::path const& { return _path; }

    void writeFile(std::string_view name, std::string_view content) const
    {
        auto file = std::ofstream(_path / name);
        file << content;
    }

  private:
    std::filesystem::path _path;
};

} // namespace

TEST_CASE("ReadFileTool.reads_file", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("hello.txt", "line 1\nline 2\nline 3\n");

    auto tool = ReadFileTool {};
    auto const args = nlohmann::json { { "path", (dir.path() / "hello.txt").string() } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("line 1") != std::string::npos);
    CHECK(result->content.find("line 2") != std::string::npos);
    CHECK(result->content.find("line 3") != std::string::npos);
    // Should have line numbers
    CHECK(result->content.find("1\t") != std::string::npos);
}

TEST_CASE("ReadFileTool.with_offset_and_limit", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("lines.txt", "a\nb\nc\nd\ne\n");

    auto tool = ReadFileTool {};
    auto const args = nlohmann::json {
        { "path", (dir.path() / "lines.txt").string() },
        { "offset", 2 },
        { "limit", 2 },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find('b') != std::string::npos);
    CHECK(result->content.find('c') != std::string::npos);
    CHECK(result->content.find('d') == std::string::npos); // Not included due to limit
}

TEST_CASE("ReadFileTool.file_not_found", "[agent][tools]")
{
    auto tool = ReadFileTool {};
    auto const args = nlohmann::json { { "path", "/nonexistent/file.txt" } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not found") != std::string::npos);
}

TEST_CASE("ReadFileTool.directory_error", "[agent][tools]")
{
    auto tool = ReadFileTool {};
    auto const args = nlohmann::json { { "path", std::filesystem::temp_directory_path().string() } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("directory") != std::string::npos);
}

TEST_CASE("ReadFileTool.empty_file", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("empty.txt", "");

    auto tool = ReadFileTool {};
    auto const args = nlohmann::json { { "path", (dir.path() / "empty.txt").string() } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("empty file") != std::string::npos);
}

TEST_CASE("ReadFileTool.missing_path_parameter", "[agent][tools]")
{
    auto tool = ReadFileTool {};
    auto const result = tool.execute(nlohmann::json::object());

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("path") != std::string::npos);
}
