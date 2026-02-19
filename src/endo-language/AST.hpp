// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Lexer.hpp"
#include "Pattern.hpp"
#include "Type.hpp"
#include "Visitor.hpp"

namespace endo::ast
{

struct Visitor;

/// Base class for all AST nodes with optional source location tracking.
struct Node
{
    std::optional<SourceLocationRange> location; ///< Source location of this node

    virtual ~Node() = default;

    virtual void accept(Visitor&) const = 0;

  protected:
    /// Computes a source location range spanning from the left to the right child node.
    void setSpanFromChildren(Node const& left, Node const& right)
    {
        if (left.location && right.location)
            location = SourceLocationRange { left.location->begin, right.location->end };
        else if (left.location)
            location = left.location;
        else if (right.location)
            location = right.location;
    }
};

struct FileDescriptor final: public Node
{
    int value;

    explicit FileDescriptor(int value): value(value) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct Expr: public Node
{
};

/// Input redirect: `< FILE` or `N< FILE`
///
/// Redirects input to a command from a file.
struct InputRedirect final: public Node
{
    std::unique_ptr<FileDescriptor> targetFd; ///< Target fd (default: 0 = stdin)
    std::unique_ptr<Expr> source;             ///< Source file path expression

    InputRedirect(std::unique_ptr<FileDescriptor> targetFd, std::unique_ptr<Expr> source):
        targetFd(std::move(targetFd)), source(std::move(source))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// "string that may contain spaces"
// 'string that may contain spaces'
// string
//
// This is a literal parameter.
// It is a string.
// It may be quoted.
/// Quoting style for literal expressions (for round-trip AST printing).
enum class LiteralQuoting : uint8_t
{
    Unquoted, ///< Bare word (shell context)
    Quoted,   ///< Quoted string literal (e.g., 'hello' or "hello")
};

struct LiteralExpr final: Expr
{
    std::string value;
    LiteralQuoting quoting = LiteralQuoting::Unquoted;

    explicit LiteralExpr(std::string value, LiteralQuoting quoting = LiteralQuoting::Unquoted):
        value(std::move(value)), quoting(quoting)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Represents a variable expression for runtime variable substitution.
///
/// This covers:
/// - Simple variables: $VAR
/// - Braced variables: ${VAR}
/// - Special variables: $?, $$, $!, $0-$9
enum class VariableType
{
    Named,        ///< Regular named variable: $VAR or ${VAR}
    ExitStatus,   ///< $? - Exit status of last command
    ProcessId,    ///< $$ - Current shell process ID
    BackgroundId, ///< $! - PID of last background process
    Positional,   ///< $0-$9 - Positional parameters
};

struct VariableExpr final: Expr
{
    std::string name;    ///< Variable name (empty for special variables like $?)
    VariableType type;   ///< Type of variable
    bool braced = false; ///< Whether this was ${VAR} syntax

    VariableExpr(std::string name, VariableType type, bool braced = false):
        name(std::move(name)), type(type), braced(braced)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Tilde expansion: ~, ~user, ~/path, or ~user/path
///
/// Expands to home directory:
/// - ~ expands to current user's home directory ($HOME)
/// - ~/path expands to $HOME/path
/// - ~user expands to user's home directory (from passwd database)
/// - ~user/path expands to user's home directory + path
struct TildeExpr final: Expr
{
    std::string user;   ///< Empty for ~ (current user), username for ~user
    std::string suffix; ///< Path suffix after tilde (e.g., "/Documents" for ~/Documents)

    explicit TildeExpr(std::string user = "", std::string suffix = ""):
        user(std::move(user)), suffix(std::move(suffix))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Parameter expansion operations
enum class ParamExpansionOp
{
    Length,            ///< ${#VAR} - string length
    DefaultValue,      ///< ${VAR:-default} - use default if unset or empty
    AlternateValue,    ///< ${VAR:+alt} - use alt if set and non-empty
    AssignDefault,     ///< ${VAR:=default} - assign default if unset or empty
    ErrorIfUnset,      ///< ${VAR:?error} - error if unset or empty
    RemovePrefixShort, ///< ${VAR#pattern} - remove shortest prefix match
    RemovePrefixLong,  ///< ${VAR##pattern} - remove longest prefix match
    RemoveSuffixShort, ///< ${VAR%pattern} - remove shortest suffix match
    RemoveSuffixLong,  ///< ${VAR%%pattern} - remove longest suffix match
    ReplaceFirst,      ///< ${VAR/pattern/replacement} - replace first match
    ReplaceAll,        ///< ${VAR//pattern/replacement} - replace all matches
};

/// Parameter expansion: ${VAR:-default}, ${#VAR}, ${VAR/old/new}, etc.
///
/// Provides shell parameter expansion features:
/// - Default values: ${VAR:-default}, ${VAR:+alt}, ${VAR:=default}, ${VAR:?error}
/// - String length: ${#VAR}
/// - Pattern removal: ${VAR#pattern}, ${VAR##pattern}, ${VAR%pattern}, ${VAR%%pattern}
/// - Substitution: ${VAR/pattern/replacement}, ${VAR//pattern/replacement}
struct ParamExpansionExpr final: Expr
{
    std::string variable; ///< Variable name
    ParamExpansionOp op;  ///< Operation type
    std::string operand1; ///< Pattern or default value
    std::string operand2; ///< Replacement (for replace operations only)

    ParamExpansionExpr(std::string var,
                       ParamExpansionOp operation,
                       std::string op1 = "",
                       std::string op2 = ""):
        variable(std::move(var)), op(operation), operand1(std::move(op1)), operand2(std::move(op2))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Glob expression: `*.txt`, `dir/*`, `[a-z].log`
///
/// Represents a pathname pattern that will be expanded at runtime
/// to match files in the filesystem. Standard glob metacharacters:
/// - `*` matches any sequence of characters
/// - `?` matches any single character
/// - `[...]` matches any character in the set
struct GlobExpr final: Expr
{
    std::string pattern; ///< The glob pattern (e.g., "*.txt")

