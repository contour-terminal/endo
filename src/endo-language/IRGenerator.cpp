// SPDX-License-Identifier: Apache-2.0
#include "IRGenerator.hpp"

#include <CoreVM/CoreVM.hpp>

#include <typeinfo>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "DiagnosticsAdapter.hpp"
#include "ScopedLogger.hpp"

// {{{ trace macros
// clang-format off
#if 0 // defined(TRACE_PARSER)
    #define TRACE_SCOPE(message) ScopedLogger _logger { message }
    #define TRACE(message, ...) do { ScopedLogger::write(::std::format(message, __VA_ARGS__)); } while (0)
#else
    #define TRACE_SCOPE(message) do {} while (0)
    #define TRACE(message, ...) do {} while (0)
#endif
// clang-format on
// }}}

#define GLOBAL_SCOPE_INIT_NAME "@main"

namespace endo
{

std::unique_ptr<CoreVM::IRProgram> IRGenerator::generate(ast::Statement const& rootNode,
                                                         CoreVM::diagnostics::Report& report,
                                                         CoreVM::Runtime& runtime)
{
    IRGenerator generator(report, runtime);

    generator.setProgram(std::make_unique<CoreVM::IRProgram>());
    generator.setHandler(generator.getHandler(GLOBAL_SCOPE_INIT_NAME));
    generator.setInsertPoint(generator.createBlock("EntryPoint"));
    generator.codegen(&rootNode);

    if (generator._hasErrors)
        return nullptr;

    generator.createRet(generator.get(CoreVM::CoreNumber(0)));

    return generator.takeProgram();
}

IRGenerator::IRGenerator(CoreVM::diagnostics::Report& report, CoreVM::Runtime& runtime):
    _report { report }, _runtime { runtime }
{
    _processCallSignature.setReturnType(CoreVM::LiteralType::Number);
    _processCallSignature.setName("ProcessCall");
}

CoreVM::NativeCallback* IRGenerator::findCallback(std::string const& signature) const
{
    return _runtime.find(signature);
}

CoreVM::Value* IRGenerator::codegen(ast::Node const* node)
{
    TRACE_SCOPE(std::format("codegen({})", node ? typeid(*node).name() : "nullptr"));
    _result = nullptr;
    if (node)
    {
        // Track current location for error reporting
        if (node->location.has_value())
            _currentLocation = toCoreLoc(node->location.value());
        node->accept(*this);
    }
    return _result;
}

template <typename... Args>
void IRGenerator::reportTypeError(std::format_string<Args...> f, Args&&... args)
{
    _report.typeError(_currentLocation, f, std::forward<Args>(args)...);
    _hasErrors = true;
}

void IRGenerator::visit(ast::BuiltinExitStmt const& node)
{
    CoreVM::Value* exitCode = nullptr;
    if (!node.code)
        exitCode = get(CoreVM::CoreNumber(0));
    else
    {
        exitCode = codegen(node.code.get());
        if (!exitCode)
            return; // Error already reported
        if (exitCode->type() == CoreVM::LiteralType::String)
            exitCode = createS2N(exitCode);
        else if (exitCode->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("exit code must be a number, got {}", exitCode->type());
            return;
        }
    }
    _result = createCallFunction(getBuiltinFunction(node.callback.get()), { exitCode }, "exit");
}

void IRGenerator::visit(ast::BuiltinExportStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    callArguments.push_back(get(node.name));
    _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "export");
}

void IRGenerator::visit(ast::BuiltinChDirStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (node.path)
        callArguments.push_back(codegen(node.path.get()));

    _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "chdir");
}

void IRGenerator::visit(ast::BuiltinSetStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (node.name && node.value)
    {

        callArguments.push_back(codegen(node.name.get()));
        callArguments.push_back(codegen(node.value.get()));
    }

    _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "set");
}

void IRGenerator::visit(ast::BuiltinFalseStmt const& node)
{
    _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "false");
}

void IRGenerator::visit(ast::BuiltinReadStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (!node.parameters.empty())
        callArguments.emplace_back(get(createCallArgs(node.parameters)));

    _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "read");
}

void IRGenerator::visit(ast::BuiltinTrueStmt const& node)
{
    _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "true");
}

