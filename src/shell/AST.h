// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/Visitor.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import CoreVM;
import Lexer;

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

// <(command)
//
// This is a bashism, but it's useful for endo.
// It's a way to pass the output of a command as a file (e.g. to a program that expects a file).
// It is the path to the file descriptor of the command's output, which is a pipe.
struct CommandFileSubst final: public Expr
{
    std::unique_ptr<Node> command;

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
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

// $(command)
// `command`
//
// This is a substitution parameter.
// It is a parameter, because it can be used as an argument to a program call.
struct SubstitutionExpr final: public Expr
{
    std::unique_ptr<Statement> pipeline;

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// a | b | (c | d) | e
//
// This is a call pipeline.
// It is a sequence of program calls, separated by pipes.
struct CallPipeline final: public Statement
{
    std::vector<std::unique_ptr<ProgramCall>> calls;

    CallPipeline(std::vector<std::unique_ptr<ProgramCall>> calls): calls(std::move(calls)) {}

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

} // namespace endo::ast
