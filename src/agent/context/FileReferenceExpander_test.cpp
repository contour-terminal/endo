// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/context/FileReferenceExpander.hpp>

using namespace endo::agent;

namespace
{

/// Creates a temporary directory with test files for FileReferenceExpander tests.
struct TempTestDir
{
    std::filesystem::path root;

    TempTestDir()
    {
        root = std::filesystem::temp_directory_path() / "endo-test-fileref";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "src");

        std::ofstream(root / "hello.txt") << "line 1\nline 2\nline 3\nline 4\nline 5\n";
        std::ofstream(root / "src" / "main.cpp") << "#include <iostream>\n\nint main() {\n    return 0;\n}\n";
    }

    ~TempTestDir() { std::filesystem::remove_all(root); }
};

} // namespace

// ── parse tests ──────────────────────────────────────────────────────────────

TEST_CASE("FileReferenceExpander.parse.single_ref", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("explain @src/main.cpp");
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].originalText == "@src/main.cpp");
    CHECK(refs[0].resolvedPath == "src/main.cpp");
    CHECK(!refs[0].startLine.has_value());
    CHECK(!refs[0].endLine.has_value());
}

TEST_CASE("FileReferenceExpander.parse.multiple_refs", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("compare @foo.cpp and @bar.cpp");
    REQUIRE(refs.size() == 2);
    CHECK(refs[0].resolvedPath == "foo.cpp");
    CHECK(refs[1].resolvedPath == "bar.cpp");
}

TEST_CASE("FileReferenceExpander.parse.line_range", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("look at @src/main.cpp:10-50");
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].originalText == "@src/main.cpp:10-50");
    CHECK(refs[0].resolvedPath == "src/main.cpp");
    CHECK(refs[0].startLine == 10);
    CHECK(refs[0].endLine == 50);
}

TEST_CASE("FileReferenceExpander.parse.single_line", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("check @file.cpp:42");
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].resolvedPath == "file.cpp");
    CHECK(refs[0].startLine == 42);
    CHECK(refs[0].endLine == 42);
}

TEST_CASE("FileReferenceExpander.parse.at_start", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("@README.md explain this");
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].resolvedPath == "README.md");
}

TEST_CASE("FileReferenceExpander.parse.at_midword_no_match", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("email user@example.com");
    CHECK(refs.empty());
}

TEST_CASE("FileReferenceExpander.parse.bare_at_no_match", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("just an @ sign");
    CHECK(refs.empty());
}

TEST_CASE("FileReferenceExpander.parse.at_end_no_match", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("trailing @");
    CHECK(refs.empty());
}

TEST_CASE("FileReferenceExpander.parse.path_with_dirs", "[agent][context]")
{
    auto const refs = FileReferenceExpander::parse("read @src/agent/context/File.hpp");
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].resolvedPath == "src/agent/context/File.hpp");
}

// ── readFile tests ───────────────────────────────────────────────────────────

TEST_CASE("FileReferenceExpander.readFile.existing_file", "[agent][context]")
{
    auto const dir = TempTestDir {};

    auto ref = FileReference {};
    ref.resolvedPath = dir.root / "hello.txt";

    auto const result = FileReferenceExpander::readFile(ref);
    REQUIRE(result.has_value());
    CHECK(result->find("line 1") != std::string::npos);
    CHECK(result->find("line 5") != std::string::npos);
}

TEST_CASE("FileReferenceExpander.readFile.missing_file", "[agent][context]")
{
    auto ref = FileReference {};
    ref.resolvedPath = "/nonexistent/path/file.txt";

    auto const result = FileReferenceExpander::readFile(ref);
    REQUIRE(!result.has_value());
    CHECK(result.error() == "File not found");
}

TEST_CASE("FileReferenceExpander.readFile.line_range", "[agent][context]")
{
    auto const dir = TempTestDir {};

    auto ref = FileReference {};
    ref.resolvedPath = dir.root / "hello.txt";
    ref.startLine = 2;
    ref.endLine = 4;

    auto const result = FileReferenceExpander::readFile(ref);
    REQUIRE(result.has_value());
    CHECK(result->find("line 1") == std::string::npos); // Before range
    CHECK(result->find("line 2") != std::string::npos);
    CHECK(result->find("line 4") != std::string::npos);
    CHECK(result->find("line 5") == std::string::npos); // After range
}

TEST_CASE("FileReferenceExpander.readFile.truncation", "[agent][context]")
{
    auto const dir = TempTestDir {};

    auto ref = FileReference {};
    ref.resolvedPath = dir.root / "hello.txt";

    auto const result = FileReferenceExpander::readFile(ref, 3);
    REQUIRE(result.has_value());
    CHECK(result->find("[truncated") != std::string::npos);
    CHECK(result->find("2 more lines omitted") != std::string::npos);
}

