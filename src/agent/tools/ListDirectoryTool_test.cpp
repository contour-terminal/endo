// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/tools/ListDirectoryTool.hpp>

using namespace endo::agent;

namespace
{

class TempDir
{
  public:
    TempDir(): _path(std::filesystem::temp_directory_path() / "endo_test_listdir")
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

    void makeDir(std::string_view relativePath) const
    {
        std::filesystem::create_directories(_path / relativePath);
    }

  private:
    std::filesystem::path _path;
};

} // namespace

TEST_CASE("ListDirectoryTool.name", "[agent][tools]")
{
    auto const tool = ListDirectoryTool {};
    CHECK(tool.name() == "list_directory");
}

TEST_CASE("ListDirectoryTool.definition_schema", "[agent][tools]")
{
    auto const tool = ListDirectoryTool {};
    auto const def = tool.definition();

    CHECK(def.name == "list_directory");
    CHECK_FALSE(def.description.empty());
    CHECK(def.inputSchema.contains("properties"));
    CHECK(def.inputSchema["properties"].contains("path"));
    CHECK(def.inputSchema["properties"].contains("show_hidden"));
    CHECK(def.inputSchema["properties"].contains("long_format"));

    auto const& required = def.inputSchema["required"];
    REQUIRE(required.is_array());
    CHECK(required.size() == 1);
    CHECK(required[0] == "path");
}

TEST_CASE("ListDirectoryTool.basic_listing", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("alpha.txt");
    dir.writeFile("beta.txt");
    dir.makeDir("gamma");

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", dir.path().string() } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("alpha.txt") != std::string::npos);
    CHECK(result->content.find("beta.txt") != std::string::npos);
    CHECK(result->content.find("gamma/") != std::string::npos);
}

TEST_CASE("ListDirectoryTool.directories_suffixed_with_slash", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.makeDir("subdir");
    dir.writeFile("file.txt");

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", dir.path().string() } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("subdir/") != std::string::npos);
    // file.txt should NOT have trailing slash
    CHECK(result->content.find("file.txt/") == std::string::npos);
    CHECK(result->content.find("file.txt") != std::string::npos);
}

TEST_CASE("ListDirectoryTool.hidden_files_filtered_by_default", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile(".hidden");
    dir.writeFile("visible.txt");

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", dir.path().string() } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("visible.txt") != std::string::npos);
    CHECK(result->content.find(".hidden") == std::string::npos);
}

TEST_CASE("ListDirectoryTool.show_hidden", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile(".hidden");
    dir.writeFile("visible.txt");

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", dir.path().string() }, { "show_hidden", true } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("visible.txt") != std::string::npos);
    CHECK(result->content.find(".hidden") != std::string::npos);
}

TEST_CASE("ListDirectoryTool.long_format", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("data.txt", "hello world");
    dir.makeDir("subdir");

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", dir.path().string() }, { "long_format", true } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    // Long format should contain size and date information
    CHECK(result->content.find("data.txt") != std::string::npos);
    CHECK(result->content.find("subdir/") != std::string::npos);
    // Should have a type indicator
    CHECK(result->content.find('d') != std::string::npos); // directory type
}

TEST_CASE("ListDirectoryTool.alphabetical_sort_dirs_first", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("zebra.txt");
    dir.writeFile("alpha.txt");
    dir.makeDir("middle_dir");
    dir.makeDir("a_dir");

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", dir.path().string() } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    auto const& content = result->content;

    // Directories should come before files
    auto const aDirPos = content.find("a_dir/");
    auto const middleDirPos = content.find("middle_dir/");
    auto const alphaPos = content.find("alpha.txt");
    auto const zebraPos = content.find("zebra.txt");

    CHECK(aDirPos < middleDirPos);  // dirs sorted alphabetically
    CHECK(middleDirPos < alphaPos); // dirs before files
    CHECK(alphaPos < zebraPos);     // files sorted alphabetically
}

TEST_CASE("ListDirectoryTool.nonexistent_path_error", "[agent][tools]")
{
    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", "/nonexistent/path/xyz" } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("does not exist") != std::string::npos);
}

TEST_CASE("ListDirectoryTool.file_not_directory_error", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("afile.txt", "content");

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", (dir.path() / "afile.txt").string() } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not a directory") != std::string::npos);
}

TEST_CASE("ListDirectoryTool.empty_directory", "[agent][tools]")
{
    auto const dir = TempDir {};
    // TempDir is empty by default

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", dir.path().string() } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    // Should return empty or minimal content
    CHECK(result->content.find("truncated") == std::string::npos);
}

TEST_CASE("ListDirectoryTool.missing_path_error", "[agent][tools]")
{
    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json::object();
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Missing required parameter") != std::string::npos);
}

TEST_CASE("ListDirectoryTool.symlink_handling", "[agent][tools]")
{
    auto const dir = TempDir {};
    dir.writeFile("target.txt", "content");

    auto ec = std::error_code {};
    std::filesystem::create_symlink(dir.path() / "target.txt", dir.path() / "link.txt", ec);
    if (ec)
        return; // Skip test if symlinks not supported

    auto tool = ListDirectoryTool {};
    auto const args = nlohmann::json { { "path", dir.path().string() }, { "long_format", true } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("link.txt") != std::string::npos);
    CHECK(result->content.find("->") != std::string::npos);
}