void IRGenerator::visit(ast::CallPipeline const& node)
{
    // A | B | C | D
    //
    // process      | stdin             |   stdout
    // -------------------------------------------------------
    // A            | STDIN             |   pipe 1 (write end)
    // B            | pipe 1 (read end) |   pipe 2 (write end)
    // C            | pipe 2 (read end) |   pipe 3 (write end)
    // D            | pipe 3 (read end) |   STDOUT

    for (size_t i = 0; i < node.calls.size(); ++i)
    {
        std::unique_ptr<ast::ProgramCall> const& call = node.calls[i];
        bool const lastInChain = i == node.calls.size() - 1;

        bool const hasRedirects = !call->inputRedirects.empty() || !call->outputRedirects.empty()
                                  || !call->hereDocuments.empty() || !call->hereStrings.empty();

        // Start redirect context if we have any redirects
        if (hasRedirects)
        {
            auto* startCallback = findCallback("internal.redirect_start()V");
            if (startCallback)
                createCallFunction(getBuiltinFunction(*startCallback), {}, "redirect_start");
        }

        // Generate code for all redirects
        for (auto const& redirect: call->inputRedirects)
            codegen(redirect.get());

        for (auto const& redirect: call->outputRedirects)
            codegen(redirect.get());

        for (auto const& heredoc: call->hereDocuments)
            codegen(heredoc.get());

        for (auto const& herestring: call->hereStrings)
            codegen(herestring.get());

        if (containsRuntimeExpr(call->parameters))
        {
            // Use dynamic argument building and execution
            buildCommandArgs(call->program, call->parameters);

            // Check if this is the last command in a background pipeline
            if (lastInChain && node.background)
            {
                _result = execBuiltCommandPipedBackground(call->program, call->parameters);
            }
            else
            {
                _result = execBuiltCommandPiped(lastInChain);
            }
        }
        else
        {
            // Check if this is the last command in a background pipeline
            if (lastInChain && node.background)
            {
                // Build command args first for background execution
                buildCommandArgs(call->program, call->parameters);
                _result = execBuiltCommandPipedBackground(call->program, call->parameters);
            }
            else
            {
                // Use constant array (fast path)
                std::vector<CoreVM::Value*> callArguments {};
                callArguments.push_back(get(lastInChain));
                callArguments.push_back(get(createCallArgs(call->program, call->parameters)));
                _result = createCallFunction(
                    getBuiltinFunction(call->callback.get()), callArguments, "callProcess");
            }
        }

        // End redirect context
        if (hasRedirects)
        {
            auto* endCallback = findCallback("internal.redirect_end()V");
            if (endCallback)
                createCallFunction(getBuiltinFunction(*endCallback), {}, "redirect_end");
        }
    }
}

void IRGenerator::visit(ast::CommandFileSubst const& node)
{
    // Process substitution: <(command) or >(command)
    // This requires forking: child runs the command, parent gets the fd path
    bool const isWrite = (node.mode == ast::ProcessSubstMode::Write);

    // Fork for process substitution
    // Returns 0 in child process, fd number (> 0) in parent process
    auto* forkCb = findCallback("internal.procsubst_fork(B)I");
    if (!forkCb)
    {
        reportTypeError("Internal error: internal.procsubst_fork builtin not found");
        return;
    }

    auto* forkResult = createCallFunction(getBuiltinFunction(*forkCb), { get(isWrite) }, "procsubst_fork");

    // Check if we're the child (result == 0)
    auto* isChild = createNCmpEQ(forkResult, get(CoreVM::CoreNumber(0)));

    CoreVM::BasicBlock* childBlock = createBlock("procsubst.child");
    CoreVM::BasicBlock* contBlock = createBlock("procsubst.cont");

    createCondBr(isChild, childBlock, contBlock);

    // Child block: execute command, then exit
    setInsertPoint(childBlock);
    codegen(node.command.get());

    // Child exits after running command
    auto* exitCb = findCallback("internal.procsubst_exit()V");
    if (exitCb)
        createCallFunction(getBuiltinFunction(*exitCb), {}, "procsubst_exit");
    createBr(contBlock); // Unreachable due to exit, but needed for valid IR

    // Continue block: parent gets the fd path
    setInsertPoint(contBlock);
    auto* pathCb = findCallback("internal.procsubst_get_path()S");
    if (!pathCb)
    {
        reportTypeError("Internal error: internal.procsubst_get_path builtin not found");
        return;
    }
    _result = createCallFunction(getBuiltinFunction(*pathCb), {}, "procsubst_get_path");
}

void IRGenerator::visit(ast::CompoundStmt const& node)
{
    for (auto const& stmt: node.statements)
    {
        codegen(stmt.get());
        // Stop generating code after a terminator (break, continue, return, etc.)
        if (getInsertPoint() && getInsertPoint()->getTerminator() != nullptr)
            break;
    }

    _result = nullptr;
}

