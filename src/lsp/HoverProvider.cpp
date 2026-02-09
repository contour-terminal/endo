// SPDX-License-Identifier: Apache-2.0
#include "HoverProvider.hpp"

#include <unordered_map>
#include <vector>

#include "StubRuntime.hpp"

#include <endo-language/AST.hpp>
#include <endo-language/Lexer.hpp>
#include <endo-language/Parser.hpp>
#include <endo-language/Type.hpp>

namespace endo::lsp
{

namespace
{

    /// Returns hover markdown for a keyword token.
    [[nodiscard]] std::optional<std::string> keywordHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case Let:
                return "`let` \u2014 Introduces an immutable binding\n\n```\nlet name = value\nlet f x y = "
                       "body\n```";
            case Mut:
                return "`mut` \u2014 Marks a binding as mutable\n\n```\nlet mut counter = 0\ncounter <- "
                       "counter + 1\n```";
            case Fun:
                return "`fun` \u2014 Lambda expression (anonymous function)\n\n```\nfun x -> x + 1\nfun x y "
                       "-> x + y\n```";
            case Match:
                return "`match` \u2014 Pattern matching expression\n\n```\nmatch value with\n| pattern1 -> "
                       "result1\n| pattern2 -> result2\n```";
            case With: return "`with` \u2014 Introduces match arms or exception handlers";
            case When:
                return "`when` \u2014 Guard clause in pattern matching\n\n```\n| x when x > 0 -> "
                       "\"positive\"\n```";
            case Type:
                return "`type` \u2014 Defines a discriminated union type\n\n```\ntype Shape =\n| Circle of "
                       "float\n| Rectangle of float * float\n```";
            case Of: return "`of` \u2014 Specifies the payload type in a union case";
            case Rec:
                return "`rec` \u2014 Marks a binding as recursive\n\n```\nlet rec factorial n =\n  if n <= 1 "
                       "then 1\n  else n * factorial (n - 1)\n```";
            case And:
                return "`and` \u2014 Defines mutually recursive functions\n\n```\nlet rec isEven n = ... and "
                       "isOdd n = ...\n```";
            case As:
                return "`as` \u2014 Pattern alias, binds the whole matched value\n\n```\n| (Some x) as opt "
                       "-> ...\n```";
            case Try:
                return "`try` \u2014 Error handling expression\n\n```\ntry expression with\n| Error e -> "
                       "handler\n```";
            case Finally:
                return "`finally` \u2014 Code that always executes after try\n\n```\ntry expression finally "
                       "cleanup\n```";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for constructor tokens.
    [[nodiscard]] std::optional<std::string> constructorHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case OptionSome: return "`Some` : `'a -> option<'a>`\n\nWraps a value in an option type.";
            case OptionNone: return "`None` : `option<'a>`\n\nRepresents the absence of a value.";
            case ResultOk: return "`Ok` : `'a -> result<'a, 'e>`\n\nWraps a success value in a result type.";
            case ResultError:
                return "`Error` : `'e -> result<'a, 'e>`\n\nWraps an error value in a result type.";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for operator tokens.
    [[nodiscard]] std::optional<std::string> operatorHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case ForwardPipe:
                return "`|>` \u2014 Forward pipe operator\n\nPasses the left operand as the last argument to "
                       "the right function.\n\n```\nvalue |> f |> g\n```";
            case Arrow:
                return "`->` \u2014 Arrow operator\n\nUsed in function types, lambda expressions, and match "
                       "arms.";
            case LeftArrow:
                return "`<-` \u2014 Mutation operator\n\nAssigns a new value to a mutable binding.";
            case ColonColon:
                return "`::` \u2014 List cons operator\n\nPrepends an element to a list.\n\n```\n1 :: [2; 3] "
                       " // [1; 2; 3]\n```";
            case DotDot:
                return "`..` \u2014 Range operator\n\nCreates a range of values.\n\n```\n[1..10]\n```";
            case Question:
                return "`?` \u2014 Error propagation operator\n\nUnwraps Ok/Some or returns early with "
                       "Error/None.";
            case EqualEqual: return "`==` \u2014 Equality comparison";
            case NotEqual: return "`!=` \u2014 Inequality comparison";
            case AmpAmp: return "`&&` \u2014 Logical AND";
            case PipePipe: return "`||` \u2014 Logical OR";
            case StarStar: return "`**` \u2014 Exponentiation operator";
            case Pipe: return "`|` \u2014 Process pipe (shell) or match arm separator";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for builtin function identifiers.
    [[nodiscard]] std::optional<std::string> builtinHover(std::string const& name)
    {
        static std::unordered_map<std::string, std::string> const builtins = {
            { "print", "`print` : `'a -> unit`\n\nPrints a value to stdout without a trailing newline." },
            { "println", "`println` : `'a -> unit`\n\nPrints a value to stdout followed by a newline." },
            { "string_length", "`string_length` : `string -> int`\n\nReturns the length of a string." },
            { "string_concat",
              "`string_concat` : `string -> string -> string`\n\nConcatenates two strings." },
            { "string_substring",
              "`string_substring` : `int -> int -> string -> string`\n\nExtracts a substring (start, length, "
              "string)." },
            { "int_to_string",
              "`int_to_string` : `int -> string`\n\nConverts an integer to its string representation." },
            { "string_to_int",
              "`string_to_int` : `string -> option<int>`\n\nParses a string as an integer, returning None on "
              "failure." },
            { "true", "`true` : `bool`\n\nBoolean true value." },
            { "false", "`false` : `bool`\n\nBoolean false value." },
        };

        if (auto const it = builtins.find(name); it != builtins.end())
            return it->second;
        return std::nullopt;
    }

