// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/FindExpression.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace endo::find;
namespace fs = std::filesystem;

namespace
{

/// Creates a mock FindEntry for testing evaluators.
FindEntry mockEntry(std::string const& path,
                    fs::file_type type = fs::file_type::regular,
                    uintmax_t size = 0,
                    int depth = 0)
{
    auto const p = fs::path(path);
    return FindEntry {
        .path = p,
        .filename = p.filename().string(),
        .type = type,
        .size = size,
        .mtime = fs::file_time_type::clock::now(),
        .depth = depth,
    };
}

} // namespace

// --- Parser tests ---

TEST_CASE("find.parse.empty_args", "[find]")
{
    std::vector<std::string> args;
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    CHECK(options.searchPaths.size() == 1);
    CHECK(options.searchPaths[0] == ".");
    CHECK(!expr); // No expression
}

TEST_CASE("find.parse.paths_only", "[find]")
{
    std::vector<std::string> args = { "src", "lib" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    CHECK(options.searchPaths.size() == 2);
    CHECK(options.searchPaths[0] == "src");
    CHECK(options.searchPaths[1] == "lib");
    CHECK(!expr);
}

TEST_CASE("find.parse.name_predicate", "[find]")
{
    std::vector<std::string> args = { "-name", "*.cpp" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(options.searchPaths.size() == 1);
    CHECK(options.searchPaths[0] == ".");

    // Test evaluation
    CHECK(expr->evaluate(mockEntry("src/main.cpp")));
    CHECK_FALSE(expr->evaluate(mockEntry("src/main.hpp")));
}

TEST_CASE("find.parse.iname_predicate", "[find]")
{
    std::vector<std::string> args = { "-iname", "*.CPP" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("src/main.cpp")));
    CHECK(expr->evaluate(mockEntry("src/MAIN.CPP")));
    CHECK_FALSE(expr->evaluate(mockEntry("src/main.hpp")));
}

TEST_CASE("find.parse.path_predicate", "[find]")
{
    std::vector<std::string> args = { "-path", "*/src/*.cpp" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("foo/src/main.cpp")));
    CHECK_FALSE(expr->evaluate(mockEntry("foo/lib/main.cpp")));
}

TEST_CASE("find.parse.type_predicate", "[find]")
{
    SECTION("file")
    {
        std::vector<std::string> args = { "-type", "f" };
        auto result = parseFindArgs(args);
        REQUIRE(result.has_value());
        auto const& [options, expr] = result.value();
        REQUIRE(expr);
        CHECK(expr->evaluate(mockEntry("file.txt", fs::file_type::regular)));
        CHECK_FALSE(expr->evaluate(mockEntry("dir", fs::file_type::directory)));
    }

    SECTION("directory")
    {
        std::vector<std::string> args = { "-type", "d" };
        auto result = parseFindArgs(args);
        REQUIRE(result.has_value());
        auto const& [options, expr] = result.value();
        REQUIRE(expr);
        CHECK(expr->evaluate(mockEntry("dir", fs::file_type::directory)));
        CHECK_FALSE(expr->evaluate(mockEntry("file.txt", fs::file_type::regular)));
    }

    SECTION("symlink")
    {
        std::vector<std::string> args = { "-type", "l" };
        auto result = parseFindArgs(args);
        REQUIRE(result.has_value());
        auto const& [options, expr] = result.value();
        REQUIRE(expr);
        CHECK(expr->evaluate(mockEntry("link", fs::file_type::symlink)));
        CHECK_FALSE(expr->evaluate(mockEntry("file.txt", fs::file_type::regular)));
    }

    SECTION("invalid type")
    {
        std::vector<std::string> args = { "-type", "x" };
        auto result = parseFindArgs(args);
        CHECK_FALSE(result.has_value());
    }
}

TEST_CASE("find.parse.size_predicate", "[find]")
{
    SECTION("greater than 1k")
    {
        std::vector<std::string> args = { "-size", "+1k" };
        auto result = parseFindArgs(args);
        REQUIRE(result.has_value());
        auto const& [options, expr] = result.value();
        REQUIRE(expr);
        CHECK(expr->evaluate(mockEntry("big.txt", fs::file_type::regular, 2048)));
        CHECK_FALSE(expr->evaluate(mockEntry("small.txt", fs::file_type::regular, 512)));
    }

    SECTION("less than 100 bytes")
    {
        std::vector<std::string> args = { "-size", "-100c" };
        auto result = parseFindArgs(args);
        REQUIRE(result.has_value());
        auto const& [options, expr] = result.value();
        REQUIRE(expr);
        CHECK(expr->evaluate(mockEntry("tiny.txt", fs::file_type::regular, 50)));
        CHECK_FALSE(expr->evaluate(mockEntry("big.txt", fs::file_type::regular, 200)));
    }

    SECTION("exact 5M")
    {
        std::vector<std::string> args = { "-size", "5M" };
        auto result = parseFindArgs(args);
        REQUIRE(result.has_value());
        auto const& [options, expr] = result.value();
        REQUIRE(expr);
        CHECK(expr->evaluate(mockEntry("exact.bin", fs::file_type::regular, 5ULL * 1024 * 1024)));
        CHECK_FALSE(expr->evaluate(mockEntry("off.bin", fs::file_type::regular, (5ULL * 1024 * 1024) + 1)));
    }
}