void IRGenerator::visit(ast::FileDescriptor const& node)
{
    _result = get(CoreVM::CoreNumber { node.value });
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

void IRGenerator::visit(ast::LogicalAndStmt const& node)
{
    // Short-circuit AND: execute right only if left succeeds (exit code 0)
    // A && B:
    //   eval A
    //   if A succeeded (exit code == 0): eval B, result = B's exit code
    //   else: result = A's exit code
    CoreVM::BasicBlock* evalRight = createBlock("and.evalRight");
    CoreVM::BasicBlock* end = createBlock("and.end");

    // Evaluate left side
    auto* leftResult = codegen(node.left.get());

    // If left succeeded (exit code == 0), evaluate right side
    createCondBr(toBool(leftResult), evalRight, end);

    // Evaluate right side
    setInsertPoint(evalRight);
    codegen(node.right.get());
    createBr(end);

    setInsertPoint(end);
    // Note: The exit code is automatically set by the last executed command
}

void IRGenerator::visit(ast::LogicalOrStmt const& node)
{
    // Short-circuit OR: execute right only if left fails (exit code != 0)
    // A || B:
    //   eval A
    //   if A failed (exit code != 0): eval B, result = B's exit code
    //   else: result = A's exit code (which is 0, success)
    CoreVM::BasicBlock* evalRight = createBlock("or.evalRight");
    CoreVM::BasicBlock* end = createBlock("or.end");

    // Evaluate left side
    auto* leftResult = codegen(node.left.get());

    // If left failed (exit code != 0), evaluate right side
    // toBool returns true for exit code 0 (success), so we flip the branches
    createCondBr(toBool(leftResult), end, evalRight);

    // Evaluate right side
    setInsertPoint(evalRight);
    codegen(node.right.get());
    createBr(end);

    setInsertPoint(end);
    // Note: The exit code is automatically set by the last executed command
}

void IRGenerator::visit(ast::InputRedirect const& node)
{
    auto* callback = findCallback("internal.redirect_input(IS)V");
    if (!callback)
    {
        reportTypeError("Internal error: internal.redirect_input builtin not found");
        return;
    }
    auto* targetFd = get(CoreVM::CoreNumber(node.targetFd->value));
    auto* source = codegen(node.source.get());
    if (!source)
        return;
    _result = createCallFunction(getBuiltinFunction(*callback), { targetFd, source }, "redirect_input");
}

void IRGenerator::visit(ast::HereDocument const& node)
{
    auto* callback = findCallback("internal.redirect_heredoc(IS)V");
    if (!callback)
    {
        reportTypeError("Internal error: internal.redirect_heredoc builtin not found");
        return;
    }
    auto* targetFd = get(CoreVM::CoreNumber(node.targetFd->value));
    auto* content = get(node.content);
    _result = createCallFunction(getBuiltinFunction(*callback), { targetFd, content }, "redirect_heredoc");
}

void IRGenerator::visit(ast::HereString const& node)
{
    auto* callback = findCallback("internal.redirect_herestring(IS)V");
    if (!callback)
    {
        reportTypeError("Internal error: internal.redirect_herestring builtin not found");
        return;
    }
    auto* targetFd = get(CoreVM::CoreNumber(node.targetFd->value));
    auto* content = codegen(node.content.get());
    if (!content)
        return;
    _result = createCallFunction(getBuiltinFunction(*callback), { targetFd, content }, "redirect_herestring");
}

void IRGenerator::visit(ast::LiteralExpr const& node)
{
    _result = get(node.value);
}

void IRGenerator::visit(ast::TildeExpr const& node)
{
    if (node.user.empty())
    {
        // Standalone ~ or ~/path - expand to home directory
        auto* callback = findCallback("expand.tilde(S)S");
        if (!callback)
        {
            reportTypeError("Internal error: expand.tilde builtin not found");
            return;
        }
        _result = createCallFunction(getBuiltinFunction(*callback), { get(node.suffix) }, "expand.tilde");
    }
    else
    {
        // ~user or ~user/path - expand to user's home directory
        auto* callback = findCallback("expand.tilde_user(SS)S");
        if (!callback)
        {
            reportTypeError("Internal error: expand.tilde_user builtin not found");
            return;
        }
        _result = createCallFunction(
            getBuiltinFunction(*callback), { get(node.user), get(node.suffix) }, "expand.tilde_user");
    }
}

void IRGenerator::visit(ast::GlobExpr const& node)
{
    auto* callback = findCallback("expand.glob(S)V");
    if (!callback)
    {
        reportTypeError("Internal error: expand.glob builtin not found");
        return;
    }
    // Glob expansion is handled specially - it adds multiple arguments to the command builder
    createCallFunction(getBuiltinFunction(*callback), { get(node.pattern) }, "expand.glob");
    _result = nullptr; // Result is captured via cmdBuilderArgs
}

void IRGenerator::visit(ast::ConcatExpr const& node)
{
    // Generate code for each part and concatenate them
    if (node.parts.empty())
    {
        _result = get("");
        return;
    }

    // Generate code for the first part
    auto* result = codegen(node.parts[0].get());
    if (!result)
        return;

    // Ensure result is a string
    if (result->type() != CoreVM::LiteralType::String)
    {
        // Convert to string if needed
        auto* callback = findCallback("expand.to_string(I)S");
        if (callback)
            result = createCallFunction(getBuiltinFunction(*callback), { result }, "to_string");
    }

    // Concatenate remaining parts
    for (size_t i = 1; i < node.parts.size(); ++i)
    {
        auto* part = codegen(node.parts[i].get());
        if (!part)
            return;

        // Ensure part is a string
        if (part->type() != CoreVM::LiteralType::String)
        {
            auto* callback = findCallback("expand.to_string(I)S");
            if (callback)
                part = createCallFunction(getBuiltinFunction(*callback), { part }, "to_string");
        }

        result = createSAdd(result, part, "concat");
    }

    _result = result;
}

void IRGenerator::visit(ast::ArithExpansionExpr const& node)
{
    // Evaluate the arithmetic expression and return the result as a string
    auto* result = codegenArith(node.expression.get());
    if (!result)
        return;

    // Convert the integer result to a string
    auto* callback = findCallback("expand.arith_to_string(I)S");
    if (!callback)
    {
        reportTypeError("Internal error: expand.arith_to_string builtin not found");
        return;
    }
    _result = createCallFunction(getBuiltinFunction(*callback), { result }, "expand.arith_to_string");
}

CoreVM::Value* IRGenerator::codegenArith(ast::ArithExpr const* expr)
{
    if (auto const* lit = dynamic_cast<ast::ArithLiteralExpr const*>(expr))
    {
        return get(CoreVM::CoreNumber(lit->value));
    }
    else if (auto const* var = dynamic_cast<ast::ArithVarExpr const*>(expr))
    {
        // Get variable value and convert to integer
        auto* callback = findCallback("expand.arith_getvar(S)I");
        if (!callback)
        {
            reportTypeError("Internal error: expand.arith_getvar builtin not found");
            return nullptr;
        }
        return createCallFunction(getBuiltinFunction(*callback), { get(var->name) }, "expand.arith_getvar");
    }
    else if (auto const* binary = dynamic_cast<ast::ArithBinaryExpr const*>(expr))
    {
        auto* left = codegenArith(binary->left.get());
        auto* right = codegenArith(binary->right.get());
        if (!left || !right)
            return nullptr;

        switch (binary->op)
        {
            case ast::ArithOp::Add: return createAdd(left, right);
            case ast::ArithOp::Sub: return createSub(left, right);
            case ast::ArithOp::Mul: return createMul(left, right);
            case ast::ArithOp::Div: return createDiv(left, right);
            case ast::ArithOp::Mod: return createRem(left, right);
            case ast::ArithOp::Pow: {
                // Power operation via builtin
                auto* callback = findCallback("expand.arith_pow(II)I");
                if (!callback)
                {
                    reportTypeError("Internal error: expand.arith_pow builtin not found");
                    return nullptr;
                }
                return createCallFunction(getBuiltinFunction(*callback), { left, right }, "expand.arith_pow");
            }
            case ast::ArithOp::Lt: return createNCmpLT(left, right);
            case ast::ArithOp::Gt: return createNCmpGT(left, right);
            case ast::ArithOp::Le: return createNCmpLE(left, right);
            case ast::ArithOp::Ge: return createNCmpGE(left, right);
            case ast::ArithOp::Eq: return createNCmpEQ(left, right);
            case ast::ArithOp::Ne: return createNCmpNE(left, right);
            case ast::ArithOp::And: return createAnd(left, right);
            case ast::ArithOp::Or: return createOr(left, right);
            case ast::ArithOp::BitAnd: return createAnd(left, right);
            case ast::ArithOp::BitOr: return createOr(left, right);
            case ast::ArithOp::BitXor: return createXor(left, right);
            case ast::ArithOp::Shl: return createShl(left, right);
            case ast::ArithOp::Shr: return createShr(left, right);
            default: reportTypeError("Unsupported arithmetic operator"); return nullptr;
        }
    }
    else if (auto const* unary = dynamic_cast<ast::ArithUnaryExpr const*>(expr))
    {
        auto* operand = codegenArith(unary->operand.get());
        if (!operand)
            return nullptr;

        switch (unary->op)
        {
            case ast::ArithOp::Neg:
                // Implement negation as 0 - operand for proper signed behavior
                return createSub(get(CoreVM::CoreNumber(0)), operand);
            case ast::ArithOp::Not: return createNot(operand);
            case ast::ArithOp::BitNot: return createNot(operand); // Bitwise NOT
            default: reportTypeError("Unsupported unary arithmetic operator"); return nullptr;
        }
    }

    reportTypeError("Unknown arithmetic expression type");
    return nullptr;
}

void IRGenerator::visit(ast::ParamExpansionExpr const& node)
{
    std::string callbackName;
    std::vector<CoreVM::Value*> args;

    switch (node.op)
    {
        case ast::ParamExpansionOp::Length:
            callbackName = "expand.param_length(S)S";
            args.push_back(get(node.variable));
            break;
        case ast::ParamExpansionOp::DefaultValue:
            callbackName = "expand.param_default(SS)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            break;
        case ast::ParamExpansionOp::AlternateValue:
            callbackName = "expand.param_alternate(SS)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            break;
        case ast::ParamExpansionOp::AssignDefault:
            callbackName = "expand.param_assign(SS)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            break;
        case ast::ParamExpansionOp::ErrorIfUnset:
            callbackName = "expand.param_error(SS)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            break;
        case ast::ParamExpansionOp::RemovePrefixShort:
            callbackName = "expand.param_remove_prefix(SSB)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            args.push_back(get(false)); // shortest
            break;
        case ast::ParamExpansionOp::RemovePrefixLong:
            callbackName = "expand.param_remove_prefix(SSB)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            args.push_back(get(true)); // longest
            break;
        case ast::ParamExpansionOp::RemoveSuffixShort:
            callbackName = "expand.param_remove_suffix(SSB)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            args.push_back(get(false)); // shortest
            break;
        case ast::ParamExpansionOp::RemoveSuffixLong:
            callbackName = "expand.param_remove_suffix(SSB)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            args.push_back(get(true)); // longest
            break;
        case ast::ParamExpansionOp::ReplaceFirst:
            callbackName = "expand.param_replace(SSSB)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            args.push_back(get(node.operand2));
            args.push_back(get(false)); // first only
            break;
        case ast::ParamExpansionOp::ReplaceAll:
            callbackName = "expand.param_replace(SSSB)S";
            args.push_back(get(node.variable));
            args.push_back(get(node.operand1));
            args.push_back(get(node.operand2));
            args.push_back(get(true)); // all
            break;
    }

    auto* callback = findCallback(callbackName);
    if (!callback)
    {
        reportTypeError("Internal error: parameter expansion builtin not found");
        return;
    }
    _result = createCallFunction(getBuiltinFunction(*callback), args, "expand.param");
}

void IRGenerator::visit(ast::VariableExpr const& node)
{
    switch (node.type)
    {
        case ast::VariableType::Named: {
            // Call getvar(name) to retrieve the variable value at runtime
            auto* callback = findCallback("getvar(S)S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar builtin not found");
                return;
            }
            _result = createCallFunction(getBuiltinFunction(*callback), { get(node.name) }, "getvar");
            break;
        }
        case ast::VariableType::ExitStatus: {
            auto* callback = findCallback("getvar.exitstatus()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.exitstatus builtin not found");
                return;
            }
            _result = createCallFunction(getBuiltinFunction(*callback), {}, "getvar.exitstatus");
            break;
        }
        case ast::VariableType::ProcessId: {
            auto* callback = findCallback("getvar.processid()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.processid builtin not found");
                return;
            }
            _result = createCallFunction(getBuiltinFunction(*callback), {}, "getvar.processid");
            break;
        }
        case ast::VariableType::BackgroundId: {
            auto* callback = findCallback("getvar.backgroundid()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.backgroundid builtin not found");
                return;
            }
            _result = createCallFunction(getBuiltinFunction(*callback), {}, "getvar.backgroundid");
            break;
        }
        case ast::VariableType::Positional: {
            // Convert name to integer index
            int index = 0;
            if (!node.name.empty())
                index = std::stoi(node.name);

            auto* callback = findCallback("getvar.positional(I)S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.positional builtin not found");
                return;
            }
            _result = createCallFunction(
                getBuiltinFunction(*callback), { get(CoreVM::CoreNumber(index)) }, "getvar.positional");
            break;
        }
    }
}

