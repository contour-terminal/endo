// SPDX-License-Identifier: Apache-2.0
#include <tui/GenericSyntaxHighlighter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace tui;
using Cat = HighlightCategory;

namespace
{
/// @brief Checks that all characters in range [start, start+count) have the expected category.
bool hasCategory(HighlightMap const& map, std::size_t start, std::size_t count, Cat expected)
{
    for (auto i = start; i < start + count && i < map.size(); ++i)
        if (map[i] != expected)
            return false;
    return true;
}

/// @brief Checks that a specific position has the expected category.
bool categoryAt(HighlightMap const& map, std::size_t pos, Cat expected)
{
    return pos < map.size() && map[pos] == expected;
}
} // namespace

// =============================================================================
// Language detection
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.detectLanguageFromExtension", "[tui][highlight]")
{
    CHECK(detectLanguageFromExtension(".cpp") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".hpp") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".c") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".h") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".py") == LanguageId::Python);
    CHECK(detectLanguageFromExtension(".sh") == LanguageId::Bash);
    CHECK(detectLanguageFromExtension(".json") == LanguageId::Json);
    CHECK(detectLanguageFromExtension(".yml") == LanguageId::Yaml);
    CHECK(detectLanguageFromExtension(".yaml") == LanguageId::Yaml);
    CHECK(detectLanguageFromExtension(".md") == LanguageId::Markdown);
    CHECK(detectLanguageFromExtension(".cmake") == LanguageId::CMake);
    CHECK(detectLanguageFromExtension(".diff") == LanguageId::GitDiff);
    CHECK(detectLanguageFromExtension(".txt") == LanguageId::None);
    // Approximate mappings — C-family syntax
    CHECK(detectLanguageFromExtension(".js") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".jsx") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".ts") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".tsx") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".rs") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".go") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".java") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".kt") == LanguageId::Cpp);
    CHECK(detectLanguageFromExtension(".cs") == LanguageId::Cpp);
    // Ruby/Lua map to Python
    CHECK(detectLanguageFromExtension(".rb") == LanguageId::Python);
    CHECK(detectLanguageFromExtension(".lua") == LanguageId::Python);
    // TOML maps to YAML
    CHECK(detectLanguageFromExtension(".toml") == LanguageId::Yaml);
    // PowerShell
    CHECK(detectLanguageFromExtension(".ps1") == LanguageId::PowerShell);
    CHECK(detectLanguageFromExtension(".psm1") == LanguageId::PowerShell);
    CHECK(detectLanguageFromExtension(".psd1") == LanguageId::PowerShell);
    // Windows CMD / batch
    CHECK(detectLanguageFromExtension(".cmd") == LanguageId::Cmd);
    CHECK(detectLanguageFromExtension(".bat") == LanguageId::Cmd);
    // XML and XML-based dialects
    CHECK(detectLanguageFromExtension(".xml") == LanguageId::Xml);
    CHECK(detectLanguageFromExtension(".props") == LanguageId::Xml);
    CHECK(detectLanguageFromExtension(".csproj") == LanguageId::Xml);
    CHECK(detectLanguageFromExtension(".xaml") == LanguageId::Xml);
    CHECK(detectLanguageFromExtension(".svg") == LanguageId::Xml);
    // INI
    CHECK(detectLanguageFromExtension(".ini") == LanguageId::Ini);
}

