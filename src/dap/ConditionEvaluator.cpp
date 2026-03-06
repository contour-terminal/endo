// SPDX-License-Identifier: Apache-2.0
#include "ConditionEvaluator.hpp"

#include <CoreVM/vm/Program.hpp>

#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <utility>

namespace endo::dap
{

// =============================================================================
// Value helpers
// =============================================================================

bool ConditionEvaluator::Value::toBool() const
{
    switch (type)
    {
        case Type::Bool: return boolValue;
        case Type::Number: return numValue != 0.0;
        case Type::String: return !strValue.empty();
    }
    return false;
}

std::string ConditionEvaluator::Value::toString() const
{
    switch (type)
    {
        case Type::Bool: return boolValue ? "true" : "false";
        case Type::Number: {
            // Print integers without decimal point
            if (numValue == std::floor(numValue) && std::abs(numValue) < 1e15)
                return std::format("{}", static_cast<int64_t>(numValue));
            return std::format("{}", numValue);
        }
        case Type::String: return strValue;
    }
    return "";
}

// =============================================================================
// Parser
// =============================================================================

ConditionEvaluator::Parser::Parser(
    std::string expr, CoreVM::Runner const& runner, CoreVM::Program const& program, size_t fp, size_t funcId):
    _expr(std::move(expr)), _runner(runner), _program(program), _fp(fp), _funcId(funcId)
{
    advance();
}

void ConditionEvaluator::Parser::advance()
{
    // Skip whitespace
    while (_pos < _expr.size() && std::isspace(static_cast<unsigned char>(_expr[_pos])))
        ++_pos;

    if (_pos >= _expr.size())
    {
        _current = { .type = TokenType::Eof, .text = "", .numValue = 0.0 };
        return;
    }

    auto const ch = _expr[_pos];

    // Numbers
    if (std::isdigit(static_cast<unsigned char>(ch))
        || (ch == '-' && _pos + 1 < _expr.size() && std::isdigit(static_cast<unsigned char>(_expr[_pos + 1]))
            && (_current.type == TokenType::Eof || _current.type == TokenType::LParen
                || _current.type == TokenType::Plus || _current.type == TokenType::Minus
                || _current.type == TokenType::Star || _current.type == TokenType::Slash
                || _current.type == TokenType::Percent || _current.type == TokenType::Eq
                || _current.type == TokenType::Ne || _current.type == TokenType::Lt
                || _current.type == TokenType::Gt || _current.type == TokenType::Le
                || _current.type == TokenType::Ge || _current.type == TokenType::And
                || _current.type == TokenType::Or || _current.type == TokenType::Not)))
    {
        auto const start = _pos;
        if (ch == '-')
            ++_pos;
        while (_pos < _expr.size()
               && (std::isdigit(static_cast<unsigned char>(_expr[_pos])) || _expr[_pos] == '.'))
            ++_pos;
        auto const text = _expr.substr(start, _pos - start);
        double val = 0.0;
        std::from_chars(text.data(), text.data() + text.size(), val);
        _current = { .type = TokenType::Number, .text = text, .numValue = val };
        return;
    }

    // String literals
    if (ch == '"' || ch == '\'')
    {
        auto const quote = ch;
        ++_pos;
        std::string text;
        while (_pos < _expr.size() && _expr[_pos] != quote)
        {
            if (_expr[_pos] == '\\' && _pos + 1 < _expr.size())
            {
                ++_pos;
                text += _expr[_pos];
            }
            else
            {
                text += _expr[_pos];
            }
            ++_pos;
        }
        if (_pos < _expr.size())
            ++_pos; // skip closing quote
        _current = { .type = TokenType::String, .text = text, .numValue = 0.0 };
        return;
    }

    // Identifiers and keywords
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')
    {
        auto const start = _pos;
        while (_pos < _expr.size()
               && (std::isalnum(static_cast<unsigned char>(_expr[_pos])) || _expr[_pos] == '_'))
            ++_pos;
        auto const text = _expr.substr(start, _pos - start);
        if (text == "true")
            _current = { .type = TokenType::Bool, .text = text, .numValue = 1.0 };
        else if (text == "false")
            _current = { .type = TokenType::Bool, .text = text, .numValue = 0.0 };
        else
            _current = { .type = TokenType::Identifier, .text = text, .numValue = 0.0 };
        return;
    }

    // Operators
    switch (ch)
    {
        case '+':
            ++_pos;
            _current = { .type = TokenType::Plus, .text = "+", .numValue = 0.0 };
            return;
        case '-':
            ++_pos;
            _current = { .type = TokenType::Minus, .text = "-", .numValue = 0.0 };
            return;
        case '*':
            ++_pos;
            _current = { .type = TokenType::Star, .text = "*", .numValue = 0.0 };
            return;
        case '/':
            ++_pos;
            _current = { .type = TokenType::Slash, .text = "/", .numValue = 0.0 };
            return;
        case '%':
            ++_pos;
            _current = { .type = TokenType::Percent, .text = "%", .numValue = 0.0 };
            return;
        case '(':
            ++_pos;
            _current = { .type = TokenType::LParen, .text = "(", .numValue = 0.0 };
            return;
        case ')':
            ++_pos;
            _current = { .type = TokenType::RParen, .text = ")", .numValue = 0.0 };
            return;
        case '!':
            if (_pos + 1 < _expr.size() && _expr[_pos + 1] == '=')
            {
                _pos += 2;
                _current = { .type = TokenType::Ne, .text = "!=", .numValue = 0.0 };
            }
            else
            {
                ++_pos;
                _current = { .type = TokenType::Not, .text = "!", .numValue = 0.0 };
            }
            return;
        case '=':
            if (_pos + 1 < _expr.size() && _expr[_pos + 1] == '=')
            {
                _pos += 2;
                _current = { .type = TokenType::Eq, .text = "==", .numValue = 0.0 };
            }
            else
            {
                ++_pos;
                _current = { .type = TokenType::Error, .text = "=", .numValue = 0.0 };
            }
            return;
        case '<':
            if (_pos + 1 < _expr.size() && _expr[_pos + 1] == '=')
            {
                _pos += 2;
                _current = { .type = TokenType::Le, .text = "<=", .numValue = 0.0 };
            }
            else
            {
                ++_pos;
                _current = { .type = TokenType::Lt, .text = "<", .numValue = 0.0 };
            }
            return;
        case '>':
            if (_pos + 1 < _expr.size() && _expr[_pos + 1] == '=')
            {
                _pos += 2;
                _current = { .type = TokenType::Ge, .text = ">=", .numValue = 0.0 };
            }
            else
            {
                ++_pos;
                _current = { .type = TokenType::Gt, .text = ">", .numValue = 0.0 };
            }
            return;
        case '&':
            if (_pos + 1 < _expr.size() && _expr[_pos + 1] == '&')
            {
                _pos += 2;
                _current = { .type = TokenType::And, .text = "&&", .numValue = 0.0 };
            }
            else
            {
                ++_pos;
                _current = { .type = TokenType::Error, .text = "&", .numValue = 0.0 };
            }
            return;
        case '|':
            if (_pos + 1 < _expr.size() && _expr[_pos + 1] == '|')
            {
                _pos += 2;
                _current = { .type = TokenType::Or, .text = "||", .numValue = 0.0 };
            }
            else
            {
                ++_pos;
                _current = { .type = TokenType::Error, .text = "|", .numValue = 0.0 };
            }
            return;
        default: break;
    }

    ++_pos;
    _current = { .type = TokenType::Error, .text = std::string(1, ch), .numValue = 0.0 };
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::parse()
{
    auto result = parseOr();
    if (!result || _current.type != TokenType::Eof)
        return std::nullopt;
    return result;
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::parseOr()
{
    auto left = parseAnd();
    if (!left)
        return std::nullopt;
    while (_current.type == TokenType::Or)
    {
        advance();
        auto right = parseAnd();
        if (!right)
            return std::nullopt;
        left = Value { .type = Value::Type::Bool,
                       .numValue = 0.0,
                       .strValue = "",
                       .boolValue = left->toBool() || right->toBool() };
    }
    return left;
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::parseAnd()
{
    auto left = parseComparison();
    if (!left)
        return std::nullopt;
    while (_current.type == TokenType::And)
    {
        advance();
        auto right = parseComparison();
        if (!right)
            return std::nullopt;
        left = Value { .type = Value::Type::Bool,
                       .numValue = 0.0,
                       .strValue = "",
                       .boolValue = left->toBool() && right->toBool() };
    }
    return left;
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::parseComparison()
{
    auto left = parseAddSub();
    if (!left)
        return std::nullopt;

    while (_current.type == TokenType::Eq || _current.type == TokenType::Ne || _current.type == TokenType::Lt
           || _current.type == TokenType::Gt || _current.type == TokenType::Le
           || _current.type == TokenType::Ge)
    {
        auto const op = _current.type;
        advance();
        auto right = parseAddSub();
        if (!right)
            return std::nullopt;

        bool result = false;
        // String comparison
        if (left->type == Value::Type::String || right->type == Value::Type::String)
        {
            auto const& ls = left->toString();
            auto const& rs = right->toString();
            switch (op)
            {
                case TokenType::Eq: result = ls == rs; break;
                case TokenType::Ne: result = ls != rs; break;
                case TokenType::Lt: result = ls < rs; break;
                case TokenType::Gt: result = ls > rs; break;
                case TokenType::Le: result = ls <= rs; break;
                case TokenType::Ge: result = ls >= rs; break;
                default: break;
            }
        }
        else
        {
            auto const lv = left->numValue;
            auto const rv = right->numValue;
            switch (op)
            {
                case TokenType::Eq: result = lv == rv; break;
                case TokenType::Ne: result = lv != rv; break;
                case TokenType::Lt: result = lv < rv; break;
                case TokenType::Gt: result = lv > rv; break;
                case TokenType::Le: result = lv <= rv; break;
                case TokenType::Ge: result = lv >= rv; break;
                default: break;
            }
        }
        left = Value { .type = Value::Type::Bool, .numValue = 0.0, .strValue = "", .boolValue = result };
    }
    return left;
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::parseAddSub()
{
    auto left = parseMulDiv();
    if (!left)
        return std::nullopt;

    while (_current.type == TokenType::Plus || _current.type == TokenType::Minus)
    {
        auto const op = _current.type;
        advance();
        auto right = parseMulDiv();
        if (!right)
            return std::nullopt;

        // String concatenation with +
        if (op == TokenType::Plus
            && (left->type == Value::Type::String || right->type == Value::Type::String))
        {
            left = Value { .type = Value::Type::String,
                           .numValue = 0.0,
                           .strValue = left->toString() + right->toString(),
                           .boolValue = false };
        }
        else
        {
            auto const val =
                (op == TokenType::Plus) ? left->numValue + right->numValue : left->numValue - right->numValue;
            left = Value { .type = Value::Type::Number, .numValue = val, .strValue = "", .boolValue = false };
        }
    }
    return left;
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::parseMulDiv()
{
    auto left = parseUnary();
    if (!left)
        return std::nullopt;

    while (_current.type == TokenType::Star || _current.type == TokenType::Slash
           || _current.type == TokenType::Percent)
    {
        auto const op = _current.type;
        advance();
        auto right = parseUnary();
        if (!right)
            return std::nullopt;

        double val = 0.0;
        switch (op)
        {
            case TokenType::Star: val = left->numValue * right->numValue; break;
            case TokenType::Slash:
                val = right->numValue != 0.0 ? left->numValue / right->numValue : 0.0;
                break;
            case TokenType::Percent:
                val = right->numValue != 0.0 ? std::fmod(left->numValue, right->numValue) : 0.0;
                break;
            default: break;
        }
        left = Value { .type = Value::Type::Number, .numValue = val, .strValue = "", .boolValue = false };
    }
    return left;
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::parseUnary()
{
    if (_current.type == TokenType::Not)
    {
        advance();
        auto val = parseUnary();
        if (!val)
            return std::nullopt;
        return Value {
            .type = Value::Type::Bool, .numValue = 0.0, .strValue = "", .boolValue = !val->toBool()
        };
    }
    if (_current.type == TokenType::Minus)
    {
        advance();
        auto val = parseUnary();
        if (!val)
            return std::nullopt;
        return Value {
            .type = Value::Type::Number, .numValue = -val->numValue, .strValue = "", .boolValue = false
        };
    }
    return parsePrimary();
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::parsePrimary()
{
    switch (_current.type)
    {
        case TokenType::Number: {
            auto val = Value {
                .type = Value::Type::Number, .numValue = _current.numValue, .strValue = "", .boolValue = false
            };
            advance();
            return val;
        }
        case TokenType::String: {
            auto val = Value {
                .type = Value::Type::String, .numValue = 0.0, .strValue = _current.text, .boolValue = false
            };
            advance();
            return val;
        }
        case TokenType::Bool: {
            auto val = Value { .type = Value::Type::Bool,
                               .numValue = 0.0,
                               .strValue = "",
                               .boolValue = _current.numValue != 0.0 };
            advance();
            return val;
        }
        case TokenType::Identifier: {
            auto const name = _current.text;
            advance();
            return lookupVariable(name);
        }
        case TokenType::LParen: {
            advance();
            auto val = parseOr();
            if (!val || _current.type != TokenType::RParen)
                return std::nullopt;
            advance();
            return val;
        }
        default: return std::nullopt;
    }
}

std::optional<ConditionEvaluator::Value> ConditionEvaluator::Parser::lookupVariable(std::string const& name)
{
    auto const& debugVars = _program.constants().getFunctionDebugVarInfo(_funcId);
    for (auto const& dvi: debugVars)
    {
        if (dvi.name != name)
            continue;

        auto const stackIndex = _fp + dvi.allocaIndex;
        if (stackIndex >= _runner.getStackPointer())
            return std::nullopt;

        auto const rawValue = _runner.stack()[stackIndex];

        switch (dvi.type)
        {
            case CoreVM::LiteralType::Number:
                return Value { .type = Value::Type::Number,
                               .numValue = static_cast<double>(static_cast<CoreVM::CoreNumber>(rawValue)),
                               .strValue = "",
                               .boolValue = false };
            case CoreVM::LiteralType::Boolean:
                return Value {
                    .type = Value::Type::Bool, .numValue = 0.0, .strValue = "", .boolValue = rawValue != 0
                };
            case CoreVM::LiteralType::String:
                if (rawValue != 0 && _runner.isKnownString(rawValue))
                    return Value { .type = Value::Type::String,
                                   .numValue = 0.0,
                                   .strValue =
                                       std::string(*reinterpret_cast<CoreVM::CoreString const*>(rawValue)),
                                   .boolValue = false };
                return Value {
                    .type = Value::Type::String, .numValue = 0.0, .strValue = "", .boolValue = false
                };
            case CoreVM::LiteralType::Float:
                return Value { .type = Value::Type::Number,
                               .numValue = std::bit_cast<double>(rawValue),
                               .strValue = "",
                               .boolValue = false };
            default:
                return Value { .type = Value::Type::Number,
                               .numValue = static_cast<double>(rawValue),
                               .strValue = "",
                               .boolValue = false };
        }
    }
    return std::nullopt;
}

// =============================================================================
// Static public API
// =============================================================================

bool ConditionEvaluator::evaluate(std::string const& expression,
                                  CoreVM::Runner const& runner,
                                  CoreVM::Program const& program,
                                  size_t fp,
                                  size_t funcId)
{
    if (expression.empty())
        return true;

    Parser parser(expression, runner, program, fp, funcId);
    auto result = parser.parse();
    if (!result)
        return true; // fail-open on parse errors
    return result->toBool();
}

bool ConditionEvaluator::checkHitCondition(std::string const& hitCondition, size_t hitCount)
{
    if (hitCondition.empty())
        return true;

    auto const& str = hitCondition;
    size_t n = 0;

    if (str.starts_with(">="))
    {
        std::from_chars(str.data() + 2, str.data() + str.size(), n);
        return hitCount >= n;
    }
    if (str.starts_with(">"))
    {
        std::from_chars(str.data() + 1, str.data() + str.size(), n);
        return hitCount > n;
    }
    if (str.starts_with("=="))
    {
        std::from_chars(str.data() + 2, str.data() + str.size(), n);
        return hitCount == n;
    }
    if (str.starts_with("%"))
    {
        std::from_chars(str.data() + 1, str.data() + str.size(), n);
        return n > 0 && (hitCount % n) == 0;
    }

    // Plain number: exact match
    std::from_chars(str.data(), str.data() + str.size(), n);
    return hitCount == n;
}

std::string ConditionEvaluator::interpolateLogMessage(std::string const& logMessage,
                                                      CoreVM::Runner const& runner,
                                                      CoreVM::Program const& program,
                                                      size_t fp,
                                                      size_t funcId)
{
    std::string result;
    result.reserve(logMessage.size());

    size_t pos = 0;
    while (pos < logMessage.size())
    {
        if (logMessage[pos] == '{')
        {
            auto const end = logMessage.find('}', pos + 1);
            if (end != std::string::npos)
            {
                auto const expr = logMessage.substr(pos + 1, end - pos - 1);
                auto const val = evaluateToString(expr, runner, program, fp, funcId);
                result += val.value_or("?");
                pos = end + 1;
            }
            else
            {
                result += logMessage[pos];
                ++pos;
            }
        }
        else
        {
            result += logMessage[pos];
            ++pos;
        }
    }
    return result;
}

std::optional<std::string> ConditionEvaluator::evaluateToString(std::string const& expression,
                                                                CoreVM::Runner const& runner,
                                                                CoreVM::Program const& program,
                                                                size_t fp,
                                                                size_t funcId)
{
    if (expression.empty())
        return std::nullopt;

    Parser parser(expression, runner, program, fp, funcId);
    auto result = parser.parse();
    if (!result)
        return std::nullopt;
    return result->toString();
}

} // namespace endo::dap
