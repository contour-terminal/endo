// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/tools/GrepTool.hpp>

using namespace endo::agent;

namespace
{

class TempDir
{
  public:
    TempDir(): _path(std::filesystem::temp_directory_path() / "endo_test_grep")
    {
        std::filesystem::remove_all(_path);
        std::filesystem::create_directories(_path);
    }

    ~TempDir() { std::filesystem::remove_all(_path); }

    [[nodiscard]] auto path() const -> std::filesystem::path const& { return _path; }

    void writeFile(std::string_view relativePath, std::string_view content) const
    {
        auto const filePath = _path / relativePath;
        std::filesystem::create_directories(filePath.parent_path());
        auto file = std::ofstream(filePath);
        file << content;
    }

  private:
    std::filesystem::path _path;
};

} // namespace

TEST_CASE("GrepTool.regex_match", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.cpp", "#include <string>\nint main() { return 0; }\n");

    auto tool = GrepTool {};
    auto const args = nlohmann::json {
        { "pattern", "include" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("#include") != std::string::npos);
    CHECK(result->content.find("test.cpp") != std::string::npos);
}

TEST_CASE("GrepTool.no_matches", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "hello world\n");

    auto tool = GrepTool {};
    auto const args = nlohmann::json {
        { "pattern", "nonexistent_string" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("No matches") != std::string::npos);
}

TEST_CASE("GrepTool.glob_filter", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("foo.cpp", "target\n");
    dir.writeFile("bar.hpp", "target\n");

    auto tool = GrepTool {};
    auto const args = nlohmann::json {
        { "pattern", "target" },
        { "path", dir.path().string() },
        { "glob", "*.cpp" },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("foo.cpp") != std::string::npos);
    CHECK(result->content.find("bar.hpp") == std::string::npos);
}

TEST_CASE("GrepTool.context_lines", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "line1\nline2\ntarget_line\nline4\nline5\n");

    auto tool = GrepTool {};
    auto const args = nlohmann::json {
        { "pattern", "target_line" },
        { "path", dir.path().string() },
        { "context", 1 },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("line2") != std::string::npos);
    CHECK(result->content.find("target_line") != std::string::npos);
    CHECK(result->content.find("line4") != std::string::npos);
}

TEST_CASE("GrepTool.invalid_regex", "[agent][tools]")
{
    auto tool = GrepTool {};
    auto const args = nlohmann::json {
        { "pattern", "[invalid" },
        { "path", std::filesystem::temp_directory_path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Invalid regex") != std::string::npos);
}
