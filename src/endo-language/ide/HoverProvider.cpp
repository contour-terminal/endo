// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/AST.hpp>
#include <endo-language/builtins/PropertyDescriptors.hpp>
#include <endo-language/builtins/StubRuntime.hpp>
#include <endo-language/ide/HoverProvider.hpp>
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>
#include <endo-language/types/Type.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

#include <algorithm>
#include <format>
#include <unordered_map>
#include <vector>

namespace endo
{

namespace
{

    /// Renders a builtin record's fields as a hover `**Fields:**` list, e.g. "`name: str`, `size:
    /// Size`".
    ///
    /// Derived from CoreVM's descriptors rather than written out per entry, so a field added or
    /// retyped there cannot leave this text stale. It had been: two entries described ProcessInfo's
    /// `mem` as a float long after it became a Size, and FileInfo's new `path` had to be pasted
    /// into two places by hand.
    ///
    /// @param recordName Builtin record to describe.
    /// @return The rendered field list, or an empty string for a name CoreVM does not know.
    [[nodiscard]] std::string fieldList(std::string const& recordName)
    {
        static auto const byRecord = builtinRecordFields(CoreVM::builtinTypes());

        auto const it = byRecord.find(recordName);
        if (it == byRecord.end())
            return {};

        auto rendered = std::string {};
        for (auto const& field: it->second)
        {
            if (!rendered.empty())
                rendered += ", ";
            rendered += std::format("`{}: {}`", field.name, field.typeName);
        }
        return rendered;
    }

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
            case Lazy:
                return "`lazy` \u2014 Defers evaluation until `force` is called\n\n```\nlet x = lazy (1 + "
                       "2)\n"
                       "println (force x)\n```";
            case Seq:
                return "`seq` \u2014 Lazy sequence builder\n\n```\nlet fibs = seq { yield 0; yield 1; yield! "
                       "rest }\nfibs |> take 10 |> toList |> each println\n```";
            case Yield:
                return "`yield` \u2014 Produces a value in a seq expression\n\n```\nseq { yield 1; yield 2; "
                       "yield! rest }\n```\n\nUse `yield!` (yield-bang) to splice another sequence.";
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