TEST_CASE("GenericSyntaxHighlighter.detectLanguageFromFenceTag", "[tui][highlight]")
{
    CHECK(detectLanguageFromFenceTag("cpp") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("c++") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("python") == LanguageId::Python);
    CHECK(detectLanguageFromFenceTag("py") == LanguageId::Python);
    CHECK(detectLanguageFromFenceTag("bash") == LanguageId::Bash);
    CHECK(detectLanguageFromFenceTag("sh") == LanguageId::Bash);
    CHECK(detectLanguageFromFenceTag("shell") == LanguageId::Bash);
    CHECK(detectLanguageFromFenceTag("json") == LanguageId::Json);
    CHECK(detectLanguageFromFenceTag("yaml") == LanguageId::Yaml);
    CHECK(detectLanguageFromFenceTag("yml") == LanguageId::Yaml);
    CHECK(detectLanguageFromFenceTag("markdown") == LanguageId::Markdown);
    CHECK(detectLanguageFromFenceTag("cmake") == LanguageId::CMake);
    CHECK(detectLanguageFromFenceTag("diff") == LanguageId::GitDiff);
    CHECK(detectLanguageFromFenceTag("") == LanguageId::None);
    // Approximate mappings — C-family syntax
    CHECK(detectLanguageFromFenceTag("javascript") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("js") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("jsx") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("typescript") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("ts") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("tsx") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("rust") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("rs") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("go") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("golang") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("java") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("kotlin") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("kt") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("csharp") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("cs") == LanguageId::Cpp);
    CHECK(detectLanguageFromFenceTag("c#") == LanguageId::Cpp);
    // Ruby/Lua map to Python
    CHECK(detectLanguageFromFenceTag("ruby") == LanguageId::Python);
    CHECK(detectLanguageFromFenceTag("rb") == LanguageId::Python);
    CHECK(detectLanguageFromFenceTag("lua") == LanguageId::Python);
    // Dockerfile maps to Bash
    CHECK(detectLanguageFromFenceTag("dockerfile") == LanguageId::Bash);
    CHECK(detectLanguageFromFenceTag("docker") == LanguageId::Bash);
    // TOML maps to YAML
    CHECK(detectLanguageFromFenceTag("toml") == LanguageId::Yaml);
    // PowerShell
    CHECK(detectLanguageFromFenceTag("powershell") == LanguageId::PowerShell);
    CHECK(detectLanguageFromFenceTag("pwsh") == LanguageId::PowerShell);
    CHECK(detectLanguageFromFenceTag("ps1") == LanguageId::PowerShell);
    // Windows CMD / batch
    CHECK(detectLanguageFromFenceTag("bat") == LanguageId::Cmd);
    CHECK(detectLanguageFromFenceTag("batch") == LanguageId::Cmd);
    CHECK(detectLanguageFromFenceTag("cmd") == LanguageId::Cmd);
    // XML
    CHECK(detectLanguageFromFenceTag("xml") == LanguageId::Xml);
    CHECK(detectLanguageFromFenceTag("xaml") == LanguageId::Xml);
    // INI
    CHECK(detectLanguageFromFenceTag("ini") == LanguageId::Ini);
    CHECK(detectLanguageFromFenceTag("editorconfig") == LanguageId::Ini);
}

TEST_CASE("GenericSyntaxHighlighter.detectLanguageFromPath", "[tui][highlight]")
{
    CHECK(detectLanguageFromPath("/src/main.cpp") == LanguageId::Cpp);
    CHECK(detectLanguageFromPath("/build/CMakeLists.txt") == LanguageId::CMake);
    CHECK(detectLanguageFromPath("script.py") == LanguageId::Python);
    CHECK(detectLanguageFromPath("run.sh") == LanguageId::Bash);
    CHECK(detectLanguageFromPath("data.json") == LanguageId::Json);
    CHECK(detectLanguageFromPath("config.yml") == LanguageId::Yaml);
    CHECK(detectLanguageFromPath("README.md") == LanguageId::Markdown);
    CHECK(detectLanguageFromPath("noext") == LanguageId::None);

    // Well-known config file names are detected by name (not extension).
    CHECK(detectLanguageFromPath(".clang-format") == LanguageId::Yaml);
    CHECK(detectLanguageFromPath(".clang-tidy") == LanguageId::Yaml);
    CHECK(detectLanguageFromPath(".endo-format") == LanguageId::Yaml);
    CHECK(detectLanguageFromPath(".editorconfig") == LanguageId::Ini);
    // Filename detection works with leading directories too.
    CHECK(detectLanguageFromPath("/home/user/project/.clang-format") == LanguageId::Yaml);
    CHECK(detectLanguageFromPath(R"(C:\proj\.editorconfig)") == LanguageId::Ini);
    // .endo-format must not be mistaken for the .endo extension.
    CHECK(detectLanguageFromPath("main.endo") == LanguageId::Endo);
    // New extensions resolve through the path entry point as well.
    CHECK(detectLanguageFromPath("Build.props") == LanguageId::Xml);
    CHECK(detectLanguageFromPath("deploy.ps1") == LanguageId::PowerShell);
    CHECK(detectLanguageFromPath("setup.bat") == LanguageId::Cmd);
    CHECK(detectLanguageFromPath("settings.ini") == LanguageId::Ini);
}

