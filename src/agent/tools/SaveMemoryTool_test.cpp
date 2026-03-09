// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <agent/tools/SaveMemoryTool.hpp>
#include <testing/EnvHelper.hpp>

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
        _previousHome = saveEnv("HOME");
        _previousXdg = saveEnv("XDG_CONFIG_HOME");
        _previousAppdata = saveEnv("APPDATA");
        endo::testing::setTestEnv("HOME", _path.string().c_str());
        // Clear XDG_CONFIG_HOME and APPDATA so that configHome() falls through to HOME/.config.
        endo::testing::unsetTestEnv("XDG_CONFIG_HOME");
        endo::testing::unsetTestEnv("APPDATA");
    }

    ~TempHome()
    {
        restoreEnv("HOME", _previousHome);
        restoreEnv("XDG_CONFIG_HOME", _previousXdg);
        restoreEnv("APPDATA", _previousAppdata);
        std::filesystem::remove_all(_path);
    }

    [[nodiscard]] auto memoryDir() const -> std::filesystem::path
    {
        return _path / ".config" / "endo" / "agent-memory";
    }

    [[nodiscard]] static auto readFile(std::filesystem::path const& filePath) -> std::string
    {
        auto file = std::ifstream(filePath);
        auto ss = std::ostringstream {};
        ss << file.rdbuf();
        return ss.str();
    }

  private:
    /// @brief Save the current value of an environment variable (empty string if unset).
    static auto saveEnv(char const* name) -> std::string
    {
        auto const* val = std::getenv(name);
        return val ? std::string(val) : std::string {};
    }

    /// @brief Restore an environment variable to its previous value, or unset it if it was unset.
    static void restoreEnv(char const* name, std::string const& prev)
    {
        if (!prev.empty())
            endo::testing::setTestEnv(name, prev.c_str());
        else
            endo::testing::unsetTestEnv(name);
    }

    std::filesystem::path _path;
    std::string _previousHome;
    std::string _previousXdg;
    std::string _previousAppdata;
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