void IRGenerator::visit(ast::BuiltinUnsetStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    callArguments.push_back(get(node.name));
    _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "unset");
}

void IRGenerator::visit(ast::BuiltinJobsStmt const& node)
{
    _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "jobs");
}

void IRGenerator::visit(ast::BuiltinFgStmt const& node)
{
    if (!node.jobId)
    {
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "fg");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), { jobIdValue }, "fg");
    }
}

void IRGenerator::visit(ast::BuiltinBgStmt const& node)
{
    if (!node.jobId)
    {
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "bg");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), { jobIdValue }, "bg");
    }
}

void IRGenerator::visit(ast::BuiltinWaitStmt const& node)
{
    if (!node.jobId)
    {
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "wait");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), { jobIdValue }, "wait");
    }
}

void IRGenerator::visit(ast::BuiltinBindStmt const& node)
{
    if (node.args.empty())
    {
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "bind");
    }
    else
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.emplace_back(get(createCallArgs(node.args)));
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "bind");
    }
}

void IRGenerator::visit(ast::BuiltinWhichStmt const& node)
{
    if (node.args.empty())
    {
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "which");
    }
    else
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.emplace_back(get(createCallArgs(node.args)));
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "which");
    }
}

void IRGenerator::visit(ast::OutputRedirect const& node)
{
    if (std::holds_alternative<std::unique_ptr<ast::FileDescriptor>>(node.target))
    {
        // fd duplication: 2>&1
        auto* callback = findCallback("internal.redirect_fd_dup(II)V");
        if (!callback)
        {
            reportTypeError("Internal error: internal.redirect_fd_dup builtin not found");
            return;
        }
        auto* sourceFd = get(CoreVM::CoreNumber(node.source->value));
        auto* targetFd =
            get(CoreVM::CoreNumber(std::get<std::unique_ptr<ast::FileDescriptor>>(node.target)->value));
        _result =
            createCallFunction(getBuiltinFunction(*callback), { sourceFd, targetFd }, "redirect_fd_dup");
    }
    else
    {
        // file redirect: > file or >> file
        auto* callback = findCallback("internal.redirect_output(ISB)V");
        if (!callback)
        {
            reportTypeError("Internal error: internal.redirect_output builtin not found");
            return;
        }
        auto* sourceFd = get(CoreVM::CoreNumber(node.source->value));
        auto* target = codegen(std::get<std::unique_ptr<ast::Expr>>(node.target).get());
        if (!target)
            return;
        auto* append = get(node.append);
        _result = createCallFunction(
            getBuiltinFunction(*callback), { sourceFd, target, append }, "redirect_output");
    }
}