    /// Returns hover markdown for a shell or agent configuration property.
    ///
    /// Read from the descriptor table rather than restated here. That table documents itself as the
    /// single source of truth, and registration, completion and type inference already derive from
    /// it; hover was the one consumer keeping its own copy, which had drifted into different
    /// wording and covered only 58 of the 84 properties — every `shell_prompt_color_*` and
    /// `agent_local_*` property had no hover at all.
    [[nodiscard]] std::optional<std::string> propertyHover(std::string const& name)
    {
        auto const descriptors = allPropertyDescriptors();
        auto const it = std::ranges::find_if(descriptors, [&](auto const& d) { return d.name == name; });
        if (it == descriptors.end())
            return std::nullopt;

        // The signature line hover uses everywhere else, then the descriptor's prose. Rows open
        // with a `**name** -- property` header for the completion detail panel, which the
        // signature line already says; drop it when present rather than print the name twice.
        auto detail = std::string { it->detail };
        auto const header = std::format("**{}** -- property", it->name);
        if (detail.starts_with(header))
        {
            auto const body = detail.find("\n\n");
            detail = body != std::string::npos ? detail.substr(body + 2) : std::string {};
        }

        return std::format("`{}` : `{}`\n\n{}{}",
                           it->name,
                           CoreVM::tos(it->type),
                           detail,
                           it->readOnly ? "\n\nRead-only." : "\n\nRead or write with `<-`.");
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
            { "env",
              "`env` : `string -> option<string>`\n\nReturns `Some value` if the environment variable is "
              "set, `None` if not found." },
            { "Size",
              std::format("`Size` \u2014 Record type for byte sizes with human-readable display\n\n"
                          "**Fields:** {}\n\n"
                          "```endo\nSize.fromBytes 1024  // 1 KB\n"
                          "Size.fromKB 5       // 5 KB\n"
                          "1MB                  // size literal: 1 MB\n"
                          "3.5KB                // float literal: 3584 bytes\n"
                          "s.bytes              // raw byte count\n```",
                          fieldList("Size")) },
            { "Size.fromBytes", "`Size.fromBytes` : `int -> Size`\n\nCreates a Size from a raw byte count." },
            { "Size.fromKB",
              "`Size.fromKB` : `int -> Size`\n\nCreates a Size from kilobytes (n \u00d7 1024)." },
            { "Size.fromMB",
              "`Size.fromMB` : `int -> Size`\n\nCreates a Size from megabytes (n \u00d7 1024\u00b2)." },
            { "Size.fromGB",
              "`Size.fromGB` : `int -> Size`\n\nCreates a Size from gigabytes (n \u00d7 1024\u00b3)." },
            { "Size.fromTB",
              "`Size.fromTB` : `int -> Size`\n\nCreates a Size from terabytes (n \u00d7 1024\u2074)." },
            { "TimeSpan",
              std::format("`TimeSpan` \u2014 Record type for time durations\n\n"
                          "**Fields:** {}\n\n"
                          "```endo\n100ms                    // 100 milliseconds\n"
                          "5s                       // 5 seconds\n"
                          "2min                     // 2 minutes\n"
                          "1h                       // 1 hour\n"
                          "1.5h                     // 1h 30m\n"
                          "TimeSpan.fromSeconds 5   // 5s\n"
                          "t.milliseconds           // raw millisecond count\n```",
                          fieldList("TimeSpan")) },
            { "TimeSpan.fromMilliseconds",
              "`TimeSpan.fromMilliseconds` : `int -> TimeSpan`\n\nCreates a TimeSpan from "
              "milliseconds." },
            { "TimeSpan.fromSeconds",
              "`TimeSpan.fromSeconds` : `int -> TimeSpan`\n\nCreates a TimeSpan from seconds (n "
              "\u00d7 1000 ms)." },
            { "TimeSpan.fromMinutes",
              "`TimeSpan.fromMinutes` : `int -> TimeSpan`\n\nCreates a TimeSpan from minutes (n "
              "\u00d7 60000 ms)." },
            { "TimeSpan.fromHours",
              "`TimeSpan.fromHours` : `int -> TimeSpan`\n\nCreates a TimeSpan from hours (n \u00d7 "
              "3600000 ms)." },
            { "TimeSpan.fromDays",
              "`TimeSpan.fromDays` : `int -> TimeSpan`\n\nCreates a TimeSpan from days (n \u00d7 "
              "86400000 ms)." },
            { "FileMode",
              std::format(
                  "`FileMode` \u2014 Record type for Unix file permissions\n\n"
                  "**Fields:** {}\n\n"
                  "**Properties:** `isReadable`, `isWritable`, `isExecutable`, `owner`, `group`, `other`\n\n"
                  "```endo\nlet m = FileMode.fromBits 0o755\n"
                  "print m              // rwxr-xr-x\n"
                  "print m.isExecutable // true\n"
                  "print m.owner        // 7\n```",
                  fieldList("FileMode")) },
            { "FileMode.fromBits",
              "`FileMode.fromBits` : `int -> FileMode`\n\nCreates a FileMode from raw Unix permission "
              "bits." },
            { "DateTime",
              std::format("`DateTime` \u2014 Record type for date/time values (UTC)\n\n"
                          "**Fields:** {}\n\n"
                          "```endo\nDateTime.now         // current UTC time\n"
                          "DateTime.fromEpoch n // DateTime from Unix epoch\n"
                          "d.year               // access individual fields\n```",
                          fieldList("DateTime")) },
            { "DateTime.now", "`DateTime.now` : `DateTime`\n\nReturns the current UTC date and time." },
            { "DateTime.fromEpoch",
              "`DateTime.fromEpoch` : `int -> DateTime`\n\nConverts a Unix epoch timestamp to a "
              "DateTime record." },
            { "FileInfo",
              std::format("`FileInfo` \u2014 Record type for file/directory information\n\n"
                          "**Fields:** {}\n\n"
                          "Returned by `ls`. Supports dot access and pattern matching.\n\n"
                          "```endo\nls |> filter (_.size.bytes > 1024) |> map _.name\n```",
                          fieldList("FileInfo")) },
            { "ProcessInfo",
              std::format("`ProcessInfo` \u2014 Record type for process information\n\n"
                          "**Fields:** {}\n\n"
                          "Returned by `ps`. Supports dot access and pattern matching.\n\n"
                          "```endo\nps |> filter (_.cpu > 5.0) |> sortBy _.cpu\n```",
                          fieldList("ProcessInfo")) },
            { "JobInfo",
              std::format("`JobInfo` \u2014 Record type for background job information\n\n"
                          "**Fields:** {}\n\n"
                          "Returned by `jobs`. Supports dot access and pattern matching.\n\n"
                          "```endo\njobs |> filter (_.state == \"Running\")\n```",
                          fieldList("JobInfo")) },
            { "ls",
              std::format("`ls` : `list<FileInfo>` | `ls path` : `list<FileInfo>`\n\n"
                          "Lists directory contents as structured FileInfo records.\n\n"
                          "**Fields:** {}",
                          fieldList("FileInfo")) },
            { "ps",
              std::format("`ps` : `list<ProcessInfo>`\n\n"
                          "Lists running processes as structured ProcessInfo records.\n\n"
                          "**Fields:** {}",
                          fieldList("ProcessInfo")) },
            { "jobs",
              std::format("`jobs` : `list<JobInfo>`\n\n"
                          "Lists background jobs as structured JobInfo records.\n\n"
                          "**Fields:** {}",
                          fieldList("JobInfo")) },
            { "fetch",
              "`fetch` : `str -> result<str, str>`\n\n"
              "Fetches content from a URL. Returns `Ok body` on success, `Error message` on failure." },
            { "which",
              "`which` : `str -> option<str>`\n\n"
              "Searches `$PATH` for a program. Returns `Some path` if found, `None` otherwise." },
            { "rand",
              "`rand` : `int` | `rand min max` : `int`\n\n"
              "Returns a random positive integer, or a random integer in `[min, max]`." },
            { "formatDateTime",
              "`formatDateTime` : `int -> str`\n\n"
              "Formats a Unix epoch timestamp as `YYYY-MM-DD HH:MM:SS`." },
            { "formatTimeSpan",
              "`formatTimeSpan` : `TimeSpan -> str`\n\n"
              "Formats a TimeSpan as a human-readable duration string (e.g., `1m 30s`)." },
            { "sleep",
              "`sleep` : `TimeSpan -> unit`\n\n"
              "Pauses execution for the given TimeSpan duration.\n\n"
              "**Shell:** `sleep NUMBER[SUFFIX]...` (s/m/h/d)\n"
              "**F#:** `sleep (TimeSpan.fromSeconds 5)` or `TimeSpan.fromSeconds 5 |> sleep`" },
            { "formatMode",
              "`formatMode` : `int | FileMode -> str`\n\n"
              "Formats a Unix file mode as a `rwxrwxrwx` permission string.\n\n"
              "Accepts either raw permission bits (int) or a FileMode object." },
            { "toText",
              "`toText` : `'a -> str`\n\n"
              "Converts any value to its string representation." },
            { "isReadable",
              "`isReadable` : `int | FileMode -> bool`\n\n"
              "Tests if any read permission bit is set in a Unix file mode.\n\n"
              "Accepts either raw permission bits (int) or a FileMode object." },
            { "isWritable",
              "`isWritable` : `int | FileMode -> bool`\n\n"
              "Tests if any write permission bit is set in a Unix file mode.\n\n"
              "Accepts either raw permission bits (int) or a FileMode object." },
            { "isExecutable",
              "`isExecutable` : `int | FileMode -> bool`\n\n"
              "Tests if any execute permission bit is set in a Unix file mode.\n\n"
              "Accepts either raw permission bits (int) or a FileMode object." },
            { "formatNumber",
              "`formatNumber` : `str -> int -> str` | `int -> str`\n\n"
              "Formats an integer with thousand separators. In the 1-arg form, uses the locale separator." },
            { "string",
              "`string` : `'a -> str`\n\n"
              "Universal conversion to string. Works with integers, floats, booleans, and strings." },
            { "not",
              "`not` : `bool -> bool`\n\n"
              "Boolean negation." },
        };

