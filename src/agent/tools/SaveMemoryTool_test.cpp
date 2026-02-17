// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <agent/tools/SaveMemoryTool.hpp>

using namespace endo::agent;

namespace
{

/// RAII helper that sets HOME to a temporary directory and restores it on destruction.
class TempHome
{
  public:
    TempHome(): _path(std::filesystem::temp_directory_path() / "endo_test_save_memory")
    {
        std::filesystem::create_directories(_path);
        auto const* home = std::getenv("HOME");
        if (home)
            _previousHome = home;
        setenv("HOME", _path.c_str(), 1);
    }

    ~TempHome()
    {
        if (!_previousHome.empty())
            setenv("HOME", _previousHome.c_str(), 1);
        std::filesystem::remove_all(_path);
    }

    [[nodiscard]] auto memoryDir() const -> std::filesystem::path
    {
        return _path / ".config" / "endo" / "agent-memory";
    }

    [[nodiscard]] auto readFile(std::filesystem::path const& filePath) const -> std::string
    {
        auto file = std::ifstream(filePath);
        auto ss = std::ostringstream {};
        ss << file.rdbuf();
        return ss.str();
    }

  private:
    std::filesystem::path _path;
    std::string _previousHome;
};

} // namespace

TEST_CASE("SaveMemoryTool.saves_new_memory_file", "[agent][tools]")
{
    auto const home = TempHome {};
    auto tool = SaveMemoryTool {};
    auto const args =
        nlohmann::json { { "filename", "project-notes" }, { "content", "# Notes\nSome notes." } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("bytes") != std::string::npos);
    CHECK(std::filesystem::exists(home.memoryDir() / "project-notes.md"));
    CHECK(home.readFile(home.memoryDir() / "project-notes.md") == "# Notes\nSome notes.");
}

TEST_CASE("SaveMemoryTool.creates_memory_directory", "[agent][tools]")
{
    auto const home = TempHome {};

    // Ensure directory does not exist initially.
    CHECK_FALSE(std::filesystem::exists(home.memoryDir()));

    auto tool = SaveMemoryTool {};
    auto const args = nlohmann::json { { "filename", "test" }, { "content", "hello" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(std::filesystem::is_directory(home.memoryDir()));
}

TEST_CASE("SaveMemoryTool.overwrites_existing_memory", "[agent][tools]")
{
    auto const home = TempHome {};
    std::filesystem::create_directories(home.memoryDir());

    // Write initial content.
    {
        auto file = std::ofstream(home.memoryDir() / "existing.md");
        file << "old content";
    }

    auto tool = SaveMemoryTool {};
    auto const args = nlohmann::json { { "filename", "existing" }, { "content", "new content" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(home.readFile(home.memoryDir() / "existing.md") == "new content");
}

TEST_CASE("SaveMemoryTool.rejects_path_traversal", "[agent][tools]")
{
    auto tool = SaveMemoryTool {};
    auto const args = nlohmann::json { { "filename", "../evil" }, { "content", "hack" } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("path separators") != std::string::npos);
}

TEST_CASE("SaveMemoryTool.missing_filename", "[agent][tools]")
{
    auto tool = SaveMemoryTool {};
    auto const args = nlohmann::json { { "content", "hello" } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("filename") != std::string::npos);
}

TEST_CASE("SaveMemoryTool.missing_content", "[agent][tools]")
{
    auto tool = SaveMemoryTool {};
    auto const args = nlohmann::json { { "filename", "test" } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("content") != std::string::npos);
}