// =============================================================================
// C++ highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.cpp_keywords", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("if (x) return 0;", LanguageId::Cpp);
    // "if" at positions 0..1 should be Keyword
    CHECK(hasCategory(map, 0, 2, Cat::Keyword));
    // "return" at positions 7..12 should be Keyword
    CHECK(hasCategory(map, 7, 6, Cat::Keyword));
    // "0" at position 14 should be Number
    CHECK(categoryAt(map, 14, Cat::Number));
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.cpp_line_comment", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("int x; // comment", LanguageId::Cpp);
    // "int" at 0..2 is keyword
    CHECK(hasCategory(map, 0, 3, Cat::Keyword));
    // everything from // onwards is comment
    CHECK(hasCategory(map, 7, 10, Cat::Comment));
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.cpp_block_comment_single_line", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("x = /* comment */ y;", LanguageId::Cpp);
    CHECK(hasCategory(map, 4, 13, Cat::Comment));
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.cpp_block_comment_multi_line", "[tui][highlight]")
{
    auto const [map1, state1] = highlightLine("/* start", LanguageId::Cpp);
    CHECK(hasCategory(map1, 0, 8, Cat::Comment));
    CHECK(state1 == HighlightState::BlockComment);

    auto const [map2, state2] = highlightLine("middle", LanguageId::Cpp, state1);
    CHECK(hasCategory(map2, 0, 6, Cat::Comment));
    CHECK(state2 == HighlightState::BlockComment);

    auto const [map3, state3] = highlightLine("end */ code", LanguageId::Cpp, state2);
    CHECK(hasCategory(map3, 0, 5, Cat::Comment));
    CHECK(state3 == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.cpp_string_literal", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(R"(auto s = "hello";)", LanguageId::Cpp);
    // "hello" including quotes at positions 9..15
    CHECK(hasCategory(map, 9, 7, Cat::String));
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.cpp_string_with_escape", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(R"(auto s = "he\"llo";)", LanguageId::Cpp);
    // The entire string including escaped quote
    CHECK(hasCategory(map, 9, 9, Cat::String));
}

TEST_CASE("GenericSyntaxHighlighter.cpp_numbers", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("int x = 42;", LanguageId::Cpp);
    CHECK(hasCategory(map, 8, 2, Cat::Number));
}

TEST_CASE("GenericSyntaxHighlighter.cpp_hex_number", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("auto v = 0xFF;", LanguageId::Cpp);
    CHECK(hasCategory(map, 9, 4, Cat::Number));
}

TEST_CASE("GenericSyntaxHighlighter.cpp_types", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("vector<int> v;", LanguageId::Cpp);
    CHECK(hasCategory(map, 0, 6, Cat::Type));
    CHECK(hasCategory(map, 7, 3, Cat::Keyword)); // int is a keyword
}

TEST_CASE("GenericSyntaxHighlighter.cpp_preprocessor", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("#include <vector>", LanguageId::Cpp);
    CHECK(hasCategory(map, 0, 17, Cat::Preprocessor));
}

TEST_CASE("GenericSyntaxHighlighter.cpp_operators", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("a + b == c", LanguageId::Cpp);
    CHECK(categoryAt(map, 2, Cat::Operator)); // +
    CHECK(categoryAt(map, 6, Cat::Operator)); // first =
    CHECK(categoryAt(map, 7, Cat::Operator)); // second =
}

TEST_CASE("GenericSyntaxHighlighter.cpp_punctuation", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("f(x, y);", LanguageId::Cpp);
    CHECK(categoryAt(map, 1, Cat::Punctuation)); // (
    CHECK(categoryAt(map, 3, Cat::Punctuation)); // ,
    CHECK(categoryAt(map, 6, Cat::Punctuation)); // )
    CHECK(categoryAt(map, 7, Cat::Punctuation)); // ;
}

// =============================================================================
// Python highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.python_keywords", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("def foo(x):", LanguageId::Python);
    CHECK(hasCategory(map, 0, 3, Cat::Keyword)); // def
}

TEST_CASE("GenericSyntaxHighlighter.python_builtins", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("print(len(x))", LanguageId::Python);
    CHECK(hasCategory(map, 0, 5, Cat::Function)); // print
    CHECK(hasCategory(map, 6, 3, Cat::Function)); // len
}

TEST_CASE("GenericSyntaxHighlighter.python_comment", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("x = 1  # note", LanguageId::Python);
    CHECK(hasCategory(map, 7, 7, Cat::Comment));
}

