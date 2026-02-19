// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>

#include <agent/tools/SearchTool.hpp>

using namespace endo::agent;

namespace
{

class TempDir
{
  public:
    TempDir(): _path(std::filesystem::temp_directory_path() / "endo_test_search")
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

    void writeBinaryFile(std::string_view relativePath) const
    {
        auto const filePath = _path / relativePath;
        std::filesystem::create_directories(filePath.parent_path());
        auto file = std::ofstream(filePath, std::ios::binary);
        auto const data = std::array<char, 11> { 'h', 'e', 'l', 'l', 'o', '\0', 'w', 'o', 'r', 'l', 'd' };
        file.write(data.data(), data.size());
    }

  private:
    std::filesystem::path _path;
};

} // namespace

// --- Meta ---

TEST_CASE("SearchTool.name", "[agent][tools][search]")
{
    auto tool = SearchTool {};
    CHECK(tool.name() == "search");
}

TEST_CASE("SearchTool.definition_schema", "[agent][tools][search]")
{
    auto tool = SearchTool {};
    auto const def = tool.definition();

    CHECK(def.name == "search");
    CHECK(!def.description.empty());

    auto const& required = def.inputSchema["required"];
    REQUIRE(required.is_array());
    CHECK(required.size() == 1);
    CHECK(required[0] == "pattern");

    auto const& props = def.inputSchema["properties"];
    CHECK(props.contains("pattern"));
    CHECK(props.contains("mode"));
    CHECK(props.contains("path"));
    CHECK(props.contains("glob"));
    CHECK(props.contains("type"));
    CHECK(props.contains("case_insensitive"));
    CHECK(props.contains("context_before"));
    CHECK(props.contains("context_after"));
    CHECK(props.contains("context"));
    CHECK(props.contains("multiline"));
    CHECK(props.contains("limit"));
    CHECK(props.contains("offset"));
}

// --- Validation ---

TEST_CASE("SearchTool.missing_pattern", "[agent][tools][search]")
{
    auto tool = SearchTool {};
    auto const result = tool.execute(nlohmann::json::object());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("pattern") != std::string::npos);
}

TEST_CASE("SearchTool.invalid_regex", "[agent][tools][search]")
{
    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "[invalid" },
        { "mode", "content" },
        { "path", std::filesystem::temp_directory_path().string() },
    };
    auto const result = tool.execute(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Invalid regex") != std::string::npos);
}

TEST_CASE("SearchTool.invalid_mode", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "test" },
        { "mode", "bogus" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Invalid mode") != std::string::npos);
}

TEST_CASE("SearchTool.unknown_type", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "test" },
        { "type", "brainfuck" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Unknown file type") != std::string::npos);
}

TEST_CASE("SearchTool.nonexistent_path", "[agent][tools][search]")
{
    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "test" },
        { "path", "/nonexistent/directory/xyz" },
    };
    auto const result = tool.execute(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not found") != std::string::npos);
}

// --- Files mode ---

TEST_CASE("SearchTool.files_simple_glob", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("foo.cpp", "// cpp");
    dir.writeFile("bar.cpp", "// cpp");
    dir.writeFile("baz.hpp", "// hpp");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "*.cpp" },
        { "mode", "files" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("foo.cpp") != std::string::npos);
    CHECK(result->content.find("bar.cpp") != std::string::npos);
    CHECK(result->content.find("baz.hpp") == std::string::npos);
}

TEST_CASE("SearchTool.files_recursive_glob", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("src/a.hpp", "// hpp");
    dir.writeFile("src/sub/b.hpp", "// hpp");
    dir.writeFile("src/c.cpp", "// cpp");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "**/*.hpp" },
        { "mode", "files" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("a.hpp") != std::string::npos);
    CHECK(result->content.find("b.hpp") != std::string::npos);
    CHECK(result->content.find("c.cpp") == std::string::npos);
}

TEST_CASE("SearchTool.files_type_filter", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("a.cpp", "// cpp");
    dir.writeFile("b.py", "# py");
    dir.writeFile("c.hpp", "// hpp");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "*" },
        { "mode", "files" },
        { "type", "cpp" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("a.cpp") != std::string::npos);
    CHECK(result->content.find("c.hpp") != std::string::npos);
    CHECK(result->content.find("b.py") == std::string::npos);
}

TEST_CASE("SearchTool.files_no_matches", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "hello");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "*.xyz" },
        { "mode", "files" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("No files matched") != std::string::npos);
}

TEST_CASE("SearchTool.files_limit", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    for (auto i = 0; i < 5; ++i)
        dir.writeFile(std::format("file{}.txt", i), "content");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "*.txt" },
        { "mode", "files" },
        { "path", dir.path().string() },
        { "limit", 2 },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    // Should only list 2 files and show truncation notice
    CHECK(result->content.find("showing") != std::string::npos);
}

TEST_CASE("SearchTool.files_offset", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    for (auto i = 0; i < 5; ++i)
        dir.writeFile(std::format("file{}.txt", i), "content");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "*.txt" }, { "mode", "files" }, { "path", dir.path().string() },
        { "limit", 2 },         { "offset", 2 },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("showing") != std::string::npos);
}

// --- Content mode ---

