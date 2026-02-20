// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/FindExpression.hpp>
#include <shell/util/GlobMatcher.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

namespace endo::find
{

namespace
{

    /// Converts a string to lowercase.
    std::string toLower(std::string_view sv)
    {
        std::string result(sv);
        std::ranges::transform(result, result.begin(), [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    /// Checks if an argument looks like a find predicate or operator (starts with '-' or is '(' or '!').
    bool isExpressionToken(std::string_view arg)
    {
        if (arg.empty())
            return false;
        return arg[0] == '-' || arg == "(" || arg == ")" || arg == "!";
    }

    /// Parses a size string like "+5M", "-100c", "1024k" into bytes and compare mode.
    std::expected<std::pair<CompareMode, uintmax_t>, std::string> parseSizeArg(std::string_view arg)
    {
        auto mode = CompareMode::Exact;
        auto sv = arg;

        if (!sv.empty() && sv[0] == '+')
        {
            mode = CompareMode::GreaterThan;
            sv.remove_prefix(1);
        }
        else if (!sv.empty() && sv[0] == '-')
        {
            mode = CompareMode::LessThan;
            sv.remove_prefix(1);
        }

        // Parse numeric part
        uintmax_t value = 0;
        auto const [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
        if (ec != std::errc {})
            return std::unexpected(std::format("Invalid size value: {}", arg));

        // Parse optional suffix
        std::string_view suffix(ptr, static_cast<size_t>(sv.data() + sv.size() - ptr));
        uintmax_t multiplier = 512; // Default: 512-byte blocks (GNU find default)
        if (!suffix.empty())
        {
            if (suffix == "c")
                multiplier = 1;
            else if (suffix == "k")
                multiplier = 1024;
            else if (suffix == "M")
                multiplier = 1024 * 1024;
            else if (suffix == "G")
                multiplier = 1024ULL * 1024 * 1024;
            else
                return std::unexpected(std::format("Unknown size suffix: {}", suffix));
        }

        return std::pair { mode, value * multiplier };
    }

    /// Parses a numeric argument with optional +/- prefix.
    std::expected<std::pair<CompareMode, int>, std::string> parseNumericArg(std::string_view arg,
                                                                            std::string_view optionName)
    {
        auto mode = CompareMode::Exact;
        auto sv = arg;

        if (!sv.empty() && sv[0] == '+')
        {
            mode = CompareMode::GreaterThan;
            sv.remove_prefix(1);
        }
        else if (!sv.empty() && sv[0] == '-')
        {
            mode = CompareMode::LessThan;
            sv.remove_prefix(1);
        }

        int value = 0;
        auto const [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
        if (ec != std::errc {} || ptr != sv.data() + sv.size())
            return std::unexpected(std::format("{} requires a numeric argument, got: {}", optionName, arg));

        return std::pair { mode, value };
    }

    /// Recursive-descent parser for find expressions.
    class FindArgParser
    {
      public:
        explicit FindArgParser(std::span<std::string const> args): _args(args) {}

        std::expected<std::pair<FindOptions, std::unique_ptr<Expr>>, std::string> parse()
        {
            parsePaths();

            if (auto err = parseGlobalOptions(); !err.empty())
                return std::unexpected(std::move(err));

            if (_pos >= _args.size())
                return std::pair { std::move(_options), nullptr };

            auto expr = parseExpression();
            if (!expr.has_value())
                return std::unexpected(std::move(expr.error()));

            if (_pos < _args.size())
                return std::unexpected(std::format("Unexpected argument: {}", _args[_pos]));

            return std::pair { std::move(_options), std::move(expr.value()) };
        }

      private:
        std::span<std::string const> _args;
        size_t _pos = 0;
        FindOptions _options;

        [[nodiscard]] bool atEnd() const { return _pos >= _args.size(); }

        [[nodiscard]] std::string_view current() const { return _args[_pos]; }

        void advance() { ++_pos; }

        /// Consume leading path arguments (anything before first predicate/operator token).
        void parsePaths()
        {
            while (!atEnd() && !isExpressionToken(current()))
            {
                _options.searchPaths.emplace_back(std::string(current()));
                advance();
            }
            if (_options.searchPaths.empty())
                _options.searchPaths.emplace_back(".");
        }

        /// Extract global options (-maxdepth, -mindepth) from the remaining args.
        /// These are consumed and removed from the expression parsing.
        std::string parseGlobalOptions()
        {
            // Scan for global options, which can appear anywhere in the expression
            // but are consumed before expression parsing. We make a second pass.
            // For simplicity, handle them inline during expression parsing instead.
            // Actually, GNU find requires -maxdepth/-mindepth before any predicates,
            // so we parse them here at the current position.
            while (!atEnd())
            {
                if (current() == "-maxdepth")
                {
                    advance();
                    if (atEnd())
                        return "-maxdepth requires an argument";
                    int val = 0;
                    auto const [ptr, ec] =
                        std::from_chars(current().data(), current().data() + current().size(), val);
                    if (ec != std::errc {} || ptr != current().data() + current().size())
                        return std::format("-maxdepth requires a numeric argument, got: {}", current());
                    _options.maxDepth = val;
                    advance();
                }
                else if (current() == "-mindepth")
                {
                    advance();
                    if (atEnd())
                        return "-mindepth requires an argument";
                    int val = 0;
                    auto const [ptr, ec] =
                        std::from_chars(current().data(), current().data() + current().size(), val);
                    if (ec != std::errc {} || ptr != current().data() + current().size())
                        return std::format("-mindepth requires a numeric argument, got: {}", current());
                    _options.minDepth = val;
                    advance();
                }
                else
                {
                    break;
                }
            }
            return {};
        }

        /// expression := or_expr
        std::expected<std::unique_ptr<Expr>, std::string> parseExpression() { return parseOrExpr(); }

        /// or_expr := and_expr ("-o" | "-or" and_expr)*
        std::expected<std::unique_ptr<Expr>, std::string> parseOrExpr()
        {
            auto left = parseAndExpr();
            if (!left.has_value())
                return left;

            while (!atEnd() && (current() == "-o" || current() == "-or"))
            {
                advance(); // consume -o / -or
                auto right = parseAndExpr();
                if (!right.has_value())
                    return right;
                left = std::make_unique<OrExpr>(std::move(left.value()), std::move(right.value()));
            }
            return left;
        }

        /// and_expr := factor (("-a" | "-and")? factor)*
        std::expected<std::unique_ptr<Expr>, std::string> parseAndExpr()
        {
            auto left = parseFactor();
            if (!left.has_value())
                return left;

            while (!atEnd())
            {
                // Explicit AND
                if (current() == "-a" || current() == "-and")
                {
                    advance(); // consume -a / -and
                    auto right = parseFactor();
                    if (!right.has_value())
                        return right;
                    left = std::make_unique<AndExpr>(std::move(left.value()), std::move(right.value()));
                    continue;
                }

                // Implicit AND: next token is a predicate/operator (not -o, -or, or ))
                if (current() != "-o" && current() != "-or" && current() != ")")
                {
                    auto right = parseFactor();
                    if (!right.has_value())
                        return right;
                    left = std::make_unique<AndExpr>(std::move(left.value()), std::move(right.value()));
                    continue;
                }

                break;
            }
            return left;
        }

        /// factor := "-not" factor | "!" factor | "(" expression ")" | predicate
        std::expected<std::unique_ptr<Expr>, std::string> parseFactor()
        {
            if (atEnd())
                return std::unexpected("Expected expression, got end of arguments");

            // Negation
            if (current() == "-not" || current() == "!")
            {
                advance();
                auto operand = parseFactor();
                if (!operand.has_value())
                    return operand;
                return std::make_unique<NotExpr>(std::move(operand.value()));
            }

            // Grouping
            if (current() == "(")
            {
                advance(); // consume (
                auto expr = parseExpression();
                if (!expr.has_value())
                    return expr;
                if (atEnd() || current() != ")")
                    return std::unexpected("Missing closing ')'");
                advance(); // consume )
                return expr;
            }

            return parsePredicate();
        }

        /// Parse a single predicate (-name, -type, -size, etc.)
        std::expected<std::unique_ptr<Expr>, std::string> parsePredicate()
        {
            if (atEnd())
                return std::unexpected("Expected predicate, got end of arguments");

            auto const token = current();

            // -name PATTERN
            if (token == "-name" || token == "-iname")
            {
                auto const ci = (token == "-iname");
                advance();
                if (atEnd())
                    return std::unexpected(std::format("{} requires a pattern argument", token));
                auto pattern = std::string(current());
                advance();
                return std::make_unique<NameExpr>(std::move(pattern), ci);
            }

            // -path PATTERN
            if (token == "-path" || token == "-ipath")
            {
                auto const ci = (token == "-ipath");
                advance();
                if (atEnd())
                    return std::unexpected(std::format("{} requires a pattern argument", token));
                auto pattern = std::string(current());
                advance();
                return std::make_unique<PathExpr>(std::move(pattern), ci);
            }

            // -type TYPE
            if (token == "-type")
            {
                advance();
                if (atEnd())
                    return std::unexpected("-type requires a type argument (f, d, or l)");
                auto const typeStr = current();
                advance();
                if (typeStr == "f")
                    return std::make_unique<TypeExpr>(std::filesystem::file_type::regular);
                if (typeStr == "d")
                    return std::make_unique<TypeExpr>(std::filesystem::file_type::directory);
                if (typeStr == "l")
                    return std::make_unique<TypeExpr>(std::filesystem::file_type::symlink);
                return std::unexpected(std::format("Unknown file type: {} (expected f, d, or l)", typeStr));
            }

            // -size [+|-]N[c|k|M|G]
            if (token == "-size")
            {
                advance();
                if (atEnd())
                    return std::unexpected("-size requires a size argument");
                auto result = parseSizeArg(current());
                advance();
                if (!result.has_value())
                    return std::unexpected(std::move(result.error()));
                return std::make_unique<SizeExpr>(result->first, result->second);
            }

            // -mtime [+|-]N
            if (token == "-mtime")
            {
                advance();
                if (atEnd())
                    return std::unexpected("-mtime requires a numeric argument");
                auto result = parseNumericArg(current(), "-mtime");
                advance();
                if (!result.has_value())
                    return std::unexpected(std::move(result.error()));
                return std::make_unique<MtimeExpr>(result->first, result->second);
            }

            // -newer FILE
            if (token == "-newer")
            {
                advance();
                if (atEnd())
                    return std::unexpected("-newer requires a file argument");
                auto const filePath = current();
                advance();
                std::error_code ec;
                auto const mtime = std::filesystem::last_write_time(filePath, ec);
                if (ec)
                    return std::unexpected(
                        std::format("-newer: cannot stat '{}': {}", filePath, ec.message()));
                return std::make_unique<NewerExpr>(mtime);
            }

            // -empty
            if (token == "-empty")
            {
                advance();
                return std::make_unique<EmptyExpr>();
            }

            // -print / -print0 (actions — always true)
            if (token == "-print")
            {
                advance();
                return std::make_unique<TrueExpr>();
            }
            if (token == "-print0")
            {
                advance();
                _options.print0 = true;
                return std::make_unique<TrueExpr>();
            }

            return std::unexpected(std::format("Unknown predicate: {}", token));
        }
    };

} // namespace

// --- Expr evaluate implementations ---

bool NameExpr::evaluate(FindEntry const& entry) const
{
    if (caseInsensitive)
        return globMatchFilename(toLower(entry.filename), toLower(pattern));
    return globMatchFilename(entry.filename, pattern);
}

bool PathExpr::evaluate(FindEntry const& entry) const
{
    auto const pathStr = entry.path.string();
    if (caseInsensitive)
        return globMatch(toLower(pathStr), toLower(pattern));
    return globMatch(pathStr, pattern);
}

bool TypeExpr::evaluate(FindEntry const& entry) const
{
    return entry.type == fileType;
}

bool SizeExpr::evaluate(FindEntry const& entry) const
{
    switch (mode)
    {
        case CompareMode::Exact: return entry.size == bytes;
        case CompareMode::GreaterThan: return entry.size > bytes;
        case CompareMode::LessThan: return entry.size < bytes;
    }
    return false;
}

bool MtimeExpr::evaluate(FindEntry const& entry) const
{
    auto const now = std::filesystem::file_time_type::clock::now();
    auto const age = std::chrono::duration_cast<std::chrono::hours>(now - entry.mtime).count() / 24;
    switch (mode)
    {
        case CompareMode::Exact: return age == days;
        case CompareMode::GreaterThan: return age > days;
        case CompareMode::LessThan: return age < days;
    }
    return false;
}

bool NewerExpr::evaluate(FindEntry const& entry) const
{
    return entry.mtime > referenceTime;
}

bool EmptyExpr::evaluate(FindEntry const& entry) const
{
    if (entry.type == std::filesystem::file_type::regular)
        return entry.size == 0;
    if (entry.type == std::filesystem::file_type::directory)
    {
        std::error_code ec;
        return std::filesystem::is_empty(entry.path, ec) && !ec;
    }
    return false;
}

bool TrueExpr::evaluate(FindEntry const& /*entry*/) const
{
    return true;
}

bool AndExpr::evaluate(FindEntry const& entry) const
{
    return left->evaluate(entry) && right->evaluate(entry);
}

bool OrExpr::evaluate(FindEntry const& entry) const
{
    return left->evaluate(entry) || right->evaluate(entry);
}

bool NotExpr::evaluate(FindEntry const& entry) const
{
    return !operand->evaluate(entry);
}

// --- Parser entry point ---

std::expected<std::pair<FindOptions, std::unique_ptr<Expr>>, std::string> parseFindArgs(
    std::span<std::string const> args)
{
    return FindArgParser(args).parse();
}

} // namespace endo::find