TEST_CASE("GenericSyntaxHighlighter.python_triple_quote", "[tui][highlight]")
{
    auto const [map1, state1] = highlightLine(R"("""start)", LanguageId::Python);
    CHECK(hasCategory(map1, 0, 8, Cat::String));
    CHECK(state1 == HighlightState::TripleQuoteString);

    auto const [map2, state2] = highlightLine("middle", LanguageId::Python, state1);
    CHECK(hasCategory(map2, 0, 6, Cat::String));
    CHECK(state2 == HighlightState::TripleQuoteString);

    auto const [map3, state3] = highlightLine(R"(end""")", LanguageId::Python, state2);
    CHECK(hasCategory(map3, 0, 6, Cat::String));
    CHECK(state3 == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.python_decorator", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("@staticmethod", LanguageId::Python);
    CHECK(hasCategory(map, 0, 13, Cat::Function));
}

// =============================================================================
// Bash highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.bash_keywords", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("if [ -f file ]; then", LanguageId::Bash);
    CHECK(hasCategory(map, 0, 2, Cat::Keyword));  // if
    CHECK(hasCategory(map, 16, 4, Cat::Keyword)); // then
}

TEST_CASE("GenericSyntaxHighlighter.bash_variables", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("echo $HOME ${PATH}", LanguageId::Bash);
    CHECK(hasCategory(map, 5, 5, Cat::Variable));  // $HOME
    CHECK(hasCategory(map, 11, 7, Cat::Variable)); // ${PATH}
}

TEST_CASE("GenericSyntaxHighlighter.bash_builtins", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("export PATH=/usr/bin", LanguageId::Bash);
    CHECK(hasCategory(map, 0, 6, Cat::Function)); // export
}

TEST_CASE("GenericSyntaxHighlighter.bash_comment", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("# this is a comment", LanguageId::Bash);
    CHECK(hasCategory(map, 0, 19, Cat::Comment));
}

// =============================================================================
// CMake highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.cmake_commands", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("add_library(mylib STATIC)", LanguageId::CMake);
    CHECK(hasCategory(map, 0, 11, Cat::Keyword)); // add_library
}

TEST_CASE("GenericSyntaxHighlighter.cmake_variables", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("set(VAR ${CMAKE_SOURCE_DIR})", LanguageId::CMake);
    CHECK(hasCategory(map, 0, 3, Cat::Keyword)); // set
    // ${CMAKE_SOURCE_DIR}
    CHECK(hasCategory(map, 8, 19, Cat::Variable));
}

// =============================================================================
// JSON highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.json_key_value", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(R"(  "name": "value",)", LanguageId::Json);
    // "name" should be Type (it's a key followed by :)
    CHECK(hasCategory(map, 2, 6, Cat::Type));
    // "value" should be String
    CHECK(hasCategory(map, 10, 7, Cat::String));
}

TEST_CASE("GenericSyntaxHighlighter.json_keywords", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("  true, false, null", LanguageId::Json);
    CHECK(hasCategory(map, 2, 4, Cat::Keyword));  // true
    CHECK(hasCategory(map, 8, 5, Cat::Keyword));  // false
    CHECK(hasCategory(map, 15, 4, Cat::Keyword)); // null
}

TEST_CASE("GenericSyntaxHighlighter.json_numbers", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("  42, -3.14", LanguageId::Json);
    CHECK(hasCategory(map, 2, 2, Cat::Number)); // 42
    CHECK(hasCategory(map, 6, 5, Cat::Number)); // -3.14
}

// =============================================================================
// YAML highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.yaml_key_value", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("name: value", LanguageId::Yaml);
    CHECK(hasCategory(map, 0, 4, Cat::Type));    // name
    CHECK(categoryAt(map, 4, Cat::Punctuation)); // :
}

TEST_CASE("GenericSyntaxHighlighter.yaml_comment", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("# comment", LanguageId::Yaml);
    CHECK(hasCategory(map, 0, 9, Cat::Comment));
}

TEST_CASE("GenericSyntaxHighlighter.yaml_keywords", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("enabled: true", LanguageId::Yaml);
    CHECK(hasCategory(map, 0, 7, Cat::Type));    // enabled
    CHECK(hasCategory(map, 9, 4, Cat::Keyword)); // true
}

