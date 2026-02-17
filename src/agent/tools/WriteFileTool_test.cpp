// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <agent/tools/WriteFileTool.hpp>

using namespace endo::agent;

namespace
{

class TempDir
{
  public:
    TempDir(): _path(std::filesystem::temp_directory_path() / "endo_test_write_file")
    {
        std::filesystem::create_directories(_path);
    }

    ~TempDir() { std::filesystem::remove_all(_path); }

    [[nodiscard]] auto path() const -> std::filesystem::path const& { return _path; }

    [[nodiscard]] auto readFile(std::string_view name) const -> std::string
    {
        auto file = std::ifstream(_path / name);
        auto ss = std::ostringstream {};
        ss << file.rdbuf();
        return ss.str();
    }

  private:
    std::filesystem::path _path;
};

} // namespace

TEST_CASE("WriteFileTool.writes_new_file", "[agent][tools]")
{
    auto const dir = TempDir {};
    auto const filePath = (dir.path() / "new.txt").string();

    auto tool = WriteFileTool {};
    auto const args = nlohmann::json { { "path", filePath }, { "content", "hello world" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("11 bytes") != std::string::npos);
    CHECK(dir.readFile("new.txt") == "hello world");
}

TEST_CASE("WriteFileTool.overwrites_existing", "[agent][tools]")
{
    auto const dir = TempDir {};
    auto const filePath = (dir.path() / "existing.txt").string();

    // Write initial content
    {
        auto file = std::ofstream(filePath);
        file << "old content";
    }

    auto tool = WriteFileTool {};
    auto const args = nlohmann::json { { "path", filePath }, { "content", "new content" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(dir.readFile("existing.txt") == "new content");
}

TEST_CASE("WriteFileTool.creates_nested_dirs", "[agent][tools]")
{
    auto const dir = TempDir {};
    auto const filePath = (dir.path() / "a" / "b" / "c" / "deep.txt").string();

    auto tool = WriteFileTool {};
    auto const args = nlohmann::json { { "path", filePath }, { "content", "deep content" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(std::filesystem::exists(filePath));
}

TEST_CASE("WriteFileTool.missing_content_parameter", "[agent][tools]")
{
    auto tool = WriteFileTool {};
    auto const args = nlohmann::json { { "path", "/tmp/test.txt" } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("content") != std::string::npos);
}
