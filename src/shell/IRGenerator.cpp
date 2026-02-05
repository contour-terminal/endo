// SPDX-License-Identifier: Apache-2.0
module;
#include <shell/AST.h>
#include <shell/DiagnosticsAdapter.h>
#include <shell/ScopedLogger.h>

#include <typeinfo>

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

import CoreVM;
import ASTPrinter;

export module IRGenerator;

#define GLOBAL_SCOPE_INIT_NAME "@main"

namespace endo
{

export class IRGenerator final: public CoreVM::IRBuilder, public ast::Visitor
{
  public:
    /// Generates IR code from an AST.
    ///
    /// @param rootNode The root statement of the AST
    /// @param report Diagnostics report for error messages
    /// @param runtime Runtime instance for accessing builtins
    /// @return The generated IR program, or nullptr if errors occurred
    static std::unique_ptr<CoreVM::IRProgram> generate(ast::Statement const& rootNode,
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

  private:
    explicit IRGenerator(CoreVM::diagnostics::Report& report, CoreVM::Runtime& runtime):
        _report { report }, _runtime { runtime }
    {
        _processCallSignature.setReturnType(CoreVM::LiteralType::Number);
        _processCallSignature.setName("ProcessCall");
    }

    /// Finds a builtin function by its signature string.
    [[nodiscard]] CoreVM::NativeCallback* findCallback(std::string const& signature) const
    {
        return _runtime.find(signature);
    }

    CoreVM::Value* codegen(ast::Node const* node)
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

    /// Reports a type error at the current location.
    template <typename... Args>
    void reportTypeError(std::format_string<Args...> f, Args&&... args)
    {
        _report.typeError(_currentLocation, f, std::forward<Args>(args)...);
        _hasErrors = true;
    }

    void visit(ast::BuiltinExitStmt const& node) override
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

    void visit(ast::BuiltinExportStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.push_back(get(node.name));
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "export");
    }

    void visit(ast::BuiltinChDirStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        if (node.path)
            callArguments.push_back(codegen(node.path.get()));

        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "chdir");
    }