// =============================================================================
// Git diff highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.gitdiff_lines", "[tui][highlight]")
{
    {
        auto const [map, state] = highlightLine("+added line", LanguageId::GitDiff);
        CHECK(hasCategory(map, 0, 11, Cat::Constructor));
    }
    {
        auto const [map, state] = highlightLine("-removed line", LanguageId::GitDiff);
        CHECK(hasCategory(map, 0, 13, Cat::Variable));
    }
    {
        auto const [map, state] = highlightLine("@@ -1,3 +1,4 @@", LanguageId::GitDiff);
        CHECK(hasCategory(map, 0, 16, Cat::Keyword));
    }
    {
        auto const [map, state] = highlightLine("diff --git a/f b/f", LanguageId::GitDiff);
        CHECK(hasCategory(map, 0, 18, Cat::Comment));
    }
}

// =============================================================================
// Markdown highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.markdown_heading", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("# Heading", LanguageId::Markdown);
    CHECK(hasCategory(map, 0, 9, Cat::Keyword));
}

TEST_CASE("GenericSyntaxHighlighter.markdown_code_span", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("use `code` here", LanguageId::Markdown);
    CHECK(hasCategory(map, 4, 6, Cat::String)); // `code`
}

// =============================================================================
// Endo detection
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.endo_detection_extension", "[tui][highlight]")
{
    CHECK(detectLanguageFromExtension(".endo") == LanguageId::Endo);
}

TEST_CASE("GenericSyntaxHighlighter.endo_detection_fence", "[tui][highlight]")
{
    CHECK(detectLanguageFromFenceTag("endo") == LanguageId::Endo);
}

// =============================================================================
// Assembly highlighting — detection
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.asm_detection_extension", "[tui][highlight]")
{
    CHECK(detectLanguageFromExtension(".asm") == LanguageId::Assembly);
    CHECK(detectLanguageFromExtension(".s") == LanguageId::Assembly);
    CHECK(detectLanguageFromExtension(".S") == LanguageId::Assembly);
    CHECK(detectLanguageFromExtension(".nasm") == LanguageId::Assembly);
}

TEST_CASE("GenericSyntaxHighlighter.asm_detection_fence", "[tui][highlight]")
{
    CHECK(detectLanguageFromFenceTag("asm") == LanguageId::Assembly);
    CHECK(detectLanguageFromFenceTag("assembly") == LanguageId::Assembly);
    CHECK(detectLanguageFromFenceTag("nasm") == LanguageId::Assembly);
    CHECK(detectLanguageFromFenceTag("x86") == LanguageId::Assembly);
    CHECK(detectLanguageFromFenceTag("x86asm") == LanguageId::Assembly);
    CHECK(detectLanguageFromFenceTag("intel") == LanguageId::Assembly);
    CHECK(detectLanguageFromFenceTag("att") == LanguageId::Assembly);
    CHECK(detectLanguageFromFenceTag("gas") == LanguageId::Assembly);
}

// =============================================================================
// Assembly highlighting — Intel syntax
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.asm_intel_instruction", "[tui][highlight]")
{
    // "mov rax, 0x10"
    auto const [map, state] = highlightLine("mov rax, 0x10", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 3, Cat::Keyword));  // mov
    CHECK(hasCategory(map, 4, 3, Cat::Variable)); // rax
    CHECK(hasCategory(map, 9, 4, Cat::Number));   // 0x10
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.asm_intel_comment", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("; this is a comment", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 19, Cat::Comment));
}

TEST_CASE("GenericSyntaxHighlighter.asm_label", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("main:", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 4, Cat::Function)); // main
    CHECK(categoryAt(map, 4, Cat::Punctuation));  // :
}

TEST_CASE("GenericSyntaxHighlighter.asm_nasm_directive", "[tui][highlight]")
{
    // "section .data"
    auto const [map, state] = highlightLine("section .data", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 7, Cat::Preprocessor)); // section
    CHECK(hasCategory(map, 8, 5, Cat::Preprocessor)); // .data
}

TEST_CASE("GenericSyntaxHighlighter.asm_registers", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("push rbp", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 4, Cat::Keyword));  // push
    CHECK(hasCategory(map, 5, 3, Cat::Variable)); // rbp
}

TEST_CASE("GenericSyntaxHighlighter.asm_case_insensitive", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("MOV EAX, EBX", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 3, Cat::Keyword));  // MOV
    CHECK(hasCategory(map, 4, 3, Cat::Variable)); // EAX
    CHECK(hasCategory(map, 9, 3, Cat::Variable)); // EBX
}