void IRGenerator::visit(ast::ProgramCall const& node)
{
    TRACE_SCOPE("ProgramCall");

    bool const hasRedirects = !node.inputRedirects.empty() || !node.outputRedirects.empty()
                              || !node.hereDocuments.empty() || !node.hereStrings.empty();

    // Start redirect context if we have any redirects
    if (hasRedirects)
    {
        auto* startCallback = findCallback("internal.redirect_start()V");
        if (startCallback)
            createCallFunction(getBuiltinFunction(*startCallback), {}, "redirect_start");
    }

    // Generate code for all redirects
    for (auto const& redirect: node.inputRedirects)
        codegen(redirect.get());

    for (auto const& redirect: node.outputRedirects)
        codegen(redirect.get());

    for (auto const& heredoc: node.hereDocuments)
        codegen(heredoc.get());

    for (auto const& herestring: node.hereStrings)
        codegen(herestring.get());

    if (containsRuntimeExpr(node.parameters))
    {
        // Use dynamic argument building and execution
        buildCommandArgs(node.program, node.parameters);
        _result = execBuiltCommand();
    }
    else
    {
        // Use constant array (fast path)
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.push_back(get(createCallArgs(node.program, node.parameters)));
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "callProcess");
    }

    // End redirect context
    if (hasRedirects)
    {
        auto* endCallback = findCallback("internal.redirect_end()V");
        if (endCallback)
            createCallFunction(getBuiltinFunction(*endCallback), {}, "redirect_end");
    }
}

void IRGenerator::visit(ast::SubstitutionExpr const& node)
{
    // Command substitution: $(command) or `command`
    // 1. Start capture - redirects stdout to a pipe
    auto* startCb = findCallback("internal.subst_start()V");
    if (!startCb)
    {
        reportTypeError("Internal error: internal.subst_start builtin not found");
        return;
    }
    createCallFunction(getBuiltinFunction(*startCb), {}, "subst_start");

    // 2. Execute the command pipeline
    codegen(node.pipeline.get());

    // 3. End capture - reads captured output and returns as string
    auto* endCb = findCallback("internal.subst_end()S");
    if (!endCb)
    {
        reportTypeError("Internal error: internal.subst_end builtin not found");
        return;
    }
    _result = createCallFunction(getBuiltinFunction(*endCb), {}, "subst_end");
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
    pushLoopContext(cond, end);
    codegen(node.body.get());
    popLoopContext();
    // Only add loop-back branch if body wasn't terminated (by break/continue/return)
    if (getInsertPoint() && !getInsertPoint()->getTerminator())
        createBr(cond);

    setInsertPoint(end);
}

