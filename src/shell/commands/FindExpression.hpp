// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endo::find
{

/// Represents a single filesystem entry being tested by find predicates.
struct FindEntry
{
    std::filesystem::path path;            ///< Full path from search root
    std::string filename;                  ///< Filename component only
    std::filesystem::file_type type;       ///< File type (regular, directory, symlink)
    uintmax_t size;                        ///< File size in bytes
    std::filesystem::file_time_type mtime; ///< Last modification time
    int depth;                             ///< Depth relative to search root
};

/// Comparison mode for numeric predicates (-size, -mtime).
enum class CompareMode : uint8_t
{
    Exact,       ///< Exact match (N)
    GreaterThan, ///< Greater than (+N)
    LessThan,    ///< Less than (-N)
};

/// Abstract base class for all find expression nodes.
struct Expr
{
    virtual ~Expr() = default;

    /// Evaluates this expression against a filesystem entry.
    /// @param entry The filesystem entry to test.
    /// @return true if the entry matches this expression.
    [[nodiscard]] virtual bool evaluate(FindEntry const& entry) const = 0;

    /// @return Whether evaluating this expression reads the entry's size, so the
    ///         caller can skip the per-entry size stat when no predicate needs it.
    [[nodiscard]] virtual bool requiresSize() const { return false; }

    /// @return Whether evaluating this expression reads the entry's mtime, so the
    ///         caller can skip the per-entry mtime stat when no predicate needs it.
    [[nodiscard]] virtual bool requiresMtime() const { return false; }
};

/// Matches filename against a glob pattern (-name, -iname).
struct NameExpr final: public Expr
{
    std::string pattern;  ///< Glob pattern to match against
    bool caseInsensitive; ///< true for -iname, false for -name

    NameExpr(std::string pat, bool ci): pattern(std::move(pat)), caseInsensitive(ci) {}

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;
};

/// Matches full path against a glob pattern (-path, -ipath).
struct PathExpr final: public Expr
{
    std::string pattern;  ///< Glob pattern to match against full path
    bool caseInsensitive; ///< true for -ipath, false for -path

    PathExpr(std::string pat, bool ci): pattern(std::move(pat)), caseInsensitive(ci) {}

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;
};

/// Matches file type (-type f, -type d, -type l).
struct TypeExpr final: public Expr
{
    std::filesystem::file_type fileType; ///< Expected file type

    explicit TypeExpr(std::filesystem::file_type ft): fileType(ft) {}

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;
};

/// Matches file size (-size [+|-]N[c|k|M|G]).
struct SizeExpr final: public Expr
{
    CompareMode mode; ///< Comparison mode
    uintmax_t bytes;  ///< Size threshold in bytes

    SizeExpr(CompareMode m, uintmax_t b): mode(m), bytes(b) {}

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;

    [[nodiscard]] bool requiresSize() const override { return true; }
};

/// Matches modification time (-mtime [+|-]N).
struct MtimeExpr final: public Expr
{
    CompareMode mode; ///< Comparison mode
    int days;         ///< Number of 24-hour periods

    MtimeExpr(CompareMode m, int d): mode(m), days(d) {}

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;

    [[nodiscard]] bool requiresMtime() const override { return true; }
};

/// Matches files newer than a reference file (-newer FILE).
struct NewerExpr final: public Expr
{
    std::filesystem::file_time_type referenceTime; ///< Reference file's mtime

    explicit NewerExpr(std::filesystem::file_time_type t): referenceTime(t) {}

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;

    [[nodiscard]] bool requiresMtime() const override { return true; }
};

/// Matches empty files or directories (-empty).
struct EmptyExpr final: public Expr
{
    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;

    [[nodiscard]] bool requiresSize() const override { return true; }
};

/// Always evaluates to true (used for -print, -print0 actions).
struct TrueExpr final: public Expr
{
    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;
};

/// Common base for binary boolean expressions (-a / -o). Holds the two operands and
/// derives the size/mtime requirements as the union of both operands' requirements, so
/// And/Or only need to define how the operands are combined in @ref evaluate.
struct BinaryExpr: public Expr
{
    std::unique_ptr<Expr> left;  ///< Left operand.
    std::unique_ptr<Expr> right; ///< Right operand.

    BinaryExpr(std::unique_ptr<Expr> l, std::unique_ptr<Expr> r): left(std::move(l)), right(std::move(r)) {}

    [[nodiscard]] bool requiresSize() const override { return left->requiresSize() || right->requiresSize(); }

    [[nodiscard]] bool requiresMtime() const override
    {
        return left->requiresMtime() || right->requiresMtime();
    }
};

/// Logical AND of two expressions (-a, implicit).
struct AndExpr final: public BinaryExpr
{
    using BinaryExpr::BinaryExpr;

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;
};

/// Logical OR of two expressions (-o, -or).
struct OrExpr final: public BinaryExpr
{
    using BinaryExpr::BinaryExpr;

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;
};

/// Logical NOT of an expression (-not, !).
struct NotExpr final: public Expr
{
    std::unique_ptr<Expr> operand;

    explicit NotExpr(std::unique_ptr<Expr> op): operand(std::move(op)) {}

    [[nodiscard]] bool evaluate(FindEntry const& entry) const override;

    [[nodiscard]] bool requiresSize() const override { return operand->requiresSize(); }

    [[nodiscard]] bool requiresMtime() const override { return operand->requiresMtime(); }
};

/// Parsed global options that are not part of the expression tree.
struct FindOptions
{
    std::vector<std::filesystem::path> searchPaths; ///< Starting directories (defaults to ".")
    std::optional<int> maxDepth;                    ///< Maximum directory depth
    std::optional<int> minDepth;                    ///< Minimum directory depth
    bool print0 = false;                            ///< Use null-terminated output instead of newline
};

/// Parses find command arguments into global options and an expression tree.
///
/// Grammar:
///   expression := or_expr
///   or_expr    := and_expr ("-o" | "-or" and_expr)*
///   and_expr   := factor (("-a" | "-and")? factor)*
///   factor     := "-not" factor | "!" factor | "(" expression ")" | predicate
///   predicate  := "-name" PAT | "-iname" PAT | "-path" PAT | "-ipath" PAT
///               | "-type" TYPE | "-size" SIZE | "-mtime" DAYS | "-newer" FILE
///               | "-empty" | "-print" | "-print0"
///
/// @param args Command arguments (excluding "find" itself).
/// @return Options and expression tree, or error message string.
[[nodiscard]] std::expected<std::pair<FindOptions, std::unique_ptr<Expr>>, std::string> parseFindArgs(
    std::span<std::string const> args);

} // namespace endo::find