TEST_CASE("GenericSyntaxHighlighter.asm_binary_number", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("mov rax, 0b1010", LanguageId::Assembly);
    CHECK(hasCategory(map, 9, 6, Cat::Number)); // 0b1010
}

// =============================================================================
// Assembly highlighting — AT&T syntax
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.asm_att_instruction", "[tui][highlight]")
{
    // "movl $0x10, %eax"
    auto const [map, state] = highlightLine("movl $0x10, %eax", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 4, Cat::Keyword));   // movl
    CHECK(hasCategory(map, 5, 5, Cat::Number));    // $0x10
    CHECK(hasCategory(map, 12, 4, Cat::Variable)); // %eax
}

TEST_CASE("GenericSyntaxHighlighter.asm_att_comment", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("# this is a comment", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 19, Cat::Comment));
}

TEST_CASE("GenericSyntaxHighlighter.asm_gas_directive", "[tui][highlight]")
{
    // ".section .text"
    auto const [map, state] = highlightLine(".section .text", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 8, Cat::Preprocessor)); // .section
    CHECK(hasCategory(map, 9, 5, Cat::Preprocessor)); // .text
}

TEST_CASE("GenericSyntaxHighlighter.asm_string_literal", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(R"(.ascii "hello")", LanguageId::Assembly);
    CHECK(hasCategory(map, 0, 6, Cat::Preprocessor)); // .ascii
    CHECK(hasCategory(map, 7, 7, Cat::String));       // "hello"
}

TEST_CASE("GenericSyntaxHighlighter.asm_block_comment", "[tui][highlight]")
{
    auto const [map1, state1] = highlightLine("/* start", LanguageId::Assembly);
    CHECK(hasCategory(map1, 0, 8, Cat::Comment));
    CHECK(state1 == HighlightState::BlockComment);

    auto const [map2, state2] = highlightLine("end */", LanguageId::Assembly, state1);
    CHECK(hasCategory(map2, 0, 5, Cat::Comment));
    CHECK(state2 == HighlightState::Normal);
}

// =============================================================================
// PowerShell highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.powershell_variable_and_cmdlet", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("$result = Get-ChildItem", LanguageId::PowerShell);
    CHECK(hasCategory(map, 0, 7, Cat::Variable));   // $result
    CHECK(hasCategory(map, 10, 13, Cat::Function)); // Get-ChildItem (Verb-Noun)
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.powershell_keyword_operator_comment", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("if ($x -eq 1) { } # done", LanguageId::PowerShell);
    CHECK(hasCategory(map, 0, 2, Cat::Keyword));  // if
    CHECK(hasCategory(map, 4, 2, Cat::Variable)); // $x
    CHECK(hasCategory(map, 7, 3, Cat::Operator)); // -eq
    CHECK(categoryAt(map, 11, Cat::Number));      // 1
    CHECK(hasCategory(map, 18, 6, Cat::Comment)); // # done
}

TEST_CASE("GenericSyntaxHighlighter.powershell_string_interpolation", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(R"("Hello $name")", LanguageId::PowerShell);
    CHECK(hasCategory(map, 0, 7, Cat::String));   // "Hello
    CHECK(hasCategory(map, 7, 5, Cat::Variable)); // $name
    CHECK(categoryAt(map, 12, Cat::String));      // closing quote
}

TEST_CASE("GenericSyntaxHighlighter.powershell_block_comment_multi_line", "[tui][highlight]")
{
    auto const [map1, state1] = highlightLine("<# start", LanguageId::PowerShell);
    CHECK(hasCategory(map1, 0, 8, Cat::Comment));
    CHECK(state1 == HighlightState::PsBlockComment);

    auto const [map2, state2] = highlightLine("middle", LanguageId::PowerShell, state1);
    CHECK(hasCategory(map2, 0, 6, Cat::Comment));
    CHECK(state2 == HighlightState::PsBlockComment);

    auto const [map3, state3] = highlightLine("end #> code", LanguageId::PowerShell, state2);
    CHECK(hasCategory(map3, 0, 6, Cat::Comment));
    CHECK(state3 == HighlightState::Normal);
}

// =============================================================================
// Windows CMD / batch highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.batch_keyword_and_variable", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(R"(set PATH=%PATH%;C:\bin)", LanguageId::Cmd);
    CHECK(hasCategory(map, 0, 3, Cat::Keyword));  // set
    CHECK(hasCategory(map, 9, 6, Cat::Variable)); // %PATH%
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.batch_rem_comment", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("REM this is a comment", LanguageId::Cmd);
    CHECK(hasCategory(map, 0, 21, Cat::Comment));
}

