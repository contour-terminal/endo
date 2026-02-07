// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/Visitor.hpp>

#include <CoreVM/CoreVM.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Lexer.hpp"

namespace endo::ast
{

struct Visitor;

/// Base class for all AST nodes with optional source location tracking.
struct Node
{
    std::optional<SourceLocationRange> location; ///< Source location of this node

    virtual ~Node() = default;

    virtual void accept(Visitor&) const = 0;
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
struct LiteralExpr final: Expr
{
    std::string value;

    explicit LiteralExpr(std::string value): value(std::move(value)) {}

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

struct BuiltinTrueStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;

    explicit BuiltinTrueStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback):
        callback { callback }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinFalseStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;

    explicit BuiltinFalseStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback):
        callback { callback }
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
    std::string program;                                          ///< Program name or path
    std::vector<std::unique_ptr<Expr>> parameters;                ///< Command arguments
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

// if command; then a; else b; fi
//
// This is an if statement.
// It is a condition, followed by a then block, followed by an optional else block.
struct IfStmt final: public Statement
{
    std::unique_ptr<Statement> condition;
    std::unique_ptr<Statement> thenBlock;
    std::unique_ptr<Statement> elseBlock;

    IfStmt(std::unique_ptr<Statement> condition,
           std::unique_ptr<Statement> thenBlock,
           std::unique_ptr<Statement> elseBlock):
        condition(std::move(condition)), thenBlock(std::move(thenBlock)), elseBlock(std::move(elseBlock))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// while command; do a; done
//
// This is a while statement.
// It is a condition, followed by a body.
struct WhileStmt final: public Statement
{
    std::unique_ptr<Statement> condition;
    std::unique_ptr<Statement> body;

    WhileStmt(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> body):
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

/// For-list statement: `for var in list; do ...; done`
///
/// Iterates over a list of items, assigning each to the loop variable.
struct ForListStmt final: public Statement
{
    std::string variable;                     ///< Loop variable name
    std::vector<std::unique_ptr<Expr>> items; ///< Items to iterate
    std::unique_ptr<Statement> body;          ///< Loop body

    ForListStmt(std::string variable,
                std::vector<std::unique_ptr<Expr>> items,
                std::unique_ptr<Statement> body):
        variable(std::move(variable)), items(std::move(items)), body(std::move(body))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// C-style for statement: `for ((init; cond; step)); do ...; done`
///
/// Traditional C-style for loop with arithmetic expressions.
struct ForCStyleStmt final: public Statement
{
    std::unique_ptr<ArithExpr> init;      ///< Initialization expression (may be null)
    std::unique_ptr<ArithExpr> condition; ///< Loop condition (may be null for infinite loop)
    std::unique_ptr<ArithExpr> step;      ///< Step/increment expression (may be null)
    std::unique_ptr<Statement> body;      ///< Loop body

    ForCStyleStmt(std::unique_ptr<ArithExpr> init,
                  std::unique_ptr<ArithExpr> condition,
                  std::unique_ptr<ArithExpr> step,
                  std::unique_ptr<Statement> body):
        init(std::move(init)), condition(std::move(condition)), step(std::move(step)), body(std::move(body))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Case clause for pattern matching
struct CaseClause
{
    std::vector<std::string> patterns; ///< Pipe-separated patterns
    std::unique_ptr<Statement> body;   ///< Commands to execute on match
};

/// Case statement: `case word in pattern) ...; esac`
///
/// Pattern matching construct similar to switch in other languages.
struct CaseStmt final: public Statement
{
    std::unique_ptr<Expr> word;      ///< Word to match against patterns
    std::vector<CaseClause> clauses; ///< Pattern-body pairs

    CaseStmt(std::unique_ptr<Expr> word, std::vector<CaseClause> clauses):
        word(std::move(word)), clauses(std::move(clauses))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

/// Function definition: `function name() { ... }` or `name() { ... }`
///
/// Defines a shell function that can be called later.
struct FunctionDefStmt final: public Statement
{
    std::string name;                ///< Function name
    std::unique_ptr<Statement> body; ///< Function body

    FunctionDefStmt(std::string name, std::unique_ptr<Statement> body):
        name(std::move(name)), body(std::move(body))
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

/// Return statement: `return [n]`
///
/// Returns from a function with the specified exit code (default: $?).
struct ReturnStmt final: public Statement
{
    std::unique_ptr<Expr> value; ///< Optional return value (defaults to $?)

    explicit ReturnStmt(std::unique_ptr<Expr> value = nullptr): value(std::move(value)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

} // namespace endo::ast