        if (auto const it = builtins.find(name); it != builtins.end())
            return it->second;
        return propertyHover(name);
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
            {
                if (param.typeAnnotation)
                    result += " (" + param.name + ": " + endo::toString(*param.typeAnnotation) + ")";
                else
                    result += " " + param.name;
            }
            if (e->returnType)
                result += " : " + endo::toString(*e->returnType);
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
        if (auto const* e = dynamic_cast<ast::RecordExpr const*>(&expr))
        {
            std::string result = "{ ";
            for (size_t i = 0; i < e->fields.size(); ++i)
            {
                if (i > 0)
                    result += "; ";
                result += e->fields[i].name + " = ";
                if (auto val = exprToString(*e->fields[i].value))
                    result += *val;
                else
                    result += "...";
            }
            result += " }";
            return result;
        }
        return std::nullopt;
    }

    /// Formats hover markdown for a let binding signature (variable or function).
    /// @param name The binding name
    /// @param isExported Whether the binding is exported as an environment variable
    /// @param isMutable Whether the binding is mutable
    /// @param isRecursive Whether the binding is recursive
    /// @param parameters Function parameters (empty for simple bindings)
    /// @param returnType Optional return type annotation
    /// @param valuePreview Optional preview of the value expression source text
    /// @param detectedType Optional detected type name (e.g., record type from value)
    /// @param typeDefinition Optional type definition source text for supplementary info
    /// @return Markdown hover string
    [[nodiscard]] std::string formatLetBinding(std::string const& name,
                                               bool isExported,
                                               bool isMutable,
                                               bool isRecursive,
                                               std::vector<ast::TypedParameter> const& parameters,
                                               std::optional<TypePtr> const& returnType,
                                               std::optional<std::string> const& valuePreview = {},
                                               std::optional<std::string> const& detectedType = {},
                                               std::optional<std::string> const& typeDefinition = {})
    {
        std::string result;

        if (!parameters.empty())
        {
            result = "`" + name + "` \u2014 function\n\n```endo\nlet ";
            if (isExported)
                result += "export ";
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
            // Determine the display type: explicit returnType takes precedence, then detectedType
            std::optional<std::string> displayType;
            if (returnType)
                displayType = toString(*returnType);
            else if (detectedType)
                displayType = detectedType;

            result = "`" + name + "` \u2014 ";
            if (isExported)
                result += "exported ";
            if (isMutable)
                result += "mutable ";
            result += "binding";
            if (displayType)
                result += " : `" + *displayType + "`";
            result += "\n\n```endo\nlet ";
            if (isExported)
                result += "export ";
            if (isMutable)
                result += "mut ";
            result += name;
            if (displayType)
                result += ": " + *displayType;
            if (valuePreview && !valuePreview->empty())
                result += " = " + *valuePreview;
            result += "\n```";

            // Append type definition if available
            if (typeDefinition)
                result += "\n\n```endo\n" + *typeDefinition + "\n```";
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

    /// Formats a record type definition as a source string for hover preview.
    /// @param recordDef The record type definition statement
    /// @return Source text like "type Person = { name: str; age: int }"
    [[nodiscard]] std::string formatRecordTypeDef(ast::RecordTypeDefStmt const& recordDef)
    {
        std::string result = "type " + recordDef.name + " = { ";
        for (size_t i = 0; i < recordDef.fields.size(); ++i)
        {
            if (i > 0)
                result += "; ";
            result += recordDef.fields[i].name + ": " + toString(recordDef.fields[i].type);
        }
        result += " }";
        return result;
    }

    /// Returns hover markdown for a field access expression (e.g., `age` in `alice.age`).
    ///
    /// Detects that the identifier at the cursor is preceded by a `.` token and an object
    /// identifier. Resolves the object's record type through its let-binding and looks up
    /// the field definition in the corresponding RecordTypeDefStmt.
    ///
    /// @param source The full document text
    /// @param fieldName The field identifier name at cursor
    /// @param tokens The token stream from lexing
    /// @param currentIndex Index of the current identifier token in the stream
    /// @return Hover markdown if field access was detected and resolved, otherwise std::nullopt
    [[nodiscard]] std::optional<std::string> fieldAccessHover(std::string const& source,
                                                              std::string const& fieldName,
                                                              std::vector<TokenInfo> const& tokens,
                                                              size_t currentIndex)
    {
        // Need at least obj.field (3 tokens: identifier, dot, identifier)
        if (currentIndex < 2)
            return std::nullopt;

        // Check preceding tokens: Identifier Dot <current>
        if (tokens[currentIndex - 1].token != Token::Dot)
            return std::nullopt;
        if (tokens[currentIndex - 2].token != Token::Identifier)
            return std::nullopt;

        auto const& objectName = tokens[currentIndex - 2].literal;

        // Parse AST to find record type definitions and let bindings
        CoreVM::Runtime runtime;
        registerStubRuntime(runtime);
        CoreVM::diagnostics::BufferedReport report;
        Parser parser(runtime, report, std::make_unique<StringSource>(source));
        auto astRoot = parser.parse();
        if (!astRoot)
            return std::nullopt;

        std::vector<ast::LetBindingStmt const*> bindings;
        std::unordered_map<std::string, ast::RecordTypeDefStmt const*> recordTypeDefs;
        if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(astRoot.get()))
        {
            for (auto const& stmt: compound->statements)
            {
                if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(stmt.get()))
                    bindings.push_back(letStmt);
                else if (auto const* recordDef = dynamic_cast<ast::RecordTypeDefStmt const*>(stmt.get()))
                    recordTypeDefs[recordDef->name] = recordDef;
            }
        }
        else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(astRoot.get()))
        {
            bindings.push_back(letStmt);
        }