    /// Renders an AST expression as a concise source string for hover preview.
    /// Handles common expression types; returns std::nullopt for complex expressions.
    /// @param expr The expression to render
    /// @return A preview string, or std::nullopt if the expression is too complex
    [[nodiscard]] std::optional<std::string> exprToString(ast::Expr const& expr)
    {
        if (auto const* e = dynamic_cast<ast::IntLiteralExpr const*>(&expr))
            return std::to_string(e->value);
        if (auto const* e = dynamic_cast<ast::FloatLiteralExpr const*>(&expr))
        {
            auto s = std::to_string(e->value);
            // Remove trailing zeros but keep at least one decimal digit
            if (s.find('.') != std::string::npos)
            {
                s.erase(s.find_last_not_of('0') + 1);
                if (s.back() == '.')
                    s += '0';
            }
            return s;
        }
        if (auto const* e = dynamic_cast<ast::BoolLiteralExpr const*>(&expr))
            return e->value ? std::string("true") : std::string("false");
        if (auto const* e = dynamic_cast<ast::LiteralExpr const*>(&expr))
            return "\"" + e->value + "\"";
        if (auto const* e = dynamic_cast<ast::IdentifierExpr const*>(&expr))
            return e->name;
        if (auto const* e = dynamic_cast<ast::ParenExpr const*>(&expr))
        {
            if (auto inner = exprToString(*e->inner))
                return "(" + *inner + ")";
        }
        if (auto const* e = dynamic_cast<ast::OptionExpr const*>(&expr))
        {
            if (!e->isSome)
                return std::string("None");
            if (e->value)
                if (auto val = exprToString(*e->value))
                    return "Some " + *val;
            return std::string("Some ...");
        }
        if (auto const* e = dynamic_cast<ast::ResultExpr const*>(&expr))
        {
            auto const prefix = e->isOk ? std::string("Ok ") : std::string("Error ");
            if (e->payload)
                if (auto val = exprToString(*e->payload))
                    return prefix + *val;
            return prefix + "...";
        }
        if (auto const* e = dynamic_cast<ast::TupleExpr const*>(&expr))
        {
            std::string result = "(";
            for (size_t i = 0; i < e->elements.size(); ++i)
            {
                if (i > 0)
                    result += ", ";
                if (auto val = exprToString(*e->elements[i]))
                    result += *val;
                else
                    result += "...";
            }
            result += ")";
            return result;
        }
        if (auto const* e = dynamic_cast<ast::UnaryExpr const*>(&expr))
        {
            if (auto operand = exprToString(*e->operand))
            {
                switch (e->op)
                {
                    case ast::UnaryOp::Neg: return "-" + *operand;
                    case ast::UnaryOp::Not: return "!" + *operand;
                }
            }
        }
        if (auto const* e = dynamic_cast<ast::BinaryExpr const*>(&expr))
        {
            auto lhs = exprToString(*e->left);
            auto rhs = exprToString(*e->right);
            if (lhs && rhs)
            {
                auto const opStr = [&]() -> std::string {
                    switch (e->op)
                    {
                        case ast::BinaryOp::Add: return " + ";
                        case ast::BinaryOp::Sub: return " - ";
                        case ast::BinaryOp::Mul: return " * ";
                        case ast::BinaryOp::Div: return " / ";
                        case ast::BinaryOp::Mod: return " % ";
                        case ast::BinaryOp::Pow: return " ** ";
                        case ast::BinaryOp::Eq: return " == ";
                        case ast::BinaryOp::Ne: return " != ";
                        case ast::BinaryOp::Lt: return " < ";
                        case ast::BinaryOp::Le: return " <= ";
                        case ast::BinaryOp::Gt: return " > ";
                        case ast::BinaryOp::Ge: return " >= ";
                        case ast::BinaryOp::And: return " && ";
                        case ast::BinaryOp::Or: return " || ";
                    }
                    return " ? ";
                }();
                return *lhs + opStr + *rhs;
            }
        }
        if (auto const* e = dynamic_cast<ast::ApplicationExpr const*>(&expr))
        {
            auto func = exprToString(*e->function);
            auto arg = exprToString(*e->argument);
            if (func && arg)
                return *func + " " + *arg;
        }
        if (auto const* e = dynamic_cast<ast::LambdaExpr const*>(&expr))
        {
            std::string result = "fun";
            for (auto const& param: e->parameters)
                result += " " + param.name;
            result += " -> ...";
            return result;
        }
        if (auto const* e = dynamic_cast<ast::PipelineExpr const*>(&expr))
        {
            auto val = exprToString(*e->value);
            auto func = exprToString(*e->function);
            if (val && func)
                return *val + " |> " + *func;
        }
        return std::nullopt;
    }