TEST_CASE("find.parse.or_expression", "[find]")
{
    std::vector<std::string> args = { "-name", "*.cpp", "-o", "-name", "*.hpp" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("main.cpp")));
    CHECK(expr->evaluate(mockEntry("main.hpp")));
    CHECK_FALSE(expr->evaluate(mockEntry("main.py")));
}

TEST_CASE("find.parse.grouped_expression", "[find]")
{
    std::vector<std::string> args = { "(", "-name", "*.cpp", "-o", "-name", "*.hpp", ")" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("main.cpp")));
    CHECK(expr->evaluate(mockEntry("main.hpp")));
    CHECK_FALSE(expr->evaluate(mockEntry("main.py")));
}

TEST_CASE("find.parse.not_expression", "[find]")
{
    std::vector<std::string> args = { "-not", "-name", "*.o" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("main.cpp")));
    CHECK_FALSE(expr->evaluate(mockEntry("main.o")));
}

TEST_CASE("find.parse.bang_negation", "[find]")
{
    std::vector<std::string> args = { "!", "-name", "*.o" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("main.cpp")));
    CHECK_FALSE(expr->evaluate(mockEntry("main.o")));
}

TEST_CASE("find.parse.and_implicit", "[find]")
{
    std::vector<std::string> args = { "-name", "*.cpp", "-type", "f" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("main.cpp", fs::file_type::regular)));
    CHECK_FALSE(expr->evaluate(mockEntry("main.cpp", fs::file_type::directory)));
    CHECK_FALSE(expr->evaluate(mockEntry("main.hpp", fs::file_type::regular)));
}

TEST_CASE("find.parse.and_explicit", "[find]")
{
    std::vector<std::string> args = { "-name", "*.cpp", "-a", "-type", "f" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("main.cpp", fs::file_type::regular)));
    CHECK_FALSE(expr->evaluate(mockEntry("main.hpp", fs::file_type::regular)));
}