TEST_CASE("SearchTool.content_basic_match", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.cpp", "#include <string>\nint main() { return 0; }\n");

    auto tool = SearchTool {};
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

TEST_CASE("SearchTool.content_no_matches", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "hello world\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "nonexistent_string" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("No matches") != std::string::npos);
}

TEST_CASE("SearchTool.content_glob_filter", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("foo.cpp", "target\n");
    dir.writeFile("bar.hpp", "target\n");

    auto tool = SearchTool {};
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

TEST_CASE("SearchTool.content_type_filter", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("foo.cpp", "target\n");
    dir.writeFile("bar.py", "target\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "target" },
        { "path", dir.path().string() },
        { "type", "cpp" },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("foo.cpp") != std::string::npos);
    CHECK(result->content.find("bar.py") == std::string::npos);
}

TEST_CASE("SearchTool.content_case_insensitive", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "Hello World\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "hello" },
        { "path", dir.path().string() },
        { "case_insensitive", true },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("Hello World") != std::string::npos);
}

TEST_CASE("SearchTool.content_symmetric_context", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "line1\nline2\ntarget_line\nline4\nline5\n");

    auto tool = SearchTool {};
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

TEST_CASE("SearchTool.content_asymmetric_context", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "line1\nline2\ntarget_line\nline4\nline5\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "target_line" },
        { "path", dir.path().string() },
        { "context_before", 2 },
        { "context_after", 0 },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("line1") != std::string::npos);
    CHECK(result->content.find("line2") != std::string::npos);
    CHECK(result->content.find("target_line") != std::string::npos);
    // line4 should NOT be present (0 after-context)
    CHECK(result->content.find("line4") == std::string::npos);
}

TEST_CASE("SearchTool.content_multiline", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "start\nmiddle\nend\n");

    auto tool = SearchTool {};
    // std::regex doesn't support dotall; use [\s\S] for cross-line matching
    auto const args = nlohmann::json {
        { "pattern", R"(start[\s\S]*middle)" },
        { "path", dir.path().string() },
        { "multiline", true },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("test.txt") != std::string::npos);
}

TEST_CASE("SearchTool.content_skips_binary", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("text.txt", "findme\n");
    dir.writeBinaryFile("binary.bin");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "findme|hello" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("text.txt") != std::string::npos);
    CHECK(result->content.find("binary.bin") == std::string::npos);
}

TEST_CASE("SearchTool.content_limit", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    auto content = std::string {};
    for (auto i = 0; i < 10; ++i)
        content += std::format("match_line_{}\n", i);
    dir.writeFile("test.txt", content);

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "match_line" },
        { "path", dir.path().string() },
        { "limit", 3 },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("truncated") != std::string::npos);
}

TEST_CASE("SearchTool.content_offset", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    auto content = std::string {};
    for (auto i = 0; i < 10; ++i)
        content += std::format("match_line_{}\n", i);
    dir.writeFile("test.txt", content);

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "match_line" },
        { "path", dir.path().string() },
        { "offset", 5 },
        { "limit", 3 },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    // Should skip first 5 matches, show next 3
    CHECK(result->content.find("match_line_5") != std::string::npos);
    CHECK(result->content.find("truncated") != std::string::npos);
}

TEST_CASE("SearchTool.content_single_file_path", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("target.txt", "findme here\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "findme" },
        { "path", (dir.path() / "target.txt").string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("findme") != std::string::npos);
}

// --- Files-with-matches mode ---

TEST_CASE("SearchTool.files_with_matches_basic", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("a.txt", "target line\n");
    dir.writeFile("b.txt", "no match here\n");
    dir.writeFile("c.txt", "another target\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "target" },
        { "mode", "files_with_matches" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("a.txt") != std::string::npos);
    CHECK(result->content.find("c.txt") != std::string::npos);
    CHECK(result->content.find("b.txt") == std::string::npos);
}

TEST_CASE("SearchTool.files_with_matches_no_matches", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "hello\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "nonexistent" },
        { "mode", "files_with_matches" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("No matches") != std::string::npos);
}

TEST_CASE("SearchTool.files_with_matches_type_filter", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("a.cpp", "target\n");
    dir.writeFile("b.py", "target\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "target" },
        { "mode", "files_with_matches" },
        { "type", "cpp" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("a.cpp") != std::string::npos);
    CHECK(result->content.find("b.py") == std::string::npos);
}

TEST_CASE("SearchTool.files_with_matches_pagination", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    for (auto i = 0; i < 5; ++i)
        dir.writeFile(std::format("file{}.txt", i), "target\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "target" },
        { "mode", "files_with_matches" },
        { "path", dir.path().string() },
        { "limit", 2 },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("showing") != std::string::npos);
}

// --- Count mode ---

TEST_CASE("SearchTool.count_basic", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("a.txt", "match\nmatch\nmatch\n");
    dir.writeFile("b.txt", "match\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "match" },
        { "mode", "count" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("a.txt:3") != std::string::npos);
    CHECK(result->content.find("b.txt:1") != std::string::npos);
}

TEST_CASE("SearchTool.count_sorted_by_count", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("few.txt", "x\n");
    dir.writeFile("many.txt", "x\nx\nx\nx\nx\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "x" },
        { "mode", "count" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    // many.txt should appear before few.txt (sorted by count descending)
    auto const manyPos = result->content.find("many.txt:5");
    auto const fewPos = result->content.find("few.txt:1");
    REQUIRE(manyPos != std::string::npos);
    REQUIRE(fewPos != std::string::npos);
    CHECK(manyPos < fewPos);
}

TEST_CASE("SearchTool.count_no_matches", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("test.txt", "hello\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "nonexistent" },
        { "mode", "count" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("No matches") != std::string::npos);
}

TEST_CASE("SearchTool.count_type_filter", "[agent][tools][search]")
{
    auto const dir = TempDir {};
    dir.writeFile("a.cpp", "target\n");
    dir.writeFile("b.py", "target\n");

    auto tool = SearchTool {};
    auto const args = nlohmann::json {
        { "pattern", "target" },
        { "mode", "count" },
        { "type", "cpp" },
        { "path", dir.path().string() },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("a.cpp") != std::string::npos);
    CHECK(result->content.find("b.py") == std::string::npos);
}
