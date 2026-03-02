// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <agent/tools/EditFileTool.hpp>

using namespace endo::agent;

namespace
{

class TempDir
{
  public:
    TempDir(): _path(std::filesystem::temp_directory_path() / "endo_test_edit_file")
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

TEST_CASE("EditFileTool.single_replacement", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "hello world");

    auto tool = EditFileTool {};
    auto const args = nlohmann::json {
        { "path", (dir.path() / "test.txt").string() },
        { "old_string", "world" },
        { "new_string", "earth" },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(dir.readFile("test.txt") == "hello earth");
    CHECK(result->content.find("1 occurrence") != std::string::npos);
}

TEST_CASE("EditFileTool.ambiguous_match_error", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "foo bar foo baz foo");

    auto tool = EditFileTool {};
    auto const args = nlohmann::json {
        { "path", (dir.path() / "test.txt").string() },
        { "old_string", "foo" },
        { "new_string", "qux" },
    };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("ambiguous") != std::string::npos);
    CHECK(result.error().message.find('3') != std::string::npos);
}

TEST_CASE("EditFileTool.replace_all", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "foo bar foo baz foo");

    auto tool = EditFileTool {};
    auto const args = nlohmann::json {
        { "path", (dir.path() / "test.txt").string() },
        { "old_string", "foo" },
        { "new_string", "qux" },
        { "replace_all", true },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(dir.readFile("test.txt") == "qux bar qux baz qux");
    CHECK(result->content.find("3 occurrences") != std::string::npos);
}

TEST_CASE("EditFileTool.not_found", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "hello world");

    auto tool = EditFileTool {};
    auto const args = nlohmann::json {
        { "path", (dir.path() / "test.txt").string() },
        { "old_string", "missing" },
        { "new_string", "replacement" },
    };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not found") != std::string::npos);
}

TEST_CASE("EditFileTool.file_not_found", "[agent][tools]")
{
    auto tool = EditFileTool {};
    auto const args = nlohmann::json {
        { "path", "/nonexistent/file.txt" },
        { "old_string", "a" },
        { "new_string", "b" },
    };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not found") != std::string::npos);
}