        // Find the let binding for the object
        std::string recordTypeName;
        for (auto const* letStmt: bindings)
        {
            if (letStmt->name != objectName)
                continue;

            // Determine record type from the binding's value expression
            if (letStmt->value)
            {
                if (auto const* recordExpr = dynamic_cast<ast::RecordExpr const*>(letStmt->value.get()))
                {
                    if (!recordExpr->typeName.empty())
                        recordTypeName = recordExpr->typeName;
                }
                else if (auto const* updateExpr =
                             dynamic_cast<ast::RecordUpdateExpr const*>(letStmt->value.get()))
                {
                    // For record update, resolve the base to get the type
                    if (auto const* baseIdent =
                            dynamic_cast<ast::IdentifierExpr const*>(updateExpr->base.get()))
                    {
                        // Look up the base variable's record type
                        for (auto const* baseBinding: bindings)
                        {
                            if (baseBinding->name != baseIdent->name)
                                continue;
                            if (auto const* baseRecord =
                                    dynamic_cast<ast::RecordExpr const*>(baseBinding->value.get()))
                            {
                                if (!baseRecord->typeName.empty())
                                    recordTypeName = baseRecord->typeName;
                            }
                            break;
                        }
                    }
                }
            }

            // Fall back to explicit type annotation
            if (recordTypeName.empty() && letStmt->returnType)
            {
                auto const typeStr = toString(*letStmt->returnType);
                if (recordTypeDefs.contains(typeStr))
                    recordTypeName = typeStr;
            }
            break;
        }