    void visit(ast::BuiltinSetStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        if (node.name && node.value)
        {

            callArguments.push_back(codegen(node.name.get()));
            callArguments.push_back(codegen(node.value.get()));
        }

        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "set");
    }

    void visit(ast::BuiltinFalseStmt const& node) override
    {
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "false");
    }

    void visit(ast::BuiltinReadStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        if (!node.parameters.empty())
            callArguments.emplace_back(get(createCallArgs(node.parameters)));

        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "read");
    }

    void visit(ast::BuiltinTrueStmt const& node) override
    {
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), {}, "true");
    }

    void visit(ast::CallPipeline const& node) override
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
                _result = execBuiltCommandPiped(lastInChain);
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

            // End redirect context
            if (hasRedirects)
            {
                auto* endCallback = findCallback("internal.redirect_end()V");
                if (endCallback)
                    createCallFunction(getBuiltinFunction(*endCallback), {}, "redirect_end");
            }
        }
    }

    void visit(ast::CommandFileSubst const& node) override
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

        auto* forkResult =
            createCallFunction(getBuiltinFunction(*forkCb), { get(isWrite) }, "procsubst_fork");

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

    void visit(ast::CompoundStmt const& node) override
    {
        for (auto const& stmt: node.statements)
            codegen(stmt.get());

        _result = nullptr;
    }

    void visit(ast::FileDescriptor const& node) override { _result = get(CoreVM::CoreNumber { node.value }); }

    void visit(ast::IfStmt const& node) override
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

    void visit(ast::LogicalAndStmt const& node) override
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

    void visit(ast::LogicalOrStmt const& node) override
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

    void visit(ast::InputRedirect const& node) override
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

    void visit(ast::HereDocument const& node) override
    {
        auto* callback = findCallback("internal.redirect_heredoc(IS)V");
        if (!callback)
        {
            reportTypeError("Internal error: internal.redirect_heredoc builtin not found");
            return;
        }
        auto* targetFd = get(CoreVM::CoreNumber(node.targetFd->value));
        auto* content = get(node.content);
        _result =
            createCallFunction(getBuiltinFunction(*callback), { targetFd, content }, "redirect_heredoc");
    }

    void visit(ast::HereString const& node) override
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
        _result =
            createCallFunction(getBuiltinFunction(*callback), { targetFd, content }, "redirect_herestring");
    }

    void visit(ast::LiteralExpr const& node) override { _result = get(node.value); }

    void visit(ast::TildeExpr const& node) override
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

    void visit(ast::GlobExpr const& node) override
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

    void visit(ast::ArithExpansionExpr const& node) override
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

    /// Generates code for an arithmetic expression, returning an integer value.
    CoreVM::Value* codegenArith(ast::ArithExpr const* expr)
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
            return createCallFunction(
                getBuiltinFunction(*callback), { get(var->name) }, "expand.arith_getvar");
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
                    return createCallFunction(
                        getBuiltinFunction(*callback), { left, right }, "expand.arith_pow");
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

    void visit(ast::ParamExpansionExpr const& node) override
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

    void visit(ast::VariableExpr const& node) override
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

    void visit(ast::BuiltinUnsetStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.push_back(get(node.name));
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "unset");
    }

    void visit(ast::OutputRedirect const& node) override
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

    void visit(ast::ProgramCall const& node) override
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
            _result =
                createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "callProcess");
        }

        // End redirect context
        if (hasRedirects)
        {
            auto* endCallback = findCallback("internal.redirect_end()V");
            if (endCallback)
                createCallFunction(getBuiltinFunction(*endCallback), {}, "redirect_end");
        }
    }

    void visit(ast::SubstitutionExpr const& node) override
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

    void visit(ast::WhileStmt const& node) override
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

    /// Converts a value to a boolean for conditional branching.
    /// For Numbers: true if equal to 0 (shell convention: 0 = success = true)
    /// For Booleans: use directly
    CoreVM::Value* toBool(CoreVM::Value* value)
    {
        if (value->type() == CoreVM::LiteralType::Boolean)
            return value;
        return createNCmpEQ(value, get(CoreVM::CoreNumber(0)));
    }

    /// Checks if any expression in the list contains a runtime-evaluated expression.
    /// This includes variable expressions, command substitutions, process substitutions, tilde and param
    /// expansion.
    [[nodiscard]] bool containsRuntimeExpr(std::vector<std::unique_ptr<ast::Expr>> const& expressions) const
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
        }
        return false;
    }

    std::vector<CoreVM::Constant*> createConstantArray(
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

    /// Builds command arguments using the command builder builtins.
    /// This is used when arguments contain variable expressions that need runtime evaluation.
    void buildCommandArgs(std::string const& programName, std::vector<std::unique_ptr<ast::Expr>> const& args)
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

    /// Executes the built command (non-piped version).
    CoreVM::Value* execBuiltCommand()
    {
        auto* cmdExecCallback = findCallback("internal.cmd_exec()I");
        if (!cmdExecCallback)
        {
            reportTypeError("Internal error: internal.cmd_exec builtin not found");
            return nullptr;
        }
        return createCallFunction(getBuiltinFunction(*cmdExecCallback), {}, "cmd_exec");
    }

    /// Executes the built command (piped version).
    CoreVM::Value* execBuiltCommandPiped(bool lastInChain)
    {
        auto* cmdExecCallback = findCallback("internal.cmd_exec_piped(B)I");
        if (!cmdExecCallback)
        {
            reportTypeError("Internal error: internal.cmd_exec_piped builtin not found");
            return nullptr;
        }
        return createCallFunction(
            getBuiltinFunction(*cmdExecCallback), { get(lastInChain) }, "cmd_exec_piped");
    }

    std::vector<CoreVM::Constant*> createCallArgs(std::vector<std::unique_ptr<ast::Expr>> const& args)
    {
        TRACE_SCOPE("createCallArgs");
        return createConstantArray(args);
    }

    std::vector<CoreVM::Constant*> createCallArgs(std::string const& programName,
                                                  std::vector<std::unique_ptr<ast::Expr>> const& args)
    {
        TRACE_SCOPE("createCallArgs");
        auto callArguments = createConstantArray(args);
        callArguments.insert(callArguments.begin(), get(programName));
        return callArguments;
    }

    CoreVM::diagnostics::Report& _report;    // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    CoreVM::Runtime& _runtime;               // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    CoreVM::SourceLocation _currentLocation; ///< Current location for error reporting
    bool _hasErrors = false;                 ///< Whether any errors have been reported
    CoreVM::Value* _result = nullptr;
    // CoreVM::NativeCallback _processCallCallback;
    // CoreVM::IRBuiltinFunction* _processCallFunction = nullptr;
    CoreVM::Signature _processCallSignature;
};
} // namespace endo