TEST_CASE("find.parse.maxdepth_mindepth", "[find]")
{
    std::vector<std::string> args = { "-maxdepth", "2", "-mindepth", "1", "-name", "*.cpp" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    CHECK(options.maxDepth == 2);
    CHECK(options.minDepth == 1);
    REQUIRE(expr);
}

TEST_CASE("find.parse.print0", "[find]")
{
    std::vector<std::string> args = { "-name", "*.cpp", "-print0" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    CHECK(options.print0);
    REQUIRE(expr);
}

TEST_CASE("find.parse.empty_predicate", "[find]")
{
    std::vector<std::string> args = { "-empty" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    // Empty regular file
    CHECK(expr->evaluate(mockEntry("empty.txt", fs::file_type::regular, 0)));
    // Non-empty regular file
    CHECK_FALSE(expr->evaluate(mockEntry("notempty.txt", fs::file_type::regular, 100)));
}

TEST_CASE("find.parse.complex_expression", "[find]")
{
    // ( -name '*.cpp' -o -name '*.hpp' ) -type f
    std::vector<std::string> args = { "(", "-name", "*.cpp", "-o", "-name", "*.hpp", ")", "-type", "f" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("main.cpp", fs::file_type::regular)));
    CHECK(expr->evaluate(mockEntry("main.hpp", fs::file_type::regular)));
    CHECK_FALSE(expr->evaluate(mockEntry("main.py", fs::file_type::regular)));
    CHECK_FALSE(expr->evaluate(mockEntry("main.cpp", fs::file_type::directory)));
}

TEST_CASE("find.parse.path_then_expression", "[find]")
{
    std::vector<std::string> args = { "src", "-name", "*.cpp" };
    auto result = parseFindArgs(args);
    REQUIRE(result.has_value());
    auto const& [options, expr] = result.value();
    CHECK(options.searchPaths.size() == 1);
    CHECK(options.searchPaths[0] == "src");
    REQUIRE(expr);
    CHECK(expr->evaluate(mockEntry("main.cpp")));
}

TEST_CASE("find.parse.error_missing_argument", "[find]")
{
    SECTION("-name without pattern")
    {
        std::vector<std::string> args = { "-name" };
        auto result = parseFindArgs(args);
        CHECK_FALSE(result.has_value());
    }

    SECTION("-type without type")
    {
        std::vector<std::string> args = { "-type" };
        auto result = parseFindArgs(args);
        CHECK_FALSE(result.has_value());
    }

    SECTION("-size without value")
    {
        std::vector<std::string> args = { "-size" };
        auto result = parseFindArgs(args);
        CHECK_FALSE(result.has_value());
    }

    SECTION("-maxdepth without value")
    {
        std::vector<std::string> args = { "-maxdepth" };
        auto result = parseFindArgs(args);
        CHECK_FALSE(result.has_value());
    }
}

TEST_CASE("find.parse.error_unknown_predicate", "[find]")
{
    std::vector<std::string> args = { "-foobar" };
    auto result = parseFindArgs(args);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("find.parse.error_unmatched_paren", "[find]")
{
    std::vector<std::string> args = { "(", "-name", "*.cpp" };
    auto result = parseFindArgs(args);
    CHECK_FALSE(result.has_value());
}

// --- Evaluator tests ---

TEST_CASE("find.eval.name_match", "[find]")
{
    NameExpr expr("*.txt", false);
    CHECK(expr.evaluate(mockEntry("readme.txt")));
    CHECK_FALSE(expr.evaluate(mockEntry("readme.md")));
}

TEST_CASE("find.eval.name_case_insensitive", "[find]")
{
    NameExpr expr("*.TXT", true);
    CHECK(expr.evaluate(mockEntry("readme.txt")));
    CHECK(expr.evaluate(mockEntry("README.TXT")));
    CHECK_FALSE(expr.evaluate(mockEntry("readme.md")));
}

TEST_CASE("find.eval.type_match", "[find]")
{
    TypeExpr expr(fs::file_type::directory);
    CHECK(expr.evaluate(mockEntry("dir", fs::file_type::directory)));
    CHECK_FALSE(expr.evaluate(mockEntry("file.txt", fs::file_type::regular)));
}

TEST_CASE("find.eval.size_comparison", "[find]")
{
    SECTION("greater than")
    {
        SizeExpr expr(CompareMode::GreaterThan, 1024);
        CHECK(expr.evaluate(mockEntry("big", fs::file_type::regular, 2048)));
        CHECK_FALSE(expr.evaluate(mockEntry("small", fs::file_type::regular, 512)));
        CHECK_FALSE(expr.evaluate(mockEntry("exact", fs::file_type::regular, 1024)));
    }

    SECTION("less than")
    {
        SizeExpr expr(CompareMode::LessThan, 1024);
        CHECK(expr.evaluate(mockEntry("small", fs::file_type::regular, 512)));
        CHECK_FALSE(expr.evaluate(mockEntry("big", fs::file_type::regular, 2048)));
    }

    SECTION("exact")
    {
        SizeExpr expr(CompareMode::Exact, 1024);
        CHECK(expr.evaluate(mockEntry("exact", fs::file_type::regular, 1024)));
        CHECK_FALSE(expr.evaluate(mockEntry("off", fs::file_type::regular, 1023)));
    }
}

TEST_CASE("find.eval.and_both_true", "[find]")
{
    auto left = std::make_unique<NameExpr>("*.cpp", false);
    auto right = std::make_unique<TypeExpr>(fs::file_type::regular);
    AndExpr expr(std::move(left), std::move(right));

    CHECK(expr.evaluate(mockEntry("main.cpp", fs::file_type::regular)));
    CHECK_FALSE(expr.evaluate(mockEntry("main.hpp", fs::file_type::regular)));
    CHECK_FALSE(expr.evaluate(mockEntry("main.cpp", fs::file_type::directory)));
}

TEST_CASE("find.eval.or_one_true", "[find]")
{
    auto left = std::make_unique<NameExpr>("*.cpp", false);
    auto right = std::make_unique<NameExpr>("*.hpp", false);
    OrExpr expr(std::move(left), std::move(right));

    CHECK(expr.evaluate(mockEntry("main.cpp")));
    CHECK(expr.evaluate(mockEntry("main.hpp")));
    CHECK_FALSE(expr.evaluate(mockEntry("main.py")));
}

TEST_CASE("find.eval.not_negates", "[find]")
{
    auto inner = std::make_unique<NameExpr>("*.o", false);
    NotExpr expr(std::move(inner));

    CHECK(expr.evaluate(mockEntry("main.cpp")));
    CHECK_FALSE(expr.evaluate(mockEntry("main.o")));
}

TEST_CASE("find.eval.empty_file", "[find]")
{
    EmptyExpr expr;
    CHECK(expr.evaluate(mockEntry("empty.txt", fs::file_type::regular, 0)));
    CHECK_FALSE(expr.evaluate(mockEntry("notempty.txt", fs::file_type::regular, 100)));
}

TEST_CASE("find.eval.true_always", "[find]")
{
    TrueExpr expr;
    CHECK(expr.evaluate(mockEntry("anything")));
    CHECK(expr.evaluate(mockEntry("dir", fs::file_type::directory)));
}