    explicit GlobExpr(std::string p): pattern(std::move(p)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Concatenation expression for interpolated strings.
///
/// Represents a sequence of expressions that should be concatenated at runtime.
/// Used for double-quoted strings with variable interpolation, e.g.:
/// - `"hello $USER"` → ConcatExpr([LiteralExpr("hello "), VariableExpr("USER")])
/// - `"$(date): $MSG"` → ConcatExpr([SubstitutionExpr(...), LiteralExpr(": "), VariableExpr("MSG")])
struct ConcatExpr final: Expr
{
    std::vector<std::unique_ptr<Expr>> parts; ///< Parts to concatenate

    explicit ConcatExpr(std::vector<std::unique_ptr<Expr>> p): parts(std::move(p)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// F#-style interpolated string expression: `$"text {expr} text"`
///
/// Represents a sequence of literal text and embedded expressions that are
/// converted to strings and concatenated at runtime.
/// Parts are alternating literal and expression nodes.
struct FStringExpr final: Expr
{
    std::vector<std::unique_ptr<Expr>> parts; ///< Alternating literal and expression parts

    explicit FStringExpr(std::vector<std::unique_ptr<Expr>> p): parts(std::move(p)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// ============================================================================
// Arithmetic Expansion $((expr))
// ============================================================================

/// Arithmetic operators for $((expr)) expansion
enum class ArithOp
{
    Add,    // +
    Sub,    // -
    Mul,    // *
    Div,    // /
    Mod,    // %
    Pow,    // **
    Lt,     // <
    Gt,     // >
    Le,     // <=
    Ge,     // >=
    Eq,     // ==
    Ne,     // !=
    And,    // &&
    Or,     // ||
    BitAnd, // &
    BitOr,  // |
    BitXor, // ^
    Shl,    // <<
    Shr,    // >>
    Not,    // ! (unary logical)
    Neg,    // - (unary negation)
    BitNot, // ~ (unary bitwise)
};

/// Base for arithmetic expressions within $((expr))
struct ArithExpr
{
    virtual ~ArithExpr() = default;
};

/// Arithmetic literal: a numeric constant
struct ArithLiteralExpr final: ArithExpr
{
    int64_t value;

    explicit ArithLiteralExpr(int64_t v): value(v) {}
};

/// Arithmetic variable reference
struct ArithVarExpr final: ArithExpr
{
    std::string name;

    explicit ArithVarExpr(std::string n): name(std::move(n)) {}
};

/// Arithmetic binary expression: left op right
struct ArithBinaryExpr final: ArithExpr
{
    ArithOp op;
    std::unique_ptr<ArithExpr> left;
    std::unique_ptr<ArithExpr> right;

    ArithBinaryExpr(ArithOp operation, std::unique_ptr<ArithExpr> l, std::unique_ptr<ArithExpr> r):
        op(operation), left(std::move(l)), right(std::move(r))
    {
    }
};

/// Arithmetic unary expression: op operand
struct ArithUnaryExpr final: ArithExpr
{
    ArithOp op;
    std::unique_ptr<ArithExpr> operand;

    ArithUnaryExpr(ArithOp operation, std::unique_ptr<ArithExpr> e): op(operation), operand(std::move(e)) {}
};

/// Arithmetic expansion: $((expr))
///
/// Evaluates the arithmetic expression and expands to the result as a string.
struct ArithExpansionExpr final: Expr
{
    std::unique_ptr<ArithExpr> expression;

    explicit ArithExpansionExpr(std::unique_ptr<ArithExpr> e): expression(std::move(e)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Output redirect: `> FILE`, `>> FILE`, `N> FILE`, `N>&M`
///
/// Redirects output from a file descriptor to a file or another file descriptor.
/// - If target is a FileDescriptor, performs fd duplication (e.g., `2>&1`)
/// - If target is an Expr, redirects to file (e.g., `> file.txt`)
/// - If append is true, appends to file (e.g., `>> file.txt`)
struct OutputRedirect final: public Expr
{
    std::unique_ptr<FileDescriptor> source; ///< Source fd (default: 1 = stdout)
    std::variant<std::unique_ptr<FileDescriptor>, std::unique_ptr<Expr>> target; ///< Target fd or file path
    bool append = false; ///< True for >> (append mode)

    /// Constructor for fd duplication: `N>&M`
    OutputRedirect(std::unique_ptr<FileDescriptor> source, std::unique_ptr<FileDescriptor> target):
        source(std::move(source)), target(std::move(target)), append(false)
    {
    }

    /// Constructor for file redirect: `> FILE` or `>> FILE`
    OutputRedirect(std::unique_ptr<FileDescriptor> source, std::unique_ptr<Expr> target, bool append = false):
        source(std::move(source)), target(std::move(target)), append(append)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Mode for process substitution.
enum class ProcessSubstMode
{
    Read,  ///< <(command) - read from command output
    Write, ///< >(command) - write to command input
};

/// Here-document: `<<EOF` or `<<-EOF`
///
/// Provides multi-line input to a command. The content is read until the delimiter
/// is found on a line by itself.
/// - If stripTabs is true (<<-), leading tabs are stripped from each line.
struct HereDocument final: public Node
{
    std::unique_ptr<FileDescriptor> targetFd; ///< Target fd (default: 0 = stdin)
    std::string delimiter;                    ///< End-of-document delimiter
    std::string content;                      ///< Here-document content
    bool stripTabs = false;                   ///< True for <<- (strip leading tabs)

    HereDocument(std::unique_ptr<FileDescriptor> targetFd,
                 std::string delimiter,
                 std::string content,
                 bool stripTabs = false):
        targetFd(std::move(targetFd)),
        delimiter(std::move(delimiter)),
        content(std::move(content)),
        stripTabs(stripTabs)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Here-string: `<<< "string"`
///
/// Provides a single string as input to a command.
struct HereString final: public Node
{
    std::unique_ptr<FileDescriptor> targetFd; ///< Target fd (default: 0 = stdin)
    std::unique_ptr<Expr> content;            ///< Content expression (may contain variables)

    HereString(std::unique_ptr<FileDescriptor> targetFd, std::unique_ptr<Expr> content):
        targetFd(std::move(targetFd)), content(std::move(content))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct Statement: public Node
{
};

struct BuiltinExitStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> code;

    BuiltinExitStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                    std::unique_ptr<Expr> code):
        callback { callback }, code { std::move(code) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinExportStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::string name;

    BuiltinExportStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback, std::string name):
        callback { callback }, name { std::move(name) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinReadStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::vector<std::unique_ptr<Expr>> parameters;

    BuiltinReadStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                    std::vector<std::unique_ptr<Expr>> parameters = {}):
        callback { callback }, parameters { std::move(parameters) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinSetStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> name;
    std::unique_ptr<Expr> value;

    BuiltinSetStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                   std::unique_ptr<Expr> name,
                   std::unique_ptr<Expr> value):
        callback { callback }, name { std::move(name) }, value { std::move(value) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinChDirStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> path;

    BuiltinChDirStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                     std::unique_ptr<Expr> path):
        callback { callback }, path { std::move(path) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Builtin unset statement for removing variables from the environment.
struct BuiltinUnsetStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::string name;

    BuiltinUnsetStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback, std::string name):
        callback { callback }, name { std::move(name) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Builtin jobs statement for listing background jobs.
struct BuiltinJobsStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;

    explicit BuiltinJobsStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback):
        callback { callback }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Builtin fg statement for bringing a job to foreground.
struct BuiltinFgStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> jobId; ///< Optional job ID (null for current job)

    BuiltinFgStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                  std::unique_ptr<Expr> jobId = nullptr):
        callback { callback }, jobId { std::move(jobId) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Builtin bg statement for resuming a stopped job in the background.
struct BuiltinBgStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> jobId; ///< Optional job ID (null for current job)

    BuiltinBgStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                  std::unique_ptr<Expr> jobId = nullptr):
        callback { callback }, jobId { std::move(jobId) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Builtin wait statement for waiting on background jobs.
struct BuiltinWaitStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> jobId; ///< Optional job ID (null for all jobs)

    BuiltinWaitStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                    std::unique_ptr<Expr> jobId = nullptr):
        callback { callback }, jobId { std::move(jobId) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Builtin bind statement for managing keybindings.
struct BuiltinBindStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::vector<std::unique_ptr<Expr>> args; ///< Arguments (key, action, flags)

    BuiltinBindStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                    std::vector<std::unique_ptr<Expr>> args = {}):
        callback { callback }, args { std::move(args) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Builtin which statement for finding executables in PATH.
struct BuiltinWhichStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::vector<std::unique_ptr<Expr>> args; ///< Program names and flags

    BuiltinWhichStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                     std::vector<std::unique_ptr<Expr>> args = {}):
        callback { callback }, args { std::move(args) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Program call: `/bin/ls -hal`
///
/// Represents a program call with its arguments and redirects.
/// It is a path to an executable, followed by arguments, with optional redirects.
struct ProgramCall final: public Statement
{
    std::string program;                                ///< Program name or path (raw display string)
    std::unique_ptr<Expr> programExpr;                  ///< Runtime program name expression (tilde expansion)
    std::optional<SourceLocationRange> programLocation; ///< Source location of the program name token
    std::vector<std::unique_ptr<Expr>> parameters;      ///< Command arguments
    std::vector<std::unique_ptr<InputRedirect>> inputRedirects;   ///< Input redirects (< FILE)
    std::vector<std::unique_ptr<OutputRedirect>> outputRedirects; ///< Output redirects (> FILE, >> FILE, >&)
    std::vector<std::unique_ptr<HereDocument>> hereDocuments;     ///< Here-documents (<<EOF)
    std::vector<std::unique_ptr<HereString>> hereStrings;         ///< Here-strings (<<<)
    std::reference_wrapper<CoreVM::NativeCallback const> callback;

    ProgramCall(CoreVM::NativeCallback const& callback,
                std::string program,
                std::vector<std::unique_ptr<Expr>> parameters,
                std::vector<std::unique_ptr<InputRedirect>> inputRedirects,
                std::vector<std::unique_ptr<OutputRedirect>> outputRedirects,
                std::vector<std::unique_ptr<HereDocument>> hereDocuments,
                std::vector<std::unique_ptr<HereString>> hereStrings):
        program(std::move(program)),
        parameters(std::move(parameters)),
        inputRedirects(std::move(inputRedirects)),
        outputRedirects(std::move(outputRedirects)),
        hereDocuments(std::move(hereDocuments)),
        hereStrings(std::move(hereStrings)),
        callback(callback)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Command substitution: `$(command)` or `` `command` ``
///
/// This is a substitution parameter.
/// It is a parameter, because it can be used as an argument to a program call.
/// The command's stdout is captured and the result (with trailing newlines stripped)
/// is substituted in place.
struct SubstitutionExpr final: public Expr
{
    std::unique_ptr<Statement> pipeline; ///< The command/pipeline to execute
    bool backtick = false;               ///< True if using backtick syntax

    explicit SubstitutionExpr(std::unique_ptr<Statement> p, bool bt = false):
        pipeline(std::move(p)), backtick(bt)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Shell command whose output feeds a structured F# pipeline.
///
/// Created when a shell command is followed by the |> operator.
/// The IRGenerator checks output definitions and either generates a
/// structured command callback or falls back to command substitution.
struct StructuredPipelineSourceExpr final: public Expr
{
    std::unique_ptr<Statement> command; ///< The shell command to execute

    explicit StructuredPipelineSourceExpr(std::unique_ptr<Statement> cmd): command(std::move(cmd)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Field definition with optional default value for data source type annotation.
struct DataSourceFieldDef
{
    std::string name;                   ///< Field name
    TypePtr type;                       ///< Field type annotation
    std::unique_ptr<Expr> defaultValue; ///< Optional default (literal) for missing fields
};

/// Data source expression: open-json/open-csv/from-json/from-csv with 'as' type annotation.
///
/// Parses arbitrary JSON/CSV data into typed records for pipeline use.
/// Examples:
///   open-json "users.json" as { name: string; age: int }
///   curl api/users | from-json as { name: string; age: int } |> filter (_.age > 25)
struct DataSourceExpr final: public Expr
{
    enum class Kind
    {
        OpenJson, ///< Read JSON from file
        OpenCsv,  ///< Read CSV from file
        FromJson, ///< Parse JSON from piped input
        FromCsv,  ///< Parse CSV from piped input
    };

    Kind kind;
    std::unique_ptr<Expr> filePath;        ///< For open-*: file path expression
    std::unique_ptr<Statement> pipeSource; ///< For from-*: shell commands piped in (may be nullptr for stdin)

    // Type specification: EITHER named type OR inline fields (exactly one is populated)
    std::string typeName;                         ///< Non-empty if referencing existing type (e.g., "Person")
    std::vector<DataSourceFieldDef> inlineFields; ///< Non-empty if inline definition

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Process substitution: `<(command)` or `>(command)`
///
/// This is a bashism, but it's useful for endo.
/// It's a way to pass the output of a command as a file (e.g. to a program that expects a file).
/// It is the path to the file descriptor of the command's output, which is a pipe.
struct CommandFileSubst final: public Expr
{
    std::unique_ptr<Statement> command; ///< The command to execute
    ProcessSubstMode mode;              ///< Read or write mode

    CommandFileSubst(std::unique_ptr<Statement> cmd, ProcessSubstMode m): command(std::move(cmd)), mode(m) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// a | b | (c | d) | e
//
// This is a call pipeline.
// It is a sequence of program calls, separated by pipes.
struct CallPipeline final: public Statement
{
    std::vector<std::unique_ptr<ProgramCall>> calls;
    bool background = false; ///< True if command ends with & (run in background)

    CallPipeline(std::vector<std::unique_ptr<ProgramCall>> calls, bool bg = false):
        calls(std::move(calls)), background(bg)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// { a; b; }
//
// This is a compound statement.
// It is a sequence of statements, separated by semicolons.
struct CompoundStmt final: public Statement
{
    std::vector<std::unique_ptr<Node>> statements;

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// While loop: `while expr do body done`
///
/// Condition is an F# boolean expression.
struct WhileStmt final: public Statement
{
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Statement> body;

    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Statement> body):
        condition(std::move(condition)), body(std::move(body))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Logical AND statement: `cmd1 && cmd2`
///
/// Executes `right` only if `left` succeeds (exit code 0).
/// Implements short-circuit evaluation.
struct LogicalAndStmt final: public Statement
{
    std::unique_ptr<Statement> left;
    std::unique_ptr<Statement> right;

    LogicalAndStmt(std::unique_ptr<Statement> left, std::unique_ptr<Statement> right):
        left(std::move(left)), right(std::move(right))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Logical OR statement: `cmd1 || cmd2`
///
/// Executes `right` only if `left` fails (exit code != 0).
/// Implements short-circuit evaluation.
struct LogicalOrStmt final: public Statement
{
    std::unique_ptr<Statement> left;
    std::unique_ptr<Statement> right;

    LogicalOrStmt(std::unique_ptr<Statement> left, std::unique_ptr<Statement> right):
        left(std::move(left)), right(std::move(right))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// For-in statement with pattern destructuring: `for pattern in expr do body done`
///
/// Iterates over a typed list, destructuring each element with a pattern.
/// Examples:
/// - `for x in [1; 2; 3] do print x done`
/// - `for (name, value) in entries do print name done`
/// - `for { host; port } in servers do ping host done`
struct ForInStmt final: public Statement
{
    std::unique_ptr<pattern::Pattern> pattern; ///< Binding pattern
    std::unique_ptr<Expr> source;              ///< Source list expression
    std::unique_ptr<Statement> body;           ///< Loop body

    ForInStmt(std::unique_ptr<pattern::Pattern> pat,
              std::unique_ptr<Expr> src,
              std::unique_ptr<Statement> bodyStmt):
        pattern(std::move(pat)), source(std::move(src)), body(std::move(bodyStmt))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Break statement: `break [n]`
///
/// Exits the innermost loop or the nth enclosing loop.
struct BreakStmt final: public Statement
{
    int levels = 1; ///< Number of loop levels to break out of

    explicit BreakStmt(int levels = 1): levels(levels) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Continue statement: `continue [n]`
///
/// Continues with the next iteration of the innermost loop or nth enclosing loop.
struct ContinueStmt final: public Statement
{
    int levels = 1; ///< Number of loop levels to skip

    explicit ContinueStmt(int levels = 1): levels(levels) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// ============================================================================
// F# Style Expressions and Statements
// ============================================================================

/// A function parameter with an optional type annotation.
///
/// Used in `let`, `let-in`, `lambda`, and `and` bindings:
/// - `let add (x: int) (y: int) = x + y`
/// - `fun (x: int) -> x + 1`
/// - Bare identifiers: `let add x y = x + y` (no annotation)
struct TypedParameter
{
    std::string name;                      ///< Parameter name
    std::optional<TypePtr> typeAnnotation; ///< Optional type annotation
    bool isVariadic = false;               ///< True for variadic parameter: `...args`
    bool isUnit = false;                   ///< True for unit parameter: `()`

    explicit TypedParameter(std::string n): name(std::move(n)) {}

    TypedParameter(std::string n, TypePtr t): name(std::move(n)), typeAnnotation(std::move(t)) {}

    TypedParameter(std::string n, bool variadic): name(std::move(n)), isVariadic(variadic) {}

    /// Creates a unit parameter representing `()`.
    static TypedParameter unitParam()
    {
        TypedParameter p("_");
        p.typeAnnotation = types::unitType();
        p.isUnit = true;
        return p;
    }
};

/// Extracts parameter names from a vector of TypedParameter.
inline std::vector<std::string> extractParameterNames(std::vector<TypedParameter> const& params)
{
    std::vector<std::string> names;
    names.reserve(params.size());
    for (auto const& p: params)
        names.push_back(p.name);
    return names;
}

/// F# style let binding: `let x = 42` or `let add x y = x + y`
///
/// Represents both simple bindings and function definitions:
/// - Simple: `let x = 42` → parameters empty, value is the expression
/// - Function: `let add x y = x + y` → parameters are [x, y], value is body
///
/// Note: Accessed via identifier name directly (not $x).
/// Environment variables ($VAR) are a separate namespace.
/// Represents a single function binding in a `let rec ... and ...` group.
struct AndBinding
{
    std::string name;                       ///< Function name
    std::vector<TypedParameter> parameters; ///< Function parameters with optional type annotations
    std::optional<TypePtr> returnType;      ///< Optional return type annotation
    std::unique_ptr<Expr> value;            ///< Function body
};

/// Property accessor definition for F# property syntax.
///
/// Used in `let Name with get () = ... and set (v) = ...` declarations.
struct PropertyAccessor
{
    std::string paramName;             ///< Setter parameter name (empty for getter)
    std::optional<TypePtr> paramType;  ///< Optional parameter type annotation
    std::optional<TypePtr> returnType; ///< Optional return type annotation
    std::unique_ptr<Expr> body;        ///< Accessor body expression
};

struct LetBindingStmt final: public Statement
{
    bool isExported;                        ///< True for `let export`
    bool isMutable;                         ///< True for `let mut`
    bool isRecursive;                       ///< True for `let rec`
    std::string name;                       ///< Binding/function name
    std::vector<TypedParameter> parameters; ///< Function parameters with optional type annotations
    std::optional<TypePtr> returnType;      ///< Return type (functions) or binding type (simple bindings)
    std::unique_ptr<Expr> value;            ///< Value expression or function body
    std::vector<AndBinding> andBindings;    ///< Additional mutually recursive bindings (`and` keyword)
    std::unique_ptr<pattern::Pattern> destructurePattern; ///< Optional pattern for `let (x, y) = expr`
    std::unique_ptr<PropertyAccessor> getter;             ///< `with get () = expr`
    std::unique_ptr<PropertyAccessor> setter;             ///< `and set (value) = expr`

    LetBindingStmt(bool exported,
                   bool mut,
                   bool rec,
                   std::string n,
                   std::vector<TypedParameter> params,
                   std::optional<TypePtr> retType,
                   std::unique_ptr<Expr> val):
        isExported(exported),
        isMutable(mut),
        isRecursive(rec),
        name(std::move(n)),
        parameters(std::move(params)),
        returnType(std::move(retType)),
        value(std::move(val))
    {
    }

    /// Constructor for destructuring let bindings: `let (x, y) = expr`
    LetBindingStmt(bool mut, std::unique_ptr<pattern::Pattern> pat, std::unique_ptr<Expr> val):
        isExported(false),
        isMutable(mut),
        isRecursive(false),
        parameters(),
        returnType(std::nullopt),
        value(std::move(val)),
        destructurePattern(std::move(pat))
    {
    }

    /// Is this a function definition (has parameters)?
    [[nodiscard]] bool isFunction() const noexcept { return !parameters.empty(); }

    /// Is this a destructuring binding?
    [[nodiscard]] bool isDestructuring() const noexcept { return destructurePattern != nullptr; }

    /// Is this a property definition (has get/set accessors)?
    [[nodiscard]] bool isProperty() const noexcept { return getter != nullptr || setter != nullptr; }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Expression statement: wraps an expression to be used as a statement.
///
/// Used for F# expressions that appear at statement level, such as:
/// - `print "hello"` (function application with side effects)
/// - `println msg` (print with newline)
struct ExprStmt final: public Statement
{
    std::unique_ptr<Expr> expr;
    bool displayResult = false; ///< When true, auto-display the result (for bare expressions at shell prompt)

    explicit ExprStmt(std::unique_ptr<Expr> e, bool display = false):
        expr(std::move(e)), displayResult(display)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Let-in expression: `let x = 5 in x + 10`
///
/// Scoped binding that introduces a variable (or function) visible only within
/// the body expression following `in`.
struct LetInExpr final: public Expr
{
    bool isRecursive;                       ///< True for `let rec`
    std::string name;                       ///< Binding/function name
    std::vector<TypedParameter> parameters; ///< Function parameters with optional type annotations
    std::optional<TypePtr> returnType;      ///< Optional return type annotation
    std::unique_ptr<Expr> value;            ///< Value expression or function body
    std::unique_ptr<Expr> body;             ///< Body expression evaluated with the binding in scope
    std::unique_ptr<pattern::Pattern>
        destructurePattern; ///< Optional pattern for `let (x, y) = expr in body`

    LetInExpr(bool rec,
              std::string n,
              std::vector<TypedParameter> params,
              std::optional<TypePtr> retType,
              std::unique_ptr<Expr> val,
              std::unique_ptr<Expr> b):
        isRecursive(rec),
        name(std::move(n)),
        parameters(std::move(params)),
        returnType(std::move(retType)),
        value(std::move(val)),
        body(std::move(b))
    {
    }

    /// Constructor for destructuring let-in: `let (x, y) = expr in body`
    LetInExpr(std::unique_ptr<pattern::Pattern> pat, std::unique_ptr<Expr> val, std::unique_ptr<Expr> b):
        isRecursive(false),
        parameters(),
        returnType(std::nullopt),
        value(std::move(val)),
        body(std::move(b)),
        destructurePattern(std::move(pat))
    {
    }

    /// Is this a function definition (has parameters)?
    [[nodiscard]] bool isFunction() const noexcept { return !parameters.empty(); }

    /// Is this a destructuring binding?
    [[nodiscard]] bool isDestructuring() const noexcept { return destructurePattern != nullptr; }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// If-then-else expression: `if cond then e1 else e2` or `if cond then e1` (returns unit).
///
/// F# style conditional expression. When `elseExpr` is null, the expression returns unit.
struct IfExpr final: public Expr
{
    std::unique_ptr<Expr> condition; ///< Boolean condition
    std::unique_ptr<Expr> thenExpr;  ///< Expression when condition is true
    std::unique_ptr<Expr> elseExpr;  ///< Expression when condition is false (null = unit)

    IfExpr(std::unique_ptr<Expr> cond, std::unique_ptr<Expr> thenE, std::unique_ptr<Expr> elseE):
        condition(std::move(cond)), thenExpr(std::move(thenE)), elseExpr(std::move(elseE))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Tuple expression: `(a, b)` or `(a, b, c)`
///
/// Represents a fixed-size tuple with 2 or more elements.
struct TupleExpr final: public Expr
{
    std::vector<std::unique_ptr<Expr>> elements; ///< Tuple elements (2+)

    explicit TupleExpr(std::vector<std::unique_ptr<Expr>> elems): elements(std::move(elems)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Mutable variable assignment: `name <- value`
///
/// Assigns a new value to a previously declared `let mut` binding.
struct MutAssignStmt final: public Statement
{
    std::string name;            ///< Variable name
    std::unique_ptr<Expr> value; ///< New value expression

    MutAssignStmt(std::string n, std::unique_ptr<Expr> val): name(std::move(n)), value(std::move(val)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Mutable variable assignment expression: `name <- value`
///
/// Same semantics as MutAssignStmt but usable in expression context (e.g., as the then-body
/// of `if cond then name <- value`). Returns unit, like in F#.
struct MutAssignExpr final: public Expr
{
    std::string name;            ///< Variable name
    std::unique_ptr<Expr> value; ///< New value expression

    MutAssignExpr(std::string n, std::unique_ptr<Expr> val): name(std::move(n)), value(std::move(val)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Binary operators for F# style expressions
enum class BinaryOp
{
    // Arithmetic
    Add, // +
    Sub, // -
    Mul, // *
    Div, // /
    Mod, // %
    Pow, // **

    // Comparison
    Eq, // ==
    Ne, // !=
    Lt, // <
    Le, // <=
    Gt, // >
    Ge, // >=

    // Logical
    And, // &&
    Or,  // ||
};

/// Unary operators for F# style expressions
enum class UnaryOp
{
    Neg, // -x
    Not, // !x
};

/// Binary expression: `left op right`
///
/// Used for arithmetic, comparison, and logical operations.
struct BinaryExpr final: public Expr
{
    BinaryOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;

    BinaryExpr(BinaryOp operation, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r):
        op(operation), left(std::move(l)), right(std::move(r))
    {
        setSpanFromChildren(*left, *right);
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Unary expression: `op operand`
///
/// Used for negation and logical not.
struct UnaryExpr final: public Expr
{
    UnaryOp op;
    std::unique_ptr<Expr> operand;

    UnaryExpr(UnaryOp operation, std::unique_ptr<Expr> e): op(operation), operand(std::move(e)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Pipeline expression: `value |> func`
///
/// Syntactic sugar for function application: `value |> f` is equivalent to `f value`.
/// Multiple pipelines chain: `x |> f |> g` is `g (f x)`.
struct PipelineExpr final: public Expr
{
    std::unique_ptr<Expr> value;    ///< Left-hand side (the value being piped)
    std::unique_ptr<Expr> function; ///< Right-hand side (function to apply)

    PipelineExpr(std::unique_ptr<Expr> val, std::unique_ptr<Expr> func):
        value(std::move(val)), function(std::move(func))
    {
        setSpanFromChildren(*value, *function);
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Function application expression: `func arg1 arg2 ...`
///
/// Represents curried function application. `add 1 2` is parsed as
/// ApplicationExpr(ApplicationExpr(add, 1), 2).
struct ApplicationExpr final: public Expr
{
    std::unique_ptr<Expr> function; ///< Function being applied
    std::unique_ptr<Expr> argument; ///< Argument being passed

    ApplicationExpr(std::unique_ptr<Expr> func, std::unique_ptr<Expr> arg):
        function(std::move(func)), argument(std::move(arg))
    {
        setSpanFromChildren(*function, *argument);
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Identifier expression: reference to a variable in F# namespace
///
/// Note: This is distinct from VariableExpr which handles $VAR shell variables.
/// IdentifierExpr accesses let-bound variables directly by name.
struct IdentifierExpr final: public Expr
{
    std::string name;

    explicit IdentifierExpr(std::string n): name(std::move(n)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Integer literal expression: `42`, `-17`, `0xFF`
struct IntLiteralExpr final: public Expr
{
    int64_t value;

    explicit IntLiteralExpr(int64_t v): value(v) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Float literal expression: `3.14`, `-0.5`, `1e10`
struct FloatLiteralExpr final: public Expr
{
    double value;

    explicit FloatLiteralExpr(double v): value(v) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Boolean literal expression: `true`, `false`
struct BoolLiteralExpr final: public Expr
{
    bool value;

    explicit BoolLiteralExpr(bool v): value(v) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Break expression: `break` inside F# expressions (e.g., in if-then-else within loops)
struct BreakExpr final: public Expr
{
    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Continue expression: `continue` inside F# expressions (e.g., in if-then-else within loops)
struct ContinueExpr final: public Expr
{
    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Parenthesized expression: `(expr)`
///
/// Used to override operator precedence or for clarity.
struct ParenExpr final: public Expr
{
    std::unique_ptr<Expr> inner;

    explicit ParenExpr(std::unique_ptr<Expr> e): inner(std::move(e)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Lambda expression: `fun x -> x * 2` or `fun x y -> x + y`
///
/// Anonymous function with one or more parameters.
/// Supports curried parameters: `fun x y -> x + y` is sugar for `fun x -> fun y -> x + y`.
struct LambdaExpr final: public Expr
{
    std::vector<TypedParameter> parameters; ///< Parameters with optional type annotations
    std::unique_ptr<Expr> body;             ///< Lambda body expression

    LambdaExpr(std::vector<TypedParameter> params, std::unique_ptr<Expr> bodyExpr):
        parameters(std::move(params)), body(std::move(bodyExpr))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// A single arm in a match expression: `| pattern when guard -> body`
///
/// Examples:
/// - `| 0 -> "zero"`
/// - `| x when x < 0 -> "negative"`
/// - `| Some n -> n`
struct MatchArm
{
    std::unique_ptr<pattern::Pattern> pattern; ///< Pattern to match against
    std::unique_ptr<Expr> guard;               ///< Optional guard expression (nullptr if no guard)
    std::unique_ptr<Expr> body;                ///< Body expression to evaluate if matched

    MatchArm(std::unique_ptr<pattern::Pattern> pat,
             std::unique_ptr<Expr> guardExpr,
             std::unique_ptr<Expr> bodyExpr):
        pattern(std::move(pat)), guard(std::move(guardExpr)), body(std::move(bodyExpr))
    {
    }
};

/// Match expression: `match expr with | pattern -> body | ...`
///
/// F#-style pattern matching expression.
///
/// Examples:
/// ```
/// match x with
/// | 0 -> "zero"
/// | 1 -> "one"
/// | n when n < 0 -> "negative"
/// | _ -> "positive"
/// ```
struct MatchExpr final: public Expr
{
    std::unique_ptr<Expr> scrutinee; ///< Expression being matched against
    std::vector<MatchArm> arms;      ///< Match arms (pattern -> body pairs)

    MatchExpr(std::unique_ptr<Expr> scrut, std::vector<MatchArm> matchArms):
        scrutinee(std::move(scrut)), arms(std::move(matchArms))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// List literal expression: `[1; 2; 3]`
///
/// Homogeneous list with semicolon-separated elements.
/// An empty list `[]` is represented with an empty elements vector.
struct ListExpr final: public Expr
{
    std::vector<std::unique_ptr<Expr>> elements; ///< List elements

    explicit ListExpr(std::vector<std::unique_ptr<Expr>> elems): elements(std::move(elems)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Cons expression: `head :: tail`
///
/// Prepends an element to a list, creating a new Cons cell.
/// Right-associative: `1 :: 2 :: []` = `1 :: (2 :: [])`.
struct ConsExpr final: public Expr
{
    std::unique_ptr<Expr> head; ///< Element to prepend
    std::unique_ptr<Expr> tail; ///< Existing list

    ConsExpr(std::unique_ptr<Expr> h, std::unique_ptr<Expr> t): head(std::move(h)), tail(std::move(t))
    {
        setSpanFromChildren(*head, *tail);
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// List concatenation expression: `left @ right`
///
/// Concatenates two lists, producing a new list.
struct ConcatListExpr final: public Expr
{
    std::unique_ptr<Expr> left;  ///< First list
    std::unique_ptr<Expr> right; ///< Second list

    ConcatListExpr(std::unique_ptr<Expr> l, std::unique_ptr<Expr> r): left(std::move(l)), right(std::move(r))
    {
        setSpanFromChildren(*left, *right);
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// List range expression: `[1..10]` or `[start..step..end]`
///
/// Creates a list from a range of values.
/// - `[1..10]` produces [1; 2; 3; ...; 10]
/// - `[2; 4..20]` produces [2; 4; 6; ...; 20] (step inferred from first two elements)
/// - `[10..-1..0]` produces [10; 9; 8; ...; 0] (explicit step)
struct ListRangeExpr final: public Expr
{
    std::unique_ptr<Expr> start; ///< Starting value
    std::unique_ptr<Expr> step;  ///< Optional step value (nullptr means step of 1)
    std::unique_ptr<Expr> end;   ///< Ending value (inclusive)

    ListRangeExpr(std::unique_ptr<Expr> s, std::unique_ptr<Expr> st, std::unique_ptr<Expr> e):
        start(std::move(s)), step(std::move(st)), end(std::move(e))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// List comprehension expression: `[for x in items -> expr]` or `[for x in items when cond -> expr]`
///
/// Generates a list by transforming elements from a source collection.
///
/// Examples:
/// - `[for x in 1..10 -> x * x]` - squares of 1 to 10
/// - `[for x in items when x > 5 -> x * 2]` - double items greater than 5
/// - `[for x in 1..3 -> for y in 1..3 -> (x, y)]` - cartesian product
struct ListComprehensionExpr final: public Expr
{
    std::string variable;         ///< Iteration variable name
    std::unique_ptr<Expr> source; ///< Source collection to iterate over
    std::unique_ptr<Expr> filter; ///< Optional filter (when clause), nullptr if none
    std::unique_ptr<Expr> body;   ///< Body expression to evaluate for each element

    ListComprehensionExpr(std::string var,
                          std::unique_ptr<Expr> src,
                          std::unique_ptr<Expr> filt,
                          std::unique_ptr<Expr> bodyExpr):
        variable(std::move(var)), source(std::move(src)), filter(std::move(filt)), body(std::move(bodyExpr))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Shell command expression: `& git status` or `& git diff HEAD..origin/master`
///
/// Executes a shell command in F# expression context.
/// - In expression context (let binding, pipeline): captures stdout as a string
/// - At statement level: executes the command with output going to terminal
///
/// The `&` prefix allows shell commands to be used as expressions in F# mode,
/// where special characters like `..` would otherwise be tokenized as operators.
///
/// Examples:
/// - `let output = & git status` - capture output to variable
/// - `& git diff HEAD..master |> String.trim` - capture and pipe to F# function
struct ShellCommandExpr final: public Expr
{
    std::unique_ptr<Statement> command; ///< The shell command/pipeline to execute

    explicit ShellCommandExpr(std::unique_ptr<Statement> cmd): command(std::move(cmd)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Splat expression: `...args` inside a shell command body
///
/// Expands a list variable into individual shell command arguments.
/// Used in alias-style function definitions:
/// - `let ll ...args = & exa -l ...args` — splats args into the shell command
struct SplatExpr final: public Expr
{
    std::string name; ///< Variable name to splat

    explicit SplatExpr(std::string n): name(std::move(n)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// ============================================================================
// F# Style - Error Handling Expressions
// ============================================================================

/// Option constructor expression: `Some expr` or `None`
///
/// Creates an Option value:
/// - `Some 42` creates an Option containing 42
/// - `None` creates an empty Option
///
/// Used with pattern matching and the `?` operator for error handling.
struct OptionExpr final: public Expr
{
    bool isSome;                 ///< true for Some, false for None
    std::unique_ptr<Expr> value; ///< The wrapped value (nullptr for None)

    explicit OptionExpr(bool some, std::unique_ptr<Expr> val = nullptr): isSome(some), value(std::move(val))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Result constructor expression: `Ok expr` or `Error expr`
///
/// Creates a Result value:
/// - `Ok 42` creates a successful Result containing 42
/// - `Error "failed"` creates an error Result with the message
///
/// Used with pattern matching and the `?` operator for error propagation.
struct ResultExpr final: public Expr
{
    bool isOk;                     ///< true for Ok, false for Error
    std::unique_ptr<Expr> payload; ///< The success value or error value

    ResultExpr(bool ok, std::unique_ptr<Expr> val): isOk(ok), payload(std::move(val)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Try expression (error propagation): `expr?`
///
/// Unwraps a Result or Option value:
/// - If Ok/Some: returns the inner value
/// - If Error/None: propagates the error (early return from function)
///
/// Examples:
/// - `let x = getValue()?` - unwrap or propagate error
/// - `data? |> process` - try-unwrap then process
///
/// The `?` operator can only be used inside functions that return Result/Option.
struct TryExpr final: public Expr
{
    std::unique_ptr<Expr> operand; ///< Expression returning Result/Option

    explicit TryExpr(std::unique_ptr<Expr> op): operand(std::move(op)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Option default expression: `expr ?| default`
///
/// Unwraps an Option value, returning the default if None:
/// - `Some v ?| d` evaluates to `v`
/// - `None ?| d` evaluates to `d`
struct OptionDefaultExpr final: public Expr
{
    std::unique_ptr<Expr> option;       ///< Expression returning Option
    std::unique_ptr<Expr> defaultValue; ///< Expression to evaluate if None

    OptionDefaultExpr(std::unique_ptr<Expr> opt, std::unique_ptr<Expr> def):
        option(std::move(opt)), defaultValue(std::move(def))
    {
        setSpanFromChildren(*option, *defaultValue);
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Try-with expression: `try expr with | pattern -> handler | ...`
///
/// Evaluates an expression that may fail and handles errors with pattern matching.
///
/// Examples:
/// ```
/// try
///     let data = fetchData()?
///     processData(data)
/// with
/// | { code = 404; _ } -> DefaultData.empty
/// | { code; message } when code >= 500 -> Error { code; message }
/// | e -> Error e
/// ```
struct TryWithExpr final: public Expr
{
    std::unique_ptr<Expr> body;     ///< Expression to try
    std::vector<MatchArm> handlers; ///< Error handlers (reuse MatchArm from match expressions)

    TryWithExpr(std::unique_ptr<Expr> b, std::vector<MatchArm> h): body(std::move(b)), handlers(std::move(h))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Try-finally expression: `try expr finally cleanup`
///
/// Evaluates body, then always evaluates finallyExpr for cleanup.
/// The expression result is the body's value; finallyExpr result is discarded.
/// If `?` triggers an early return inside body, finallyExpr still runs before propagation.
struct TryFinallyExpr final: public Expr
{
    std::unique_ptr<Expr> body;        ///< Expression to try
    std::unique_ptr<Expr> finallyExpr; ///< Cleanup expression (always runs, result discarded)

    TryFinallyExpr(std::unique_ptr<Expr> b, std::unique_ptr<Expr> f):
        body(std::move(b)), finallyExpr(std::move(f))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Unit expression: `()`
///
/// Represents the unit value (void/nothing). Used as a placeholder when no
/// meaningful value is needed, analogous to `void` in C++ or `unit` in F#.
struct UnitExpr final: public Expr
{
    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Block expression: `{ let x = 1; x + 2 }`
///
/// A scoped expression delimited by braces. Variables defined inside are
/// local to the block. The block's value is the last expression.
struct BlockExpr final: public Expr
{
    std::vector<std::unique_ptr<Statement>> statements; ///< Zero or more let bindings/statements
    std::unique_ptr<Expr> result;                       ///< Final expression (the block's value)

    BlockExpr(std::vector<std::unique_ptr<Statement>> stmts, std::unique_ptr<Expr> res):
        statements(std::move(stmts)), result(std::move(res))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// ============================================================================
// F# Style - Record Types
// ============================================================================

/// A single field definition in a record type: `name: type`
struct RecordFieldDef
{
    std::string name; ///< Field name
    TypePtr type;     ///< Field type annotation
};

/// Record type definition statement: `type Person = { name: str; age: int }`
///
/// Defines a named record type with typed fields.
struct RecordTypeDefStmt final: public Statement
{
    std::string name;                   ///< Type name (e.g., "Person")
    std::vector<RecordFieldDef> fields; ///< Field definitions

    RecordTypeDefStmt(std::string n, std::vector<RecordFieldDef> f): name(std::move(n)), fields(std::move(f))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// ============================================================================
// F# Style - Discriminated Unions (ADTs)
// ============================================================================

/// A single variant definition in a discriminated union type.
///
/// Each variant has a name and an optional list of payload types.
/// For example, in `type Shape = Circle of float | Rectangle of float * float | Point`,
/// `Circle` has one payload type (float), `Rectangle` has two (float * float),
/// and `Point` has none.
struct UnionVariantDef
{
    std::string name;                    ///< Variant constructor name (e.g., "Circle")
    std::vector<TypePtr> payloadTypes;   ///< Payload types (empty for unit constructors)
    std::vector<std::string> fieldNames; ///< Named fields, parallel to payloadTypes (empty string = unnamed)
};

/// Discriminated union type definition statement: `type Shape = | Circle of float | Rectangle of float *
/// float | Point`
///
/// Defines a named sum type with tagged variants, each optionally carrying payload data.
struct UnionTypeDefStmt final: public Statement
{
    std::string name;                      ///< Type name (e.g., "Shape")
    std::vector<UnionVariantDef> variants; ///< Variant definitions

    UnionTypeDefStmt(std::string n, std::vector<UnionVariantDef> v):
        name(std::move(n)), variants(std::move(v))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Union constructor expression: `Circle 5.0` or `Rectangle(10.0, 20.0)` or `Point`
///
/// Constructs a value of a discriminated union type using a specific variant constructor.
struct UnionConstructorExpr final: public Expr
{
    std::string typeName;                         ///< The union type name (e.g., "Shape")
    std::string constructorName;                  ///< The constructor name (e.g., "Circle")
    std::vector<std::unique_ptr<Expr>> arguments; ///< Constructor arguments (empty for unit constructors)

    UnionConstructorExpr(std::string tn, std::string cn, std::vector<std::unique_ptr<Expr>> args):
        typeName(std::move(tn)), constructorName(std::move(cn)), arguments(std::move(args))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// A single field initialization in a record literal: `name = expr`
struct RecordFieldInit
{
    std::string name;            ///< Field name
    std::unique_ptr<Expr> value; ///< Field value expression
};

/// Record literal expression: `{ name = "Alice"; age = 30 }`
///
/// Creates a new record value with the given field values.
struct RecordExpr final: public Expr
{
    std::string typeName;                ///< Resolved record type name (empty if anonymous)
    std::vector<RecordFieldInit> fields; ///< Field initializations

    RecordExpr(std::string tn, std::vector<RecordFieldInit> f): typeName(std::move(tn)), fields(std::move(f))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Record update expression: `{ person with age = 31 }`
///
/// Creates a new record that copies the base record but overrides specified fields.
struct RecordUpdateExpr final: public Expr
{
    std::unique_ptr<Expr> base;           ///< The record to copy
    std::vector<RecordFieldInit> updates; ///< Fields to override

    RecordUpdateExpr(std::unique_ptr<Expr> b, std::vector<RecordFieldInit> u):
        base(std::move(b)), updates(std::move(u))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Field access expression: `person.name`
///
/// Accesses a named field on a record value.
struct FieldAccessExpr final: public Expr
{
    std::unique_ptr<Expr> object; ///< The expression being accessed
    std::string fieldName;        ///< The field name

    FieldAccessExpr(std::unique_ptr<Expr> obj, std::string field):
        object(std::move(obj)), fieldName(std::move(field))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Optional chaining expression: `expr?.field`
///
/// If expr is Some(v), returns Some(v.field). If None, returns None.
/// The result is always option<T>, enabling chaining: `a?.b?.c`.
struct OptionalChainExpr final: public Expr
{
    std::unique_ptr<Expr> object; ///< The option expression
    std::string fieldName;        ///< The field to access on the inner value

    OptionalChainExpr(std::unique_ptr<Expr> obj, std::string field):
        object(std::move(obj)), fieldName(std::move(field))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Pipeline of one or more dynamically-resolved exec commands connected by OS-level pipes.
///
/// Each command consists of a program path (F# expression evaluating to a string)
/// and zero or more argument expressions.
///
/// Examples:
/// - `exec "/bin/echo" "hello"` — single command
/// - `exec f "-s" | exec l` — piped commands with dynamic program paths
/// - `exec "/bin/cat" "/etc/hostname" | exec "/bin/tr" "a-z" "A-Z"` — multi-stage pipeline
struct ExecPipelineExpr final: public Expr
{
    /// A single exec command: program path and arguments, all F# expressions.
    struct Command
    {
        std::unique_ptr<Expr> program;                ///< F# expression evaluating to the command path
        std::vector<std::unique_ptr<Expr>> arguments; ///< F# expressions evaluating to argument strings
    };

    std::vector<Command> commands; ///< Commands in the pipeline (connected by OS-level pipes)

    explicit ExecPipelineExpr(std::vector<Command> cmds): commands(std::move(cmds)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

} // namespace endo::ast