// ── expand tests ─────────────────────────────────────────────────────────────

TEST_CASE("FileReferenceExpander.expand.no_refs", "[agent][context]")
{
    auto const result = FileReferenceExpander::expand("just a normal message", "/tmp");
    CHECK(result.expandedMessage == "just a normal message");
    CHECK(result.fileCount == 0);
}

TEST_CASE("FileReferenceExpander.expand.valid_file", "[agent][context]")
{
    auto const dir = TempTestDir {};

    auto const result = FileReferenceExpander::expand("explain @hello.txt", dir.root);
    CHECK(result.fileCount == 1);
    CHECK(result.expandedMessage.find("explain @hello.txt") != std::string::npos);
    CHECK(result.expandedMessage.find("<file path=\"hello.txt\">") != std::string::npos);
    CHECK(result.expandedMessage.find("line 1") != std::string::npos);
    CHECK(result.expandedMessage.find("</file>") != std::string::npos);
}

TEST_CASE("FileReferenceExpander.expand.missing_file", "[agent][context]")
{
    auto const dir = TempTestDir {};

    auto const result = FileReferenceExpander::expand("read @nonexistent.txt", dir.root);
    CHECK(result.fileCount == 0);
    CHECK(result.expandedMessage.find("error=\"File not found\"") != std::string::npos);
}

TEST_CASE("FileReferenceExpander.expand.mixed_valid_invalid", "[agent][context]")
{
    auto const dir = TempTestDir {};

    auto const result = FileReferenceExpander::expand("compare @hello.txt and @missing.txt", dir.root);
    CHECK(result.fileCount == 1);
    CHECK(result.expandedMessage.find("<file path=\"hello.txt\">") != std::string::npos);
    CHECK(result.expandedMessage.find("error=\"File not found\"") != std::string::npos);
}

TEST_CASE("FileReferenceExpander.expand.with_line_range", "[agent][context]")
{
    auto const dir = TempTestDir {};

    auto const result = FileReferenceExpander::expand("check @hello.txt:2-3", dir.root);
    CHECK(result.fileCount == 1);
    CHECK(result.expandedMessage.find("lines=\"2-3\"") != std::string::npos);
    CHECK(result.expandedMessage.find("line 2") != std::string::npos);
}

TEST_CASE("FileReferenceExpander.expand.subdirectory", "[agent][context]")
{
    auto const dir = TempTestDir {};

    auto const result = FileReferenceExpander::expand("read @src/main.cpp", dir.root);
    CHECK(result.fileCount == 1);
    CHECK(result.expandedMessage.find("<file path=\"src/main.cpp\">") != std::string::npos);
    CHECK(result.expandedMessage.find("#include") != std::string::npos);
}

// ── stripExpansions tests ────────────────────────────────────────────────────

TEST_CASE("FileReferenceExpander.stripExpansions.no_file_blocks", "[agent][context]")
{
    auto const result = FileReferenceExpander::stripExpansions("explain @src/main.cpp");
    CHECK(result == "explain @src/main.cpp");
}

TEST_CASE("FileReferenceExpander.stripExpansions.single_file_block", "[agent][context]")
{
    auto const input = "explain @hello.txt\n\n<file path=\"hello.txt\">\n     1\tline 1\n</file>";
    auto const result = FileReferenceExpander::stripExpansions(input);
    CHECK(result == "explain @hello.txt");
}

TEST_CASE("FileReferenceExpander.stripExpansions.multiple_file_blocks", "[agent][context]")
{
    auto const input = "compare @a.txt and @b.txt\n\n<file path=\"a.txt\">\ncontent a\n</file>"
                       "\n\n<file path=\"b.txt\">\ncontent b\n</file>";
    auto const result = FileReferenceExpander::stripExpansions(input);
    CHECK(result == "compare @a.txt and @b.txt");
}

TEST_CASE("FileReferenceExpander.stripExpansions.error_file_block", "[agent][context]")
{
    auto const input = "read @missing.txt\n\n<file path=\"missing.txt\" error=\"File not found\"/>";
    auto const result = FileReferenceExpander::stripExpansions(input);
    CHECK(result == "read @missing.txt");
}

TEST_CASE("FileReferenceExpander.stripExpansions.empty_string", "[agent][context]")
{
    auto const result = FileReferenceExpander::stripExpansions("");
    CHECK(result.empty());
}

TEST_CASE("FileReferenceExpander.stripExpansions.roundtrip_with_expand", "[agent][context]")
{
    auto const dir = TempTestDir {};
    auto const original = "explain @hello.txt";
    auto const expanded = FileReferenceExpander::expand(original, dir.root);
    auto const stripped = FileReferenceExpander::stripExpansions(expanded.expandedMessage);
    CHECK(stripped == original);
}