    /// Formats hover markdown for a let binding signature (variable or function).
    /// @param name The binding name
    /// @param isMutable Whether the binding is mutable
    /// @param isRecursive Whether the binding is recursive
    /// @param parameters Function parameters (empty for simple bindings)
    /// @param returnType Optional return type annotation
    /// @param valuePreview Optional preview of the value expression source text
    /// @return Markdown hover string
    [[nodiscard]] std::string formatLetBinding(std::string const& name,
                                               bool isMutable,
                                               bool isRecursive,
                                               std::vector<ast::TypedParameter> const& parameters,
                                               std::optional<TypePtr> const& returnType,
                                               std::optional<std::string> const& valuePreview = {})
    {
        std::string result;

        if (!parameters.empty())
        {
            result = "`" + name + "` \u2014 function\n\n```endo\nlet ";
            if (isRecursive)
                result += "rec ";
            result += name;
            for (auto const& param: parameters)
            {
                if (param.typeAnnotation)
                    result += " (" + param.name + ": " + toString(*param.typeAnnotation) + ")";
                else
                    result += " " + param.name;
            }
            if (returnType)
                result += ": " + toString(*returnType);
            result += "\n```";
        }
        else
        {
            result = "`" + name + "` \u2014 ";
            if (isMutable)
                result += "mutable ";
            result += "binding\n\n```endo\nlet ";
            if (isMutable)
                result += "mut ";
            result += name;
            if (returnType)
                result += ": " + toString(*returnType);
            if (valuePreview && !valuePreview->empty())
                result += " = " + *valuePreview;
            result += "\n```";
        }

        return result;
    }

