// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file ConditionEvaluator.hpp
/// @brief Lightweight expression evaluator for breakpoint conditions and log message interpolation.

#include <CoreVM/vm/Runner.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace CoreVM
{
class Program;
struct DebugVarInfo;
} // namespace CoreVM

namespace endo::dap
{

/// Lightweight expression evaluator for breakpoint conditions, hit conditions,
/// and log message interpolation.
///
/// Supports: variable references, numeric/string/bool literals, comparison operators
/// (==, !=, <, >, <=, >=), logical operators (&&, ||, !), arithmetic (+, -, *, /, %),
/// and parenthesized sub-expressions.
class ConditionEvaluator
{
  public:
    /// Evaluates a condition expression against the current VM state.
    /// @param expression Condition expression string
    /// @param runner Current VM runner (for stack access)
    /// @param program Compiled program (for debug variable info)
    /// @param fp Frame pointer for variable lookup
    /// @param funcId Function index for debug variable lookup
    /// @return true if condition is met, true on parse errors (fail-open)
    [[nodiscard]] static bool evaluate(std::string const& expression,
                                       CoreVM::Runner const& runner,
                                       CoreVM::Program const& program,
                                       size_t fp,
                                       size_t funcId);

    /// Checks whether a hit condition is satisfied for a given hit count.
    /// Supported formats: "N" (exact), ">=N", ">N", "==N", "%N" (every Nth).
    /// @param hitCondition Hit condition expression
    /// @param hitCount Current hit count
    /// @return true if the hit condition is satisfied
    [[nodiscard]] static bool checkHitCondition(std::string const& hitCondition, size_t hitCount);

    /// Interpolates a log message, replacing {expression} placeholders with evaluated values.
    /// @param logMessage Log message template with {expression} placeholders
    /// @param runner Current VM runner
    /// @param program Compiled program
    /// @param fp Frame pointer
    /// @param funcId Function index
    /// @return Interpolated message string
    [[nodiscard]] static std::string interpolateLogMessage(std::string const& logMessage,
                                                           CoreVM::Runner const& runner,
                                                           CoreVM::Program const& program,
                                                           size_t fp,
                                                           size_t funcId);

    /// Evaluates an expression and returns its string representation.
    /// @param expression Expression string
    /// @param runner Current VM runner
    /// @param program Compiled program
    /// @param fp Frame pointer
    /// @param funcId Function index
    /// @return String representation of the result, or nullopt on error
    [[nodiscard]] static std::optional<std::string> evaluateToString(std::string const& expression,
                                                                     CoreVM::Runner const& runner,
                                                                     CoreVM::Program const& program,
                                                                     size_t fp,
                                                                     size_t funcId);

  private:
    /// Token types for the mini expression lexer.
    enum class TokenType // NOLINT(performance-enum-size)
    {
        Number,
        String,
        Bool,
        Identifier,
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Eq,
        Ne,
        Lt,
        Gt,
        Le,
        Ge,
        And,
        Or,
        Not,
        LParen,
        RParen,
        Eof,
        Error,
    };

    /// A token produced by the mini lexer.
    struct Token
    {
        TokenType type = TokenType::Eof;
        std::string text;
        double numValue = 0.0;
    };

    /// Value type for expression evaluation.
    struct Value
    {
        enum class Type // NOLINT(performance-enum-size)
        {
            Number,
            String,
            Bool,
        } type = Type::Number;
        double numValue = 0.0;
        std::string strValue;
        bool boolValue = false;

        [[nodiscard]] bool toBool() const;
        [[nodiscard]] std::string toString() const;
    };

    /// Mini expression parser/evaluator.
    class Parser
    {
      public:
        Parser(std::string expr,
               CoreVM::Runner const& runner,
               CoreVM::Program const& program,
               size_t fp,
               size_t funcId);

        [[nodiscard]] std::optional<Value> parse();

      private:
        void advance();
        [[nodiscard]] std::optional<Value> parseOr();
        [[nodiscard]] std::optional<Value> parseAnd();
        [[nodiscard]] std::optional<Value> parseComparison();
        [[nodiscard]] std::optional<Value> parseAddSub();
        [[nodiscard]] std::optional<Value> parseMulDiv();
        [[nodiscard]] std::optional<Value> parseUnary();
        [[nodiscard]] std::optional<Value> parsePrimary();
        [[nodiscard]] std::optional<Value> lookupVariable(std::string const& name);

        std::string _expr;
        size_t _pos = 0;
        Token _current;

        CoreVM::Runner const& _runner;
        CoreVM::Program const& _program;
        size_t _fp;
        size_t _funcId;
    };
};

} // namespace endo::dap
