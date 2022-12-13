// SPDX-License-Identifier: Apache-2.0
#include <shell/AST.h>
#include <shell/ASTPrinter.h>
#include <shell/IRGenerator.h>
#include <shell/ScopedLogger.h>

#include <CoreVM/ir/ConstantArray.h>

#include <typeinfo>

// {{{ trace macros
// clang-format off
#if 0 // defined(TRACE_PARSER)
    #define TRACE_SCOPE(message) ScopedLogger _logger { message }
    #define TRACE(message, ...) do { ScopedLogger::write(::fmt::format(message, __VA_ARGS__)); } while (0)
#else
    #define TRACE_SCOPE(message) do {} while (0)
    #define TRACE(message, ...) do {} while (0)
#endif
// clang-format on
// }}}

namespace crush
{

#define GLOBAL_SCOPE_INIT_NAME "@main"

CoreVM::IRProgram* IRGenerator::generate(ast::Statement const& rootNode)
{
    IRGenerator generator;

    generator.setProgram(std::make_unique<CoreVM::IRProgram>());
    generator.setHandler(generator.getHandler(GLOBAL_SCOPE_INIT_NAME));
    generator.setInsertPoint(generator.createBlock("EntryPoint"));
    generator.codegen(&rootNode);
    generator.createRet(generator.get(CoreVM::CoreNumber(0)));

    return generator.program();
}

IRGenerator::IRGenerator()
{
    _processCallSignature.setReturnType(CoreVM::LiteralType::Number);
    _processCallSignature.setName("ProcessCall");
}

CoreVM::Value* IRGenerator::codegen(ast::Node const* node)
{
    TRACE_SCOPE(fmt::format("codegen({})", node ? typeid(*node).name() : "nullptr"));
    _result = nullptr;
    if (node)
        node->accept(*this);
    return _result;
}

void IRGenerator::visit(ast::FileDescriptor const& node)
{
    _result = get(CoreVM::CoreNumber { node.value });
}

void IRGenerator::visit(ast::InputRedirect const&)
{
    // TODO
}

void IRGenerator::visit(ast::OutputRedirect const&)
{
    // TODO
}

void IRGenerator::visit(ast::BuiltinExitStmt const& node)
{
    CoreVM::Value* exitCode = nullptr;
    if (!node.code)
        exitCode = get(CoreVM::CoreNumber(0));
    else
    {
        exitCode = codegen(node.code.get());
        if (exitCode->type() == CoreVM::LiteralType::String)
            exitCode = createS2N(exitCode);
        else if (exitCode->type() != CoreVM::LiteralType::Number)
            throw std::runtime_error("exit code must be a number");
    }
    _result = createCallFunction(getBuiltinFunction(node.callback.get()), { exitCode }, "exit");
}

void IRGenerator::visit(ast::BuiltinTrueStmt const&)
{
    _result = get(CoreVM::CoreNumber(0));
}

void IRGenerator::visit(ast::BuiltinChDirStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (node.path)
        callArguments.push_back(codegen(node.path.get()));

    _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "chdir");
}

void IRGenerator::visit(ast::BuiltinFalseStmt const&)
{
    _result = get(CoreVM::CoreNumber(1));
}

void IRGenerator::visit(ast::ProgramCall const& node)
{
    TRACE_SCOPE("ProgramCall");
    auto callParametersArg = std::vector<CoreVM::Constant*>();
    TRACE("ProgramCall: {}", ast::ASTPrinter::print(node));
    callParametersArg.push_back(get(node.program));
    for (auto const& arg: node.parameters)
    {
        TRACE_SCOPE(fmt::format("Parameter: ", ast::ASTPrinter::print(*arg)));
        auto* value = codegen(arg.get());
        if (auto* constant = dynamic_cast<CoreVM::Constant*>(value); constant != nullptr)
            callParametersArg.push_back(constant);
        else
            fmt::print("Warning: non-constant argument passed to program call\n");
    }

    // auto redirectsArg = std::vector<CoreVM::Constant*> {};
    // redirectsArg.push_back(get(STDOUT_FILENO));
    // redirectsArg.push_back(get(STDERR_FILENO));
    // for (auto const& redirect: node.outputRedirects)
    // {
    //     auto* value = codegen(redirect.target.get());
    //     if (auto* constant = dynamic_cast<CoreVM::Constant*>(value); constant != nullptr)
    //         redirectsArg.push_back(constant);
    //     else
    //         fmt::print("Warning: non-constant argument passed to program call\n");
    // }

    auto callArguments = std::vector<CoreVM::Value*> {};
    callArguments.push_back(get(callParametersArg));
    // callArguments.push_back(get(redirectsArg));

    _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "callProcess");
}

void IRGenerator::visit(ast::CallPipeline const&)
{
    // TODO: Create pipe for each connection, then spawn processes and connect them to the pipes in order
}

void IRGenerator::visit(ast::CompoundStmt const& node)
{
    for (auto const& stmt: node.statements)
        codegen(stmt.get());

    _result = nullptr;
}

CoreVM::Value* IRGenerator::toBool(CoreVM::Value* value)
{
    return createNCmpEQ(value, get(CoreVM::CoreNumber(0)));
}

void IRGenerator::visit(ast::IfStmt const& node)
{
    CoreVM::BasicBlock* cond = createBlock("if.cond");
    CoreVM::BasicBlock* trueBlock = createBlock("if.trueBlock");
    CoreVM::BasicBlock* falseBlock = createBlock("if.falseBlock");
    CoreVM::BasicBlock* end = createBlock("if.end");

    createBr(cond);
    setInsertPoint(cond);
    createCondBr(toBool(codegen(node.condition.get())), trueBlock, falseBlock);

    setInsertPoint(trueBlock);
    codegen(node.thenBlock.get());
    createBr(end);

    setInsertPoint(falseBlock);
    codegen(node.elseBlock.get());
    createBr(end);

    setInsertPoint(end);
}

void IRGenerator::visit(ast::WhileStmt const& node)
{
    CoreVM::BasicBlock* cond = createBlock("while.cond");
    CoreVM::BasicBlock* body = createBlock("while.body");
    CoreVM::BasicBlock* end = createBlock("while.end");

    createBr(cond);

    setInsertPoint(cond);
    createCondBr(toBool(codegen(node.condition.get())), body, end);

    setInsertPoint(body);
    codegen(node.body.get());
    createBr(cond);

    setInsertPoint(end);
}

void IRGenerator::visit(ast::LiteralExpr const& node)
{
    _result = get(node.value);
}

void IRGenerator::visit(ast::SubstitutionExpr const&)
{
    // TODO
}

void IRGenerator::visit(ast::CommandFileSubst const&)
{
    // TODO
}

} // namespace crush