void IRGenerator::visit(ast::ForListStmt const& node)
{
    // for var in item1 item2 ...; do body; done
    //
    // IR pattern:
    //   for.init:  index = 0; items = [item1, item2, ...]
    //   for.cond:  if index >= count goto for.end
    //   for.body:  var = items[index]; BODY
    //   for.step:  index++; goto for.cond
    //   for.end:

    // Initialize the iterator
    auto* initIterCb = findCallback("internal.for_init(S)V");
    if (!initIterCb)
    {
        reportTypeError("Internal error: internal.for_init builtin not found");
        return;
    }
    createCallFunction(getBuiltinFunction(*initIterCb), { get(node.variable) }, "for_init");

    // Add all items to the iterator
    auto* addItemCb = findCallback("internal.for_add_item(S)V");
    if (!addItemCb)
    {
        reportTypeError("Internal error: internal.for_add_item builtin not found");
        return;
    }
    for (auto const& item: node.items)
    {
        auto* itemValue = codegen(item.get());
        if (itemValue)
            createCallFunction(getBuiltinFunction(*addItemCb), { itemValue }, "for_add_item");
    }

    CoreVM::BasicBlock* cond = createBlock("for.cond");
    CoreVM::BasicBlock* body = createBlock("for.body");
    CoreVM::BasicBlock* step = createBlock("for.step");
    CoreVM::BasicBlock* end = createBlock("for.end");

    createBr(cond);

    // Condition: check if there are more items
    setInsertPoint(cond);
    auto* hasMoreCb = findCallback("internal.for_has_more()B");
    if (!hasMoreCb)
    {
        reportTypeError("Internal error: internal.for_has_more builtin not found");
        return;
    }
    auto* hasMore = createCallFunction(getBuiltinFunction(*hasMoreCb), {}, "for_has_more");
    createCondBr(hasMore, body, end);

    // Body: set variable to next item and execute body
    setInsertPoint(body);
    auto* nextCb = findCallback("internal.for_next(S)V");
    if (!nextCb)
    {
        reportTypeError("Internal error: internal.for_next builtin not found");
        return;
    }
    createCallFunction(getBuiltinFunction(*nextCb), { get(node.variable) }, "for_next");

    pushLoopContext(step, end);
    codegen(node.body.get());
    popLoopContext();
    // Only add step branch if body wasn't terminated (by break/continue/return)
    if (getInsertPoint() && !getInsertPoint()->getTerminator())
        createBr(step);

    // Step: just loop back to condition (next was already called)
    setInsertPoint(step);
    createBr(cond);

    // End: clean up the for-loop state
    setInsertPoint(end);
    auto* cleanupCb = findCallback("internal.for_cleanup()V");
    if (cleanupCb)
        createCallFunction(getBuiltinFunction(*cleanupCb), {}, "for_cleanup");
}

void IRGenerator::visit(ast::ForCStyleStmt const& node)
{
    // for ((init; cond; step)); do body; done
    //
    // IR pattern:
    //   forc.init: eval(init)
    //   forc.cond: if !eval(cond) goto forc.end
    //   forc.body: BODY
    //   forc.step: eval(step); goto forc.cond
    //   forc.end:

    CoreVM::BasicBlock* initBlock = createBlock("forc.init");
    CoreVM::BasicBlock* cond = createBlock("forc.cond");
    CoreVM::BasicBlock* body = createBlock("forc.body");
    CoreVM::BasicBlock* step = createBlock("forc.step");
    CoreVM::BasicBlock* end = createBlock("forc.end");

    createBr(initBlock);

    // Init: evaluate init expression
    setInsertPoint(initBlock);
    if (node.init)
        codegenArith(node.init.get());
    createBr(cond);

    // Condition: check condition
    setInsertPoint(cond);
    if (node.condition)
    {
        auto* condValue = codegenArith(node.condition.get());
        // If condValue is already Boolean (from comparison), use it directly
        // Otherwise, convert to Boolean by comparing with 0
        CoreVM::Value* condBool = condValue;
        if (condValue->type() != CoreVM::LiteralType::Boolean)
            condBool = createNCmpNE(condValue, get(CoreVM::CoreNumber(0)));
        createCondBr(condBool, body, end);
    }
    else
    {
        // No condition = infinite loop (always enter body)
        createBr(body);
    }

    // Body
    setInsertPoint(body);
    pushLoopContext(step, end);
    codegen(node.body.get());
    popLoopContext();
    createBr(step);

    // Step: evaluate step expression and loop
    setInsertPoint(step);
    if (node.step)
        codegenArith(node.step.get());
    createBr(cond);

    setInsertPoint(end);
}