    /// Formats hover markdown for a function parameter.
    /// @param param The parameter with optional type annotation
    /// @param functionName The enclosing function name
    /// @return Markdown hover string
    [[nodiscard]] std::string formatParameter(ast::TypedParameter const& param,
                                              std::string const& functionName)
    {
        auto result = "`" + param.name + "` \u2014 parameter of `" + functionName + "`";
        if (param.typeAnnotation)
            result += "\n\n```endo\n" + param.name + ": " + toString(*param.typeAnnotation) + "\n```";
        return result;
    }

    /// Returns hover markdown for a user-defined binding or function parameter.
    ///
    /// Parses the source into an AST and searches top-level `let` bindings and their
    /// parameters for a matching name.
    ///
    /// @param source The full document text
    /// @param name The identifier name to look up
    /// @return Hover markdown if a matching binding was found, otherwise std::nullopt
    [[nodiscard]] std::optional<std::string> bindingHover(std::string const& source, std::string const& name)
    {
        CoreVM::Runtime runtime;
        registerStubRuntime(runtime);

        CoreVM::diagnostics::BufferedReport report;
        Parser parser(runtime, report, std::make_unique<StringSource>(source));
        auto astRoot = parser.parse();
        if (!astRoot)
            return std::nullopt;

        // Collect top-level LetBindingStmt nodes
        std::vector<ast::LetBindingStmt const*> bindings;
        if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(astRoot.get()))
        {
            for (auto const& stmt: compound->statements)
            {
                if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(stmt.get()))
                    bindings.push_back(letStmt);
            }
        }
        else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(astRoot.get()))
        {
            bindings.push_back(letStmt);
        }

        // Check binding names (including and-bindings for mutual recursion)
        for (auto const* letStmt: bindings)
        {
            if (letStmt->name == name)
            {
                auto valuePreview = std::optional<std::string> {};
                if (!letStmt->isFunction() && letStmt->value)
                    valuePreview = exprToString(*letStmt->value);
                return formatLetBinding(name,
                                        letStmt->isMutable,
                                        letStmt->isRecursive,
                                        letStmt->parameters,
                                        letStmt->returnType,
                                        valuePreview);
            }

            for (auto const& andBinding: letStmt->andBindings)
            {
                if (andBinding.name == name)
                    return formatLetBinding(name, false, true, andBinding.parameters, andBinding.returnType);
            }
        }

        // Check function parameters
        for (auto const* letStmt: bindings)
        {
            for (auto const& param: letStmt->parameters)
            {
                if (param.name == name)
                    return formatParameter(param, letStmt->name);
            }

            for (auto const& andBinding: letStmt->andBindings)
            {
                for (auto const& param: andBinding.parameters)
                {
                    if (param.name == name)
                        return formatParameter(param, andBinding.name);
                }
            }
        }

        return std::nullopt;
    }

} // namespace

std::optional<Hover> computeHover(std::string const& source, Position position)
{
    // Tokenize with F# mode for proper operator recognition
    auto lexer = Lexer { std::make_unique<StringSource>(source) };
    lexer.enterFSharpExpr();

    std::vector<TokenInfo> tokens;
    while (lexer.currentToken() != Token::EndOfInput)
    {
        tokens.emplace_back(TokenInfo { lexer.currentToken(), lexer.currentLiteral(), lexer.currentRange() });
        lexer.nextToken();
    }

    for (auto const& tokenInfo: tokens)
    {
        if (tokenInfo.token == Token::EndOfInput)
            continue;

        if (!containsPosition(tokenInfo.location, position))
            continue;

        auto const range = toRange(tokenInfo.location);

        // Try keyword hover
        if (auto text = keywordHover(tokenInfo.token))
            return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };

        // Try constructor hover
        if (auto text = constructorHover(tokenInfo.token))
            return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };

        // Try operator hover
        if (auto text = operatorHover(tokenInfo.token))
            return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };

        // Try builtin hover for identifiers
        if (tokenInfo.token == Token::Identifier)
        {
            if (auto text = builtinHover(tokenInfo.literal))
                return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };

            // Try user-defined binding hover (requires AST parsing)
            if (auto text = bindingHover(source, tokenInfo.literal))
                return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };
        }

        // No hover info for this token
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace endo::lsp
