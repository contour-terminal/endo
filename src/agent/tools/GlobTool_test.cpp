// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/tools/GlobTool.hpp>

using namespace endo::agent;

namespace
{

class TempDir
{
  public:
    TempDir(): _path(std::filesystem::temp_directory_path() / "endo_test_glob")
    {
        std::filesystem::remove_all(_path);
        std::filesystem::create_directories(_path);
    }

    ~TempDir() { std::filesystem::remove_all(_path); }

    [[nodiscard]] auto path() const -> std::filesystem::path const& { return _path; }

    void writeFile(std::string_view relativePath, std::string_view content = "") const
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

TEST_CASE("GlobTool.simple_wildcard", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("foo.cpp", "// cpp");
    dir.writeFile("bar.cpp", "// cpp");
    dir.writeFile("baz.hpp", "// hpp");

    auto tool = GlobTool {};
    auto const args = nlohmann::json {
        { "pattern", "*.cpp" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("foo.cpp") != std::string::npos);
    CHECK(result->content.find("bar.cpp") != std::string::npos);
    CHECK(result->content.find("baz.hpp") == std::string::npos);
}

TEST_CASE("GlobTool.recursive_wildcard", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("src/a.hpp", "// hpp");
    dir.writeFile("src/sub/b.hpp", "// hpp");
    dir.writeFile("src/c.cpp", "// cpp");

    auto tool = GlobTool {};
    auto const args = nlohmann::json {
        { "pattern", "**/*.hpp" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("a.hpp") != std::string::npos);
    CHECK(result->content.find("b.hpp") != std::string::npos);
    CHECK(result->content.find("c.cpp") == std::string::npos);
}

TEST_CASE("GlobTool.no_matches", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt");

    auto tool = GlobTool {};
    auto const args = nlohmann::json {
        { "pattern", "*.xyz" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("No files matched") != std::string::npos);
}

TEST_CASE("GlobTool.base_path_not_found", "[agent][tools]")
{
    auto tool = GlobTool {};
    auto const args = nlohmann::json {
        { "pattern", "*.cpp" },
        { "path", "/nonexistent/directory" },
    };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not found") != std::string::npos);
}
