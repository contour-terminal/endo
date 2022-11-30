#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <bits/utility.h>

namespace contrair::ast
{

struct Visitor;

struct Node
{
    virtual ~Node() = default;
    virtual void accept(Visitor const&) = 0;
};

struct FileDescriptor: public Node
{
    int value;

    void accept(Visitor const&) override;
};

// <FILE
struct InputRedirect: public Node
{
    void accept(Visitor const&) override;
};

struct OutputRedirect: public Node
{
    std::unique_ptr<FileDescriptor> source;
    std::variant<FileDescriptor, std::filesystem::path> target;

    void accept(Visitor const&) override;
};

// /bin/ls -hal
struct ProgramCall: public Node
{
    std::vector<std::string> argv;
    std::vector<OutputRedirect> redirects;

    void accept(Visitor const&) override;
};

// a | b | (c | d) | e
struct CallPipeline: public Node
{
    std::vector<ProgramCall> calls;

    void accept(Visitor const&) override;
};

// <(command)
struct CommandFileSubst: public Node
{
    std::unique_ptr<Node> command;

    void accept(Visitor const&) override;
};

struct CompoundStmt: public Node
{
    std::vector<CallPipeline> pipelines;

    void accept(Visitor const&) override;
};

struct IfStmt: public Node
{
    std::unique_ptr<CallPipeline> condition;
    std::unique_ptr<CompoundStmt> thenBlock;
    std::unique_ptr<CompoundStmt> elseBlock;

    void accept(Visitor const&) override;
};

}