void IRGenerator::visit(ast::CaseStmt const& node)
{
    // case word in pattern1) cmd1;; pattern2) cmd2;; esac
    //
    // IR pattern:
    //   case.word:  word_value = eval(word)
    //   case.check0: if matches(word, patterns[0]) goto case.body0
    //   case.check1: if matches(word, patterns[1]) goto case.body1
    //   ...         goto case.end
    //   case.body0: commands; goto case.end
    //   case.body1: commands; goto case.end
    //   case.end:

    // Evaluate the word first
    auto* wordValue = codegen(node.word.get());
    if (!wordValue)
        return;

    CoreVM::BasicBlock* endBlock = createBlock("case.end");

    // Create blocks for each clause
    std::vector<CoreVM::BasicBlock*> bodyBlocks;
    std::vector<CoreVM::BasicBlock*> checkBlocks;

    for (size_t i = 0; i < node.clauses.size(); ++i)
    {
        checkBlocks.push_back(createBlock(std::format("case.check{}", i)));
        bodyBlocks.push_back(createBlock(std::format("case.body{}", i)));
    }

    // Start checking patterns
    createBr(checkBlocks.empty() ? endBlock : checkBlocks[0]);

    // Generate pattern matching checks
    auto* matchCb = findCallback("internal.case_match(SS)B");
    if (!matchCb)
    {
        reportTypeError("Internal error: internal.case_match builtin not found");
        return;
    }

    for (size_t i = 0; i < node.clauses.size(); ++i)
    {
        auto const& clause = node.clauses[i];
        setInsertPoint(checkBlocks[i]);

        // Check each pattern (pipe-separated)
        // For multiple patterns, we chain the checks: if any pattern matches, go to body
        CoreVM::BasicBlock* nextClause = (i + 1 < checkBlocks.size()) ? checkBlocks[i + 1] : endBlock;

        for (size_t p = 0; p < clause.patterns.size(); ++p)
        {
            auto const& pattern = clause.patterns[p];
            auto* match =
                createCallFunction(getBuiltinFunction(*matchCb), { wordValue, get(pattern) }, "case_match");

            // Create intermediate check block for next pattern (if any)
            CoreVM::BasicBlock* nextPatternCheck =
                (p + 1 < clause.patterns.size()) ? createBlock(std::format("case.check{}.pat{}", i, p + 1))
                                                 : nextClause;

            createCondBr(match, bodyBlocks[i], nextPatternCheck);

            if (p + 1 < clause.patterns.size())
                setInsertPoint(nextPatternCheck);
        }

        // Handle empty patterns (shouldn't happen, but defensive)
        if (clause.patterns.empty())
            createBr(nextClause);
    }

    // Generate body blocks
    for (size_t i = 0; i < node.clauses.size(); ++i)
    {
        auto const& clause = node.clauses[i];
        setInsertPoint(bodyBlocks[i]);
        if (clause.body)
            codegen(clause.body.get());
        createBr(endBlock);
    }

    setInsertPoint(endBlock);
}

void IRGenerator::visit(ast::FunctionDefStmt const& node)
{
    // Register the function for later invocation
    // Functions are compiled as separate handlers and called at runtime
    auto* registerCb = findCallback("internal.function_register(S)V");
    if (!registerCb)
    {
        reportTypeError("Internal error: internal.function_register builtin not found");
        return;
    }

    // Save current handler and insertion point
    auto* savedHandler = handler();
    auto* savedBlock = getInsertPoint();

    // Create a new handler for the function and switch to it
    auto* funcHandler = getHandler(node.name);
    setHandler(funcHandler);
    auto* entryBlock = createBlock(node.name + ".entry");
    setInsertPoint(entryBlock);

    pushFunctionContext();
    codegen(node.body.get());
    popFunctionContext();

    // Always add return at the end - the VM will handle duplicate terminators
    createRet(get(CoreVM::CoreNumber(0)));

    // Restore to main handler
    setHandler(savedHandler);
    setInsertPoint(savedBlock);

    // Register the function name
    createCallFunction(getBuiltinFunction(*registerCb), { get(node.name) }, "function_register");
}

void IRGenerator::visit(ast::BreakStmt const& node)
{
    auto* ctx = getLoopContext(node.levels);
    if (!ctx)
    {
        reportTypeError("break: not in a loop");
        return;
    }
    createBr(ctx->breakTarget);
}

void IRGenerator::visit(ast::ContinueStmt const& node)
{
    auto* ctx = getLoopContext(node.levels);
    if (!ctx)
    {
        reportTypeError("continue: not in a loop");
        return;
    }
    createBr(ctx->continueTarget);
}

void IRGenerator::visit(ast::ReturnStmt const& node)
{
    if (!inFunction())
    {
        reportTypeError("return: not in a function");
        return;
    }

    CoreVM::Value* returnValue = nullptr;
    if (node.value)
    {
        returnValue = codegen(node.value.get());
        if (!returnValue)
            return;
        if (returnValue->type() == CoreVM::LiteralType::String)
            returnValue = createS2N(returnValue);
    }
    else
    {
        // Default to last exit code ($?)
        auto* exitStatusCb = findCallback("getvar.exitstatus()S");
        if (exitStatusCb)
        {
            auto* exitStr = createCallFunction(getBuiltinFunction(*exitStatusCb), {}, "getvar.exitstatus");
            returnValue = createS2N(exitStr);
        }
        else
        {
            returnValue = get(CoreVM::CoreNumber(0));
        }
    }

    // Set $? to the return value before exiting
    auto* setExitCb = findCallback("setvar.exitstatus(I)V");
    if (setExitCb)
        createCallFunction(getBuiltinFunction(*setExitCb), { returnValue }, "setvar.exitstatus");

    createRet(returnValue);
}

CoreVM::Value* IRGenerator::toBool(CoreVM::Value* value)
{
    if (value->type() == CoreVM::LiteralType::Boolean)
        return value;
    return createNCmpEQ(value, get(CoreVM::CoreNumber(0)));
}