TEST_CASE("GenericSyntaxHighlighter.batch_label", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(":start", LanguageId::Cmd);
    CHECK(hasCategory(map, 0, 6, Cat::Function));
}

TEST_CASE("GenericSyntaxHighlighter.batch_loop_variable", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("echo %%i", LanguageId::Cmd);
    CHECK(hasCategory(map, 0, 4, Cat::Keyword));  // echo
    CHECK(hasCategory(map, 5, 3, Cat::Variable)); // %%i
}

// =============================================================================
// XML highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.xml_tag_attribute_value", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(R"(<tag attr="val">)", LanguageId::Xml);
    CHECK(categoryAt(map, 0, Cat::Punctuation));  // <
    CHECK(hasCategory(map, 1, 3, Cat::Keyword));  // tag
    CHECK(hasCategory(map, 5, 4, Cat::Variable)); // attr
    CHECK(categoryAt(map, 9, Cat::Operator));     // =
    CHECK(hasCategory(map, 10, 5, Cat::String));  // "val"
    CHECK(categoryAt(map, 15, Cat::Punctuation)); // >
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.xml_comment_multi_line", "[tui][highlight]")
{
    auto const [map1, state1] = highlightLine("<!-- start", LanguageId::Xml);
    CHECK(hasCategory(map1, 0, 10, Cat::Comment));
    CHECK(state1 == HighlightState::XmlComment);

    auto const [map2, state2] = highlightLine("still comment", LanguageId::Xml, state1);
    CHECK(hasCategory(map2, 0, 13, Cat::Comment));
    CHECK(state2 == HighlightState::XmlComment);

    auto const [map3, state3] = highlightLine("end -->", LanguageId::Xml, state2);
    CHECK(hasCategory(map3, 0, 7, Cat::Comment));
    CHECK(state3 == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.xml_processing_instruction", "[tui][highlight]")
{
    auto const [map, state] = highlightLine(R"(<?xml version="1.0"?>)", LanguageId::Xml);
    CHECK(hasCategory(map, 0, 21, Cat::Preprocessor));
}

TEST_CASE("GenericSyntaxHighlighter.xml_entity", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("<p>&amp;</p>", LanguageId::Xml);
    CHECK(categoryAt(map, 1, Cat::Keyword));         // p
    CHECK(hasCategory(map, 3, 5, Cat::Constructor)); // &amp;
    CHECK(categoryAt(map, 10, Cat::Keyword));        // p (closing tag)
}

// =============================================================================
// INI / .editorconfig highlighting
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.ini_section", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("[section]", LanguageId::Ini);
    CHECK(hasCategory(map, 0, 9, Cat::Keyword));
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.ini_key_value", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("key = value", LanguageId::Ini);
    CHECK(hasCategory(map, 0, 3, Cat::Variable)); // key
    CHECK(categoryAt(map, 4, Cat::Operator));     // =
    CHECK(hasCategory(map, 6, 5, Cat::String));   // value
}

TEST_CASE("GenericSyntaxHighlighter.ini_comment", "[tui][highlight]")
{
    {
        auto const [map, state] = highlightLine("; comment", LanguageId::Ini);
        CHECK(hasCategory(map, 0, 9, Cat::Comment));
    }
    {
        auto const [map, state] = highlightLine("# comment", LanguageId::Ini);
        CHECK(hasCategory(map, 0, 9, Cat::Comment));
    }
}

TEST_CASE("GenericSyntaxHighlighter.ini_editorconfig_glob", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("indent_size = 4", LanguageId::Ini);
    CHECK(hasCategory(map, 0, 11, Cat::Variable)); // indent_size
    CHECK(categoryAt(map, 12, Cat::Operator));     // =
    CHECK(categoryAt(map, 14, Cat::String));       // 4
}

// =============================================================================
// Empty / None language
// =============================================================================

TEST_CASE("GenericSyntaxHighlighter.empty_line", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("", LanguageId::Cpp);
    CHECK(map.empty());
    CHECK(state == HighlightState::Normal);
}

TEST_CASE("GenericSyntaxHighlighter.none_language", "[tui][highlight]")
{
    auto const [map, state] = highlightLine("some code here", LanguageId::None);
    CHECK(map.empty());
}