        if (recordTypeName.empty())
            return std::nullopt;

        // Look up the record type definition
        auto const it = recordTypeDefs.find(recordTypeName);
        if (it == recordTypeDefs.end())
            return std::nullopt;

        // Find the field in the record type
        for (auto const& field: it->second->fields)
        {
            if (field.name == fieldName)
            {
                std::string result;
                result += '`';
                result += fieldName;
                result += "` \u2014 field of `";
                result += recordTypeName;
                result += "` : `";
                result += toString(field.type);
                result += "`\n\n```endo\n";
                result += formatRecordTypeDef(*it->second);
                result += "\n```";
                return result;
            }
        }

        return std::nullopt;
    }

    /// Returns hover markdown for a user-defined binding or function parameter.
    ///
    /// Parses the source into an AST and searches top-level `let` bindings and their
    /// parameters for a matching name. For record bindings, detects the record type name
    /// and includes the type definition.
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

        // Collect top-level LetBindingStmt and RecordTypeDefStmt nodes
        std::vector<ast::LetBindingStmt const*> bindings;
        std::unordered_map<std::string, ast::RecordTypeDefStmt const*> recordTypeDefs;
        if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(astRoot.get()))
        {
            for (auto const& stmt: compound->statements)
            {
                if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(stmt.get()))
                    bindings.push_back(letStmt);
                else if (auto const* recordDef = dynamic_cast<ast::RecordTypeDefStmt const*>(stmt.get()))
                    recordTypeDefs[recordDef->name] = recordDef;
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
                auto detectedType = std::optional<std::string> {};
                auto typeDefinition = std::optional<std::string> {};

                if (!letStmt->isFunction() && letStmt->value)
                {
                    valuePreview = exprToString(*letStmt->value);

                    // Detect record type from RecordExpr value (only if no explicit returnType)
                    if (!letStmt->returnType)
                    {
                        if (auto const* recordExpr =
                                dynamic_cast<ast::RecordExpr const*>(letStmt->value.get()))
                        {
                            if (!recordExpr->typeName.empty())
                            {
                                detectedType = recordExpr->typeName;
                                // Look up the type definition for supplementary info
                                if (auto const it = recordTypeDefs.find(recordExpr->typeName);
                                    it != recordTypeDefs.end())
                                    typeDefinition = formatRecordTypeDef(*it->second);
                            }
                        }
                    }
                    else
                    {
                        // Explicit annotation — check if it's a known record type for the definition
                        auto const typeStr = toString(*letStmt->returnType);
                        if (auto const it = recordTypeDefs.find(typeStr); it != recordTypeDefs.end())
                            typeDefinition = formatRecordTypeDef(*it->second);
                    }
                }

                return formatLetBinding(name,
                                        letStmt->visibility == ast::Visibility::Exported,
                                        letStmt->mutability == ast::Mutability::Mutable,
                                        letStmt->isRecursive(),
                                        letStmt->parameters,
                                        letStmt->returnType,
                                        valuePreview,
                                        detectedType,
                                        typeDefinition);
            }

            for (auto const& andBinding: letStmt->andBindings)
            {
                if (andBinding.name == name)
                    return formatLetBinding(
                        name, false, false, true, andBinding.parameters, andBinding.returnType);
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

std::optional<HoverInfo> computeHover(std::string const& source, SourcePosition position)
{
    // Tokenize with F# mode for proper operator recognition
    auto lexer = Lexer { std::make_unique<StringSource>(source) };
    lexer.enterFSharpExpr();

    std::vector<TokenInfo> tokens;
    while (lexer.currentToken() != Token::EndOfInput)
    {
        tokens.emplace_back(TokenInfo { .token = lexer.currentToken(),
                                        .literal = lexer.currentLiteral(),
                                        .location = lexer.currentRange() });
        lexer.nextToken();
    }

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        auto const& tokenInfo = tokens[i];

        if (tokenInfo.token == Token::EndOfInput)
            continue;

        if (!containsPosition(tokenInfo.location, position))
            continue;

        auto const range = toSourceRange(tokenInfo.location);

        // F# interpolated string hover
        if (tokenInfo.token == Token::FStringStart)
            return HoverInfo { .markdownText =
                                   "`$\"...\"`  \u2014 F#-style interpolated string. Embed expressions with "
                                   "`{expr}`.\n\n```endo\n$\"Hello, {name}!\"\n$\"Sum is {3 + 4}\"\n```",
                               .range = range };

        // Try keyword hover
        if (auto text = keywordHover(tokenInfo.token))
            return HoverInfo { .markdownText = std::move(*text), .range = range };

        // Try constructor hover
        if (auto text = constructorHover(tokenInfo.token))
            return HoverInfo { .markdownText = std::move(*text), .range = range };

        // Try operator hover
        if (auto text = operatorHover(tokenInfo.token))
            return HoverInfo { .markdownText = std::move(*text), .range = range };

        // Boolean literal hover
        if (tokenInfo.token == Token::True)
            return HoverInfo { .markdownText = "`true` : `bool`\n\nBoolean true value.", .range = range };
        if (tokenInfo.token == Token::False)
            return HoverInfo { .markdownText = "`false` : `bool`\n\nBoolean false value.", .range = range };

        // Try builtin hover for identifiers
        if (tokenInfo.token == Token::Identifier)
        {
            if (auto text = builtinHover(tokenInfo.literal))
                return HoverInfo { .markdownText = std::move(*text), .range = range };

            // Try field access hover (identifier preceded by a dot)
            if (auto text = fieldAccessHover(source, tokenInfo.literal, tokens, i))
                return HoverInfo { .markdownText = std::move(*text), .range = range };

            // Try user-defined binding hover (requires AST parsing)
            if (auto text = bindingHover(source, tokenInfo.literal))
                return HoverInfo { .markdownText = std::move(*text), .range = range };
        }

        // No hover info for this token
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace endo