bool IRGenerator::containsRuntimeExpr(std::vector<std::unique_ptr<ast::Expr>> const& expressions) const
{
    for (auto const& expr: expressions)
    {
        if (dynamic_cast<ast::VariableExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::SubstitutionExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::CommandFileSubst const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::TildeExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::ParamExpansionExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::GlobExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::ArithExpansionExpr const*>(expr.get()) != nullptr)
            return true;
        if (dynamic_cast<ast::ConcatExpr const*>(expr.get()) != nullptr)
            return true;
    }
    return false;
}

std::vector<CoreVM::Constant*> IRGenerator::createConstantArray(
    std::vector<std::unique_ptr<ast::Expr>> const& expressions)
{
    auto irArray = std::vector<CoreVM::Constant*> {};
    for (auto const& expr: expressions)
    {
        TRACE_SCOPE(std::format("Parameter: ", ast::ASTPrinter::print(*expr)));
        auto* value = codegen(expr.get());
        if (!value)
        {
            // Error already reported
            continue;
        }
        if (auto* constant = dynamic_cast<CoreVM::Constant*>(value); constant != nullptr)
            irArray.push_back(constant);
        else
        {
            reportTypeError("Non-constant expression in array context");
        }
    }
    return irArray;
}

void IRGenerator::buildCommandArgs(std::string const& programName,
                                   std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    TRACE_SCOPE("buildCommandArgs");

    // Start building the command with the program name
    auto* cmdStartCallback = findCallback("internal.cmd_start(S)V");
    if (!cmdStartCallback)
    {
        reportTypeError("Internal error: internal.cmd_start builtin not found");
        return;
    }
    createCallFunction(getBuiltinFunction(*cmdStartCallback), { get(programName) }, "cmd_start");

    // Add each argument
    auto* cmdArgCallback = findCallback("internal.cmd_arg(S)V");
    if (!cmdArgCallback)
    {
        reportTypeError("Internal error: internal.cmd_arg builtin not found");
        return;
    }

    for (auto const& arg: args)
    {
        auto* value = codegen(arg.get());
        if (!value)
            continue; // Error already reported

        createCallFunction(getBuiltinFunction(*cmdArgCallback), { value }, "cmd_arg");
    }
}

CoreVM::Value* IRGenerator::execBuiltCommand()
{
    auto* cmdExecCallback = findCallback("internal.cmd_exec()I");
    if (!cmdExecCallback)
    {
        reportTypeError("Internal error: internal.cmd_exec builtin not found");
        return nullptr;
    }
    return createCallFunction(getBuiltinFunction(*cmdExecCallback), {}, "cmd_exec");
}

CoreVM::Value* IRGenerator::execBuiltCommandPiped(bool lastInChain)
{
    auto* cmdExecCallback = findCallback("internal.cmd_exec_piped(B)I");
    if (!cmdExecCallback)
    {
        reportTypeError("Internal error: internal.cmd_exec_piped builtin not found");
        return nullptr;
    }
    return createCallFunction(getBuiltinFunction(*cmdExecCallback), { get(lastInChain) }, "cmd_exec_piped");
}

CoreVM::Value* IRGenerator::execBuiltCommandPipedBackground(
    std::string const& programName, std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    auto* cmdExecCallback = findCallback("internal.cmd_exec_piped_background(S)I");
    if (!cmdExecCallback)
    {
        reportTypeError("Internal error: internal.cmd_exec_piped_background builtin not found");
        return nullptr;
    }

    // Build the command string for the job table
    std::string command = programName;
    for (auto const& arg: args)
    {
        if (auto const* lit = dynamic_cast<ast::LiteralExpr const*>(arg.get()))
            command += " " + lit->value;
    }
    command += " &";

    return createCallFunction(
        getBuiltinFunction(*cmdExecCallback), { get(command) }, "cmd_exec_piped_background");
}

std::vector<CoreVM::Constant*> IRGenerator::createCallArgs(
    std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    TRACE_SCOPE("createCallArgs");
    return createConstantArray(args);
}

std::vector<CoreVM::Constant*> IRGenerator::createCallArgs(
    std::string const& programName, std::vector<std::unique_ptr<ast::Expr>> const& args)
{
    TRACE_SCOPE("createCallArgs");
    auto callArguments = createConstantArray(args);
    callArguments.insert(callArguments.begin(), get(programName));
    return callArguments;
}

void IRGenerator::pushLoopContext(CoreVM::BasicBlock* continueTarget, CoreVM::BasicBlock* breakTarget)
{
    _loopStack.push_back({ continueTarget, breakTarget });
}

void IRGenerator::popLoopContext()
{
    if (!_loopStack.empty())
        _loopStack.pop_back();
}

IRGenerator::LoopContext* IRGenerator::getLoopContext(int levels)
{
    if (_loopStack.empty())
        return nullptr;

    // levels is 1-indexed: break 1 = current loop, break 2 = parent loop
    int const index = static_cast<int>(_loopStack.size()) - levels;
    if (index < 0)
        return nullptr;

    return &_loopStack[static_cast<size_t>(index)];
}

void IRGenerator::pushFunctionContext()
{
    ++_functionDepth;
}

void IRGenerator::popFunctionContext()
{
    if (_functionDepth > 0)
        --_functionDepth;
}

bool IRGenerator::inFunction() const
{
    return _functionDepth > 0;
}

} // namespace endo
