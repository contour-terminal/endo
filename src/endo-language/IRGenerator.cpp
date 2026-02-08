// SPDX-License-Identifier: Apache-2.0
#include "IRGenerator.hpp"

#include <CoreVM/CoreVM.hpp>

#include <functional>
#include <typeinfo>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "DiagnosticsAdapter.hpp"
#include "Pattern.hpp"
#include "PatternIRGenerator.hpp"
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
                                                         CoreVM::Runtime& runtime,
                                                         FSharpPersistentState* persistentState)
{
    IRGenerator generator(report, runtime);

    generator._builder.setProgram(std::make_unique<CoreVM::IRProgram>());
    generator._builder.setHandler(generator._builder.getHandler(GLOBAL_SCOPE_INIT_NAME));
    generator._builder.setInsertPoint(generator._builder.createBlock("EntryPoint"));

    // Initialize F# root scope
    generator.pushFSharpScope();

    // Pre-populate function table from persistent state (REPL session continuity)
    if (persistentState)
    {
        for (auto const& [name, persisted]: persistentState->functions)
        {
            FSharpFunction func;
            func.parameters = persisted.parameters;
            func.body = persisted.body;
            func.returnsResultOrOption = persisted.returnsResultOrOption;
            func.isRecursive = persisted.isRecursive;
            // capturedBindings intentionally left empty — captures from previous
            // IR programs are no longer valid; only pure functions persist correctly.
            generator.registerFSharpFunction(name, std::move(func));
        }
    }

    generator.codegen(&rootNode);

    // Persist newly defined functions back to persistent state
    if (persistentState)
    {
        for (auto const& [name, func]: generator._fsharpFunctions)
        {
            // Skip auto-generated lambda names (partial application intermediates)
            if (name.starts_with("__lambda_"))
                continue;

            FSharpPersistentState::PersistedFunction persisted;
            persisted.parameters = func.parameters;
            persisted.body = func.body;
            persisted.returnsResultOrOption = func.returnsResultOrOption;
            persisted.isRecursive = func.isRecursive;
            persistentState->functions[name] = std::move(persisted);
        }
    }

    // Clean up F# scope
    generator.popFSharpScope();

    if (generator._hasErrors)
        return nullptr;

    generator._builder.createRet(generator._builder.get(CoreVM::CoreNumber(0)));

    return generator._builder.takeProgram();
}

IRGenerator::IRGenerator(CoreVM::diagnostics::Report& report, CoreVM::Runtime& runtime):
    _report { report }, _runtime { runtime }
{
    _processCallSignature.setReturnType(CoreVM::LiteralType::Number);
    _processCallSignature.setName("ProcessCall");
}

// F# scope management implementation
void IRGenerator::pushFSharpScope()
{
    auto newScope = std::make_unique<FSharpScope>();
    newScope->parent = _currentFSharpScope;
    if (!_rootFSharpScope)
    {
        _rootFSharpScope = std::move(newScope);
        _currentFSharpScope = _rootFSharpScope.get();
    }
    else
    {
        _currentFSharpScope = newScope.release();
    }
}

void IRGenerator::popFSharpScope()
{
    if (_currentFSharpScope)
    {
        // Release all object variables in this scope before exiting.
        // We pass the storage (alloca) directly to ObjReleaseInstr, which allows
        // the TargetCodeGenerator to emit LOAD from the alloca's fixed index.
        // This avoids cross-block value tracking issues.
        for (CoreVM::AllocaInstr* storage: _currentFSharpScope->objectVariables)
        {
            _builder.createObjRelease(storage, "scope.exit.release");
        }

        FSharpScope* parent = _currentFSharpScope->parent;
        if (_currentFSharpScope != _rootFSharpScope.get())
        {
            delete _currentFSharpScope;
        }
        _currentFSharpScope = parent;
    }
}

void IRGenerator::bindFSharpVariable(std::string const& name, CoreVM::Value* value, bool isMutable)
{
    if (_currentFSharpScope)
        _currentFSharpScope->bindings[name] = BindingInfo { value, isMutable };
}

void IRGenerator::bindFSharpObjectVariable(std::string const& name,
                                           CoreVM::AllocaInstr* storage,
                                           bool isMutable)
{
    if (_currentFSharpScope)
    {
        // Track the storage for ORELEASE at scope exit
        _currentFSharpScope->objectVariables.push_back(storage);
        // Also bind as a regular variable
        _currentFSharpScope->bindings[name] = BindingInfo { storage, isMutable };
    }
}

CoreVM::Value* IRGenerator::lookupFSharpVariable(std::string const& name) const
{
    for (FSharpScope const* scope = _currentFSharpScope; scope != nullptr; scope = scope->parent)
    {
        auto it = scope->bindings.find(name);
        if (it != scope->bindings.end())
            return it->second.value;
    }
    return nullptr;
}

IRGenerator::BindingInfo const* IRGenerator::lookupFSharpBinding(std::string const& name) const
{
    for (FSharpScope const* scope = _currentFSharpScope; scope != nullptr; scope = scope->parent)
    {
        auto it = scope->bindings.find(name);
        if (it != scope->bindings.end())
            return &it->second;
    }
    return nullptr;
}

// F# function management implementation
void IRGenerator::registerFSharpFunction(std::string const& name, FSharpFunction func)
{
    _fsharpFunctions[name] = std::move(func);
}

IRGenerator::FSharpFunction const* IRGenerator::lookupFSharpFunction(std::string const& name) const
{
    auto it = _fsharpFunctions.find(name);
    if (it != _fsharpFunctions.end())
        return &it->second;
    return nullptr;
}

bool IRGenerator::isBodyResultOrOption(ast::Expr const* body) const
{
    if (!body)
        return false;

    // Direct Result/Option constructors
    if (dynamic_cast<ast::OptionExpr const*>(body))
        return true;
    if (dynamic_cast<ast::ResultExpr const*>(body))
        return true;

    // Try expressions produce Result/Option
    if (dynamic_cast<ast::TryExpr const*>(body))
        return true;

    // Check through parentheses
    if (auto* paren = dynamic_cast<ast::ParenExpr const*>(body))
        return isBodyResultOrOption(paren->inner.get());

    // Check match expression arms
    if (auto* match = dynamic_cast<ast::MatchExpr const*>(body))
    {
        for (auto const& arm: match->arms)
            if (isBodyResultOrOption(arm.body.get()))
                return true;
        return false;
    }

    // Check pipeline - the result type is the last function's return type
    if (auto* pipe = dynamic_cast<ast::PipelineExpr const*>(body))
    {
        // The function (right side) determines the result type
        // For simplicity, check if any part produces Result/Option
        if (isBodyResultOrOption(pipe->value.get()))
            return true;
        if (isBodyResultOrOption(pipe->function.get()))
            return true;
        return false;
    }

    // Function application - would need to look up the function's return type
    // For now, we don't handle this case (would require more complex analysis)

    return false;
}

std::string IRGenerator::generateLambdaName()
{
    return std::format("__lambda_{}", _lambdaCounter++);
}

std::unordered_map<std::string, CoreVM::Value*> IRGenerator::collectFreeVariables(
    ast::Expr const* body, std::vector<std::string> const& boundNames) const
{
    std::unordered_map<std::string, CoreVM::Value*> freeVars;

    // Recursive walker as a lambda (avoids needing a full Visitor subclass)
    std::function<void(ast::Expr const*, std::vector<std::string> const&)> walk =
        [&](ast::Expr const* expr, std::vector<std::string> const& bound) {
            if (!expr)
                return;

            if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(expr))
            {
                // Check if the identifier is already bound (parameter or locally-scoped)
                if (std::ranges::find(bound, ident->name) != bound.end())
                    return;
                // Check if it's a registered function name
                if (lookupFSharpFunction(ident->name) != nullptr)
                    return;
                // Check if it's accessible in the current variable scope
                if (auto* storage = lookupFSharpVariable(ident->name))
                    freeVars[ident->name] = storage;
                return;
            }

            if (auto const* bin = dynamic_cast<ast::BinaryExpr const*>(expr))
            {
                walk(bin->left.get(), bound);
                walk(bin->right.get(), bound);
                return;
            }

            if (auto const* unary = dynamic_cast<ast::UnaryExpr const*>(expr))
            {
                walk(unary->operand.get(), bound);
                return;
            }

            if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(expr))
            {
                walk(paren->inner.get(), bound);
                return;
            }

            if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(expr))
            {
                walk(app->function.get(), bound);
                walk(app->argument.get(), bound);
                return;
            }

            if (auto const* pipe = dynamic_cast<ast::PipelineExpr const*>(expr))
            {
                walk(pipe->value.get(), bound);
                walk(pipe->function.get(), bound);
                return;
            }

            if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(expr))
            {
                // Lambda parameters shadow outer bindings within the lambda body
                auto innerBound = bound;
                innerBound.insert(innerBound.end(), lambda->parameters.begin(), lambda->parameters.end());
                walk(lambda->body.get(), innerBound);
                return;
            }

            if (auto const* match = dynamic_cast<ast::MatchExpr const*>(expr))
            {
                walk(match->scrutinee.get(), bound);
                for (auto const& arm: match->arms)
                {
                    // Pattern bindings shadow outer names within the arm body and guard
                    auto armBound = bound;
                    auto bindings = pattern::collectBindings(*arm.pattern);
                    armBound.insert(armBound.end(), bindings.begin(), bindings.end());
                    if (arm.guard)
                        walk(arm.guard.get(), armBound);
                    walk(arm.body.get(), armBound);
                }
                return;
            }

            if (auto const* opt = dynamic_cast<ast::OptionExpr const*>(expr))
            {
                if (opt->value)
                    walk(opt->value.get(), bound);
                return;
            }

            if (auto const* res = dynamic_cast<ast::ResultExpr const*>(expr))
            {
                if (res->payload)
                    walk(res->payload.get(), bound);
                return;
            }

            if (auto const* tryExpr = dynamic_cast<ast::TryExpr const*>(expr))
            {
                walk(tryExpr->operand.get(), bound);
                return;
            }

            if (auto const* tryWith = dynamic_cast<ast::TryWithExpr const*>(expr))
            {
                walk(tryWith->body.get(), bound);
                for (auto const& handler: tryWith->handlers)
                {
                    auto handlerBound = bound;
                    auto bindings = pattern::collectBindings(*handler.pattern);
                    handlerBound.insert(handlerBound.end(), bindings.begin(), bindings.end());
                    if (handler.guard)
                        walk(handler.guard.get(), handlerBound);
                    walk(handler.body.get(), handlerBound);
                }
                return;
            }

            if (auto const* list = dynamic_cast<ast::ListExpr const*>(expr))
            {
                for (auto const& elem: list->elements)
                    walk(elem.get(), bound);
                return;
            }

            if (auto const* range = dynamic_cast<ast::ListRangeExpr const*>(expr))
            {
                walk(range->start.get(), bound);
                if (range->step)
                    walk(range->step.get(), bound);
                walk(range->end.get(), bound);
                return;
            }

            if (auto const* comp = dynamic_cast<ast::ListComprehensionExpr const*>(expr))
            {
                walk(comp->source.get(), bound);
                // The iteration variable is bound within filter and body
                auto innerBound = bound;
                innerBound.push_back(comp->variable);
                if (comp->filter)
                    walk(comp->filter.get(), innerBound);
                walk(comp->body.get(), innerBound);
                return;
            }

            if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(expr))
            {
                walk(ifExpr->condition.get(), bound);
                walk(ifExpr->thenExpr.get(), bound);
                walk(ifExpr->elseExpr.get(), bound);
                return;
            }

            if (auto const* tupleExpr = dynamic_cast<ast::TupleExpr const*>(expr))
            {
                for (auto const& elem: tupleExpr->elements)
                    walk(elem.get(), bound);
                return;
            }

            // Literal types (IntLiteralExpr, FloatLiteralExpr, BoolLiteralExpr) have no free variables.
            // ShellCommandExpr has no F# free variables.
        };

    walk(body, boundNames);
    return freeVars;
}

CoreVM::AllocaInstr* IRGenerator::createAllocaInEntryBlock(CoreVM::LiteralType type, std::string const& name)
{
    // Get the entry block of the current handler
    CoreVM::BasicBlock* entryBlock = _builder.handler()->getEntryBlock();

    // Create the alloca instruction
    auto allocaInstr = std::make_unique<CoreVM::AllocaInstr>(
        type, _builder.get(CoreVM::CoreNumber(1)), _builder.makeName(name));

    // Insert before terminator in entry block (handles case where entry block already has a branch)
    CoreVM::Instr* inserted = entryBlock->insertBeforeTerminator(std::move(allocaInstr));

    return static_cast<CoreVM::AllocaInstr*>(inserted);
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
        // Track current location for error reporting and IR instruction annotation
        if (node->location.has_value())
        {
            _builder.setSourceLocation(toCoreLoc(node->location.value()));
        }
        node->accept(*this);
    }
    return _result;
}

template <typename... Args>
void IRGenerator::reportTypeError(std::format_string<Args...> f, Args&&... args)
{
    _report.typeError(_builder.sourceLocation(), f, std::forward<Args>(args)...);
    _hasErrors = true;
}

void IRGenerator::visit(ast::BuiltinExitStmt const& node)
{
    CoreVM::Value* exitCode = nullptr;
    if (!node.code)
        exitCode = _builder.get(CoreVM::CoreNumber(0));
    else
    {
        // Special handling: if the exit code is a LiteralExpr that looks like an identifier,
        // check if it's an F# variable first (e.g., "exit r" where r is a let-bound variable)
        if (auto const* literal = dynamic_cast<ast::LiteralExpr const*>(node.code.get()))
        {
            // Check if this literal is an F# variable name
            if (CoreVM::Value* fsharpVar = lookupFSharpVariable(literal->value))
            {
                // It's an F# variable - load its value
                exitCode = _builder.createLoad(fsharpVar, literal->value);
            }
        }

        if (!exitCode)
        {
            exitCode = codegen(node.code.get());
            if (!exitCode)
                return; // Error already reported
        }

        if (exitCode->type() == CoreVM::LiteralType::String)
            exitCode = _builder.createS2N(exitCode);
        else if (exitCode->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("exit code must be a number, got {}", exitCode->type());
            return;
        }
    }
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), { exitCode }, "exit");
}

void IRGenerator::visit(ast::BuiltinExportStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    callArguments.push_back(_builder.get(node.name));
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(node.callback.get()), callArguments, "export");
}

void IRGenerator::visit(ast::BuiltinChDirStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (node.path)
        callArguments.push_back(codegen(node.path.get()));

    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "chdir");
}

void IRGenerator::visit(ast::BuiltinSetStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (node.name && node.value)
    {

        callArguments.push_back(codegen(node.name.get()));
        callArguments.push_back(codegen(node.value.get()));
    }

    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "set");
}

void IRGenerator::visit(ast::BuiltinFalseStmt const& node)
{
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "false");
}

void IRGenerator::visit(ast::BuiltinReadStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    if (!node.parameters.empty())
        callArguments.emplace_back(_builder.get(createCallArgs(node.parameters)));

    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "read");
}

void IRGenerator::visit(ast::BuiltinTrueStmt const& node)
{
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "true");
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
                _builder.createCallFunction(
                    _builder.getBuiltinFunction(*startCallback), {}, "redirect_start");
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
                callArguments.push_back(_builder.get(lastInChain));
                callArguments.push_back(_builder.get(createCallArgs(call->program, call->parameters)));
                _result = _builder.createCallFunction(
                    _builder.getBuiltinFunction(call->callback.get()), callArguments, "callProcess");
            }
        }

        // End redirect context
        if (hasRedirects)
        {
            auto* endCallback = findCallback("internal.redirect_end()V");
            if (endCallback)
                _builder.createCallFunction(_builder.getBuiltinFunction(*endCallback), {}, "redirect_end");
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

    auto* forkResult = _builder.createCallFunction(
        _builder.getBuiltinFunction(*forkCb), { _builder.get(isWrite) }, "procsubst_fork");

    // Check if we're the child (result == 0)
    auto* isChild = _builder.createNCmpEQ(forkResult, _builder.get(CoreVM::CoreNumber(0)));

    CoreVM::BasicBlock* childBlock = _builder.createBlock("procsubst.child");
    CoreVM::BasicBlock* contBlock = _builder.createBlock("procsubst.cont");

    _builder.createCondBr(isChild, childBlock, contBlock);

    // Child block: execute command, then exit
    _builder.setInsertPoint(childBlock);
    codegen(node.command.get());

    // Child exits after running command
    auto* exitCb = findCallback("internal.procsubst_exit()V");
    if (exitCb)
        _builder.createCallFunction(_builder.getBuiltinFunction(*exitCb), {}, "procsubst_exit");
    _builder.createBr(contBlock); // Unreachable due to exit, but needed for valid IR

    // Continue block: parent gets the fd path
    _builder.setInsertPoint(contBlock);
    auto* pathCb = findCallback("internal.procsubst_get_path()S");
    if (!pathCb)
    {
        reportTypeError("Internal error: internal.procsubst_get_path builtin not found");
        return;
    }
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*pathCb), {}, "procsubst_get_path");
}

void IRGenerator::visit(ast::CompoundStmt const& node)
{
    for (auto const& stmt: node.statements)
    {
        codegen(stmt.get());
        // Stop generating code after a terminator (break, continue, return, etc.)
        if (_builder.getInsertPoint() && _builder.getInsertPoint()->getTerminator() != nullptr)
            break;
    }

    _result = nullptr;
}

void IRGenerator::visit(ast::FileDescriptor const& node)
{
    _result = _builder.get(CoreVM::CoreNumber { node.value });
}

void IRGenerator::visit(ast::IfStmt const& node)
{
    CoreVM::BasicBlock* cond = _builder.createBlock("if.cond");
    CoreVM::BasicBlock* trueBlock = _builder.createBlock("if.trueBlock");
    CoreVM::BasicBlock* falseBlock = _builder.createBlock("if.falseBlock");
    CoreVM::BasicBlock* end = _builder.createBlock("if.end");

    _builder.createBr(cond);
    _builder.setInsertPoint(cond);
    _builder.createCondBr(toBool(codegen(node.condition.get())), trueBlock, falseBlock);

    _builder.setInsertPoint(trueBlock);
    codegen(node.thenBlock.get());
    _builder.createBr(end);

    _builder.setInsertPoint(falseBlock);
    codegen(node.elseBlock.get());
    _builder.createBr(end);

    _builder.setInsertPoint(end);
}

void IRGenerator::visit(ast::LogicalAndStmt const& node)
{
    // Short-circuit AND: execute right only if left succeeds (exit code 0)
    // A && B:
    //   eval A
    //   if A succeeded (exit code == 0): eval B, result = B's exit code
    //   else: result = A's exit code
    CoreVM::BasicBlock* evalRight = _builder.createBlock("and.evalRight");
    CoreVM::BasicBlock* end = _builder.createBlock("and.end");

    // Evaluate left side
    auto* leftResult = codegen(node.left.get());

    // If left succeeded (exit code == 0), evaluate right side
    _builder.createCondBr(toBool(leftResult), evalRight, end);

    // Evaluate right side
    _builder.setInsertPoint(evalRight);
    codegen(node.right.get());
    _builder.createBr(end);

    _builder.setInsertPoint(end);
    // Note: The exit code is automatically set by the last executed command
}

void IRGenerator::visit(ast::LogicalOrStmt const& node)
{
    // Short-circuit OR: execute right only if left fails (exit code != 0)
    // A || B:
    //   eval A
    //   if A failed (exit code != 0): eval B, result = B's exit code
    //   else: result = A's exit code (which is 0, success)
    CoreVM::BasicBlock* evalRight = _builder.createBlock("or.evalRight");
    CoreVM::BasicBlock* end = _builder.createBlock("or.end");

    // Evaluate left side
    auto* leftResult = codegen(node.left.get());

    // If left failed (exit code != 0), evaluate right side
    // toBool returns true for exit code 0 (success), so we flip the branches
    _builder.createCondBr(toBool(leftResult), end, evalRight);

    // Evaluate right side
    _builder.setInsertPoint(evalRight);
    codegen(node.right.get());
    _builder.createBr(end);

    _builder.setInsertPoint(end);
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
    auto* targetFd = _builder.get(CoreVM::CoreNumber(node.targetFd->value));
    auto* source = codegen(node.source.get());
    if (!source)
        return;
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { targetFd, source }, "redirect_input");
}

void IRGenerator::visit(ast::HereDocument const& node)
{
    auto* callback = findCallback("internal.redirect_heredoc(IS)V");
    if (!callback)
    {
        reportTypeError("Internal error: internal.redirect_heredoc builtin not found");
        return;
    }
    auto* targetFd = _builder.get(CoreVM::CoreNumber(node.targetFd->value));
    auto* content = _builder.get(node.content);
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { targetFd, content }, "redirect_heredoc");
}

void IRGenerator::visit(ast::HereString const& node)
{
    auto* callback = findCallback("internal.redirect_herestring(IS)V");
    if (!callback)
    {
        reportTypeError("Internal error: internal.redirect_herestring builtin not found");
        return;
    }
    auto* targetFd = _builder.get(CoreVM::CoreNumber(node.targetFd->value));
    auto* content = codegen(node.content.get());
    if (!content)
        return;
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { targetFd, content }, "redirect_herestring");
}

void IRGenerator::visit(ast::LiteralExpr const& node)
{
    _result = _builder.get(node.value);
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
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { _builder.get(node.suffix) }, "expand.tilde");
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
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback),
                                              { _builder.get(node.user), _builder.get(node.suffix) },
                                              "expand.tilde_user");
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
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { _builder.get(node.pattern) }, "expand.glob");
    _result = nullptr; // Result is captured via cmdBuilderArgs
}

void IRGenerator::visit(ast::ConcatExpr const& node)
{
    // Generate code for each part and concatenate them
    if (node.parts.empty())
    {
        _result = _builder.get("");
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
            result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { result }, "to_string");
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
                part = _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { part }, "to_string");
        }

        result = _builder.createSAdd(result, part, "concat");
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
    _result = _builder.createCallFunction(
        _builder.getBuiltinFunction(*callback), { result }, "expand.arith_to_string");
}

CoreVM::Value* IRGenerator::codegenArith(ast::ArithExpr const* expr)
{
    if (auto const* lit = dynamic_cast<ast::ArithLiteralExpr const*>(expr))
    {
        return _builder.get(CoreVM::CoreNumber(lit->value));
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
        return _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { _builder.get(var->name) }, "expand.arith_getvar");
    }
    else if (auto const* binary = dynamic_cast<ast::ArithBinaryExpr const*>(expr))
    {
        auto* left = codegenArith(binary->left.get());
        auto* right = codegenArith(binary->right.get());
        if (!left || !right)
            return nullptr;

        switch (binary->op)
        {
            case ast::ArithOp::Add: return _builder.createAdd(left, right);
            case ast::ArithOp::Sub: return _builder.createSub(left, right);
            case ast::ArithOp::Mul: return _builder.createMul(left, right);
            case ast::ArithOp::Div: return _builder.createDiv(left, right);
            case ast::ArithOp::Mod: return _builder.createRem(left, right);
            case ast::ArithOp::Pow: {
                // Power operation via builtin
                auto* callback = findCallback("expand.arith_pow(II)I");
                if (!callback)
                {
                    reportTypeError("Internal error: expand.arith_pow builtin not found");
                    return nullptr;
                }
                return _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { left, right }, "expand.arith_pow");
            }
            case ast::ArithOp::Lt: return _builder.createNCmpLT(left, right);
            case ast::ArithOp::Gt: return _builder.createNCmpGT(left, right);
            case ast::ArithOp::Le: return _builder.createNCmpLE(left, right);
            case ast::ArithOp::Ge: return _builder.createNCmpGE(left, right);
            case ast::ArithOp::Eq: return _builder.createNCmpEQ(left, right);
            case ast::ArithOp::Ne: return _builder.createNCmpNE(left, right);
            case ast::ArithOp::And: return _builder.createAnd(left, right);
            case ast::ArithOp::Or: return _builder.createOr(left, right);
            case ast::ArithOp::BitAnd: return _builder.createAnd(left, right);
            case ast::ArithOp::BitOr: return _builder.createOr(left, right);
            case ast::ArithOp::BitXor: return _builder.createXor(left, right);
            case ast::ArithOp::Shl: return _builder.createShl(left, right);
            case ast::ArithOp::Shr: return _builder.createShr(left, right);
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
                return _builder.createSub(_builder.get(CoreVM::CoreNumber(0)), operand);
            case ast::ArithOp::Not: return _builder.createNot(operand);
            case ast::ArithOp::BitNot: return _builder.createNot(operand); // Bitwise NOT
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
            args.push_back(_builder.get(node.variable));
            break;
        case ast::ParamExpansionOp::DefaultValue:
            callbackName = "expand.param_default(SS)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            break;
        case ast::ParamExpansionOp::AlternateValue:
            callbackName = "expand.param_alternate(SS)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            break;
        case ast::ParamExpansionOp::AssignDefault:
            callbackName = "expand.param_assign(SS)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            break;
        case ast::ParamExpansionOp::ErrorIfUnset:
            callbackName = "expand.param_error(SS)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            break;
        case ast::ParamExpansionOp::RemovePrefixShort:
            callbackName = "expand.param_remove_prefix(SSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(false)); // shortest
            break;
        case ast::ParamExpansionOp::RemovePrefixLong:
            callbackName = "expand.param_remove_prefix(SSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(true)); // longest
            break;
        case ast::ParamExpansionOp::RemoveSuffixShort:
            callbackName = "expand.param_remove_suffix(SSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(false)); // shortest
            break;
        case ast::ParamExpansionOp::RemoveSuffixLong:
            callbackName = "expand.param_remove_suffix(SSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(true)); // longest
            break;
        case ast::ParamExpansionOp::ReplaceFirst:
            callbackName = "expand.param_replace(SSSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(node.operand2));
            args.push_back(_builder.get(false)); // first only
            break;
        case ast::ParamExpansionOp::ReplaceAll:
            callbackName = "expand.param_replace(SSSB)S";
            args.push_back(_builder.get(node.variable));
            args.push_back(_builder.get(node.operand1));
            args.push_back(_builder.get(node.operand2));
            args.push_back(_builder.get(true)); // all
            break;
    }

    auto* callback = findCallback(callbackName);
    if (!callback)
    {
        reportTypeError("Internal error: parameter expansion builtin not found");
        return;
    }
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback), args, "expand.param");
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
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), { _builder.get(node.name) }, "getvar");
            break;
        }
        case ast::VariableType::ExitStatus: {
            auto* callback = findCallback("getvar.exitstatus()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.exitstatus builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), {}, "getvar.exitstatus");
            break;
        }
        case ast::VariableType::ProcessId: {
            auto* callback = findCallback("getvar.processid()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.processid builtin not found");
                return;
            }
            _result =
                _builder.createCallFunction(_builder.getBuiltinFunction(*callback), {}, "getvar.processid");
            break;
        }
        case ast::VariableType::BackgroundId: {
            auto* callback = findCallback("getvar.backgroundid()S");
            if (!callback)
            {
                reportTypeError("Internal error: getvar.backgroundid builtin not found");
                return;
            }
            _result = _builder.createCallFunction(
                _builder.getBuiltinFunction(*callback), {}, "getvar.backgroundid");
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
            _result = _builder.createCallFunction(_builder.getBuiltinFunction(*callback),
                                                  { _builder.get(CoreVM::CoreNumber(index)) },
                                                  "getvar.positional");
            break;
        }
    }
}

void IRGenerator::visit(ast::BuiltinUnsetStmt const& node)
{
    auto callArguments = std::vector<CoreVM::Value*> {};
    callArguments.push_back(_builder.get(node.name));
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), callArguments, "unset");
}

void IRGenerator::visit(ast::BuiltinJobsStmt const& node)
{
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "jobs");
}

void IRGenerator::visit(ast::BuiltinFgStmt const& node)
{
    if (!node.jobId)
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "fg");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = _builder.createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), { jobIdValue }, "fg");
    }
}

void IRGenerator::visit(ast::BuiltinBgStmt const& node)
{
    if (!node.jobId)
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "bg");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = _builder.createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), { jobIdValue }, "bg");
    }
}

void IRGenerator::visit(ast::BuiltinWaitStmt const& node)
{
    if (!node.jobId)
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "wait");
    }
    else
    {
        auto* jobIdValue = codegen(node.jobId.get());
        if (!jobIdValue)
            return;
        if (jobIdValue->type() == CoreVM::LiteralType::String)
            jobIdValue = _builder.createS2N(jobIdValue);
        else if (jobIdValue->type() != CoreVM::LiteralType::Number)
        {
            reportTypeError("job ID must be a number, got {}", jobIdValue->type());
            return;
        }
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), { jobIdValue }, "wait");
    }
}

void IRGenerator::visit(ast::BuiltinBindStmt const& node)
{
    if (node.args.empty())
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "bind");
    }
    else
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.emplace_back(_builder.get(createCallArgs(node.args)));
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), callArguments, "bind");
    }
}

void IRGenerator::visit(ast::BuiltinWhichStmt const& node)
{
    if (node.args.empty())
    {
        _result = _builder.createCallFunction(_builder.getBuiltinFunction(node.callback.get()), {}, "which");
    }
    else
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.emplace_back(_builder.get(createCallArgs(node.args)));
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), callArguments, "which");
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
        auto* sourceFd = _builder.get(CoreVM::CoreNumber(node.source->value));
        auto* targetFd = _builder.get(
            CoreVM::CoreNumber(std::get<std::unique_ptr<ast::FileDescriptor>>(node.target)->value));
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { sourceFd, targetFd }, "redirect_fd_dup");
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
        auto* sourceFd = _builder.get(CoreVM::CoreNumber(node.source->value));
        auto* target = codegen(std::get<std::unique_ptr<ast::Expr>>(node.target).get());
        if (!target)
            return;
        auto* append = _builder.get(node.append);
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(*callback), { sourceFd, target, append }, "redirect_output");
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
            _builder.createCallFunction(_builder.getBuiltinFunction(*startCallback), {}, "redirect_start");
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
        callArguments.push_back(_builder.get(createCallArgs(node.program, node.parameters)));
        _result = _builder.createCallFunction(
            _builder.getBuiltinFunction(node.callback.get()), callArguments, "callProcess");
    }

    // End redirect context
    if (hasRedirects)
    {
        auto* endCallback = findCallback("internal.redirect_end()V");
        if (endCallback)
            _builder.createCallFunction(_builder.getBuiltinFunction(*endCallback), {}, "redirect_end");
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
    _builder.createCallFunction(_builder.getBuiltinFunction(*startCb), {}, "subst_start");

    // 2. Execute the command pipeline
    codegen(node.pipeline.get());

    // 3. End capture - reads captured output and returns as string
    auto* endCb = findCallback("internal.subst_end()S");
    if (!endCb)
    {
        reportTypeError("Internal error: internal.subst_end builtin not found");
        return;
    }
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*endCb), {}, "subst_end");
}

void IRGenerator::visit(ast::WhileStmt const& node)
{
    CoreVM::BasicBlock* cond = _builder.createBlock("while.cond");
    CoreVM::BasicBlock* body = _builder.createBlock("while.body");
    CoreVM::BasicBlock* end = _builder.createBlock("while.end");

    _builder.createBr(cond);

    _builder.setInsertPoint(cond);
    _builder.createCondBr(toBool(codegen(node.condition.get())), body, end);

    _builder.setInsertPoint(body);
    pushLoopContext(cond, end);
    codegen(node.body.get());
    popLoopContext();
    // Only add loop-back branch if body wasn't terminated (by break/continue/return)
    if (_builder.getInsertPoint() && !_builder.getInsertPoint()->getTerminator())
        _builder.createBr(cond);

    _builder.setInsertPoint(end);
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
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*initIterCb), { _builder.get(node.variable) }, "for_init");

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
            _builder.createCallFunction(
                _builder.getBuiltinFunction(*addItemCb), { itemValue }, "for_add_item");
    }

    CoreVM::BasicBlock* cond = _builder.createBlock("for.cond");
    CoreVM::BasicBlock* body = _builder.createBlock("for.body");
    CoreVM::BasicBlock* step = _builder.createBlock("for.step");
    CoreVM::BasicBlock* end = _builder.createBlock("for.end");

    _builder.createBr(cond);

    // Condition: check if there are more items
    _builder.setInsertPoint(cond);
    auto* hasMoreCb = findCallback("internal.for_has_more()B");
    if (!hasMoreCb)
    {
        reportTypeError("Internal error: internal.for_has_more builtin not found");
        return;
    }
    auto* hasMore = _builder.createCallFunction(_builder.getBuiltinFunction(*hasMoreCb), {}, "for_has_more");
    _builder.createCondBr(hasMore, body, end);

    // Body: set variable to next item and execute body
    _builder.setInsertPoint(body);
    auto* nextCb = findCallback("internal.for_next(S)V");
    if (!nextCb)
    {
        reportTypeError("Internal error: internal.for_next builtin not found");
        return;
    }
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*nextCb), { _builder.get(node.variable) }, "for_next");

    pushLoopContext(step, end);
    codegen(node.body.get());
    popLoopContext();
    // Only add step branch if body wasn't terminated (by break/continue/return)
    if (_builder.getInsertPoint() && !_builder.getInsertPoint()->getTerminator())
        _builder.createBr(step);

    // Step: just loop back to condition (next was already called)
    _builder.setInsertPoint(step);
    _builder.createBr(cond);

    // End: clean up the for-loop state
    _builder.setInsertPoint(end);
    auto* cleanupCb = findCallback("internal.for_cleanup()V");
    if (cleanupCb)
        _builder.createCallFunction(_builder.getBuiltinFunction(*cleanupCb), {}, "for_cleanup");
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

    CoreVM::BasicBlock* initBlock = _builder.createBlock("forc.init");
    CoreVM::BasicBlock* cond = _builder.createBlock("forc.cond");
    CoreVM::BasicBlock* body = _builder.createBlock("forc.body");
    CoreVM::BasicBlock* step = _builder.createBlock("forc.step");
    CoreVM::BasicBlock* end = _builder.createBlock("forc.end");

    _builder.createBr(initBlock);

    // Init: evaluate init expression
    _builder.setInsertPoint(initBlock);
    if (node.init)
        codegenArith(node.init.get());
    _builder.createBr(cond);

    // Condition: check condition
    _builder.setInsertPoint(cond);
    if (node.condition)
    {
        auto* condValue = codegenArith(node.condition.get());
        // If condValue is already Boolean (from comparison), use it directly
        // Otherwise, convert to Boolean by comparing with 0
        CoreVM::Value* condBool = condValue;
        if (condValue->type() != CoreVM::LiteralType::Boolean)
            condBool = _builder.createNCmpNE(condValue, _builder.get(CoreVM::CoreNumber(0)));
        _builder.createCondBr(condBool, body, end);
    }
    else
    {
        // No condition = infinite loop (always enter body)
        _builder.createBr(body);
    }

    // Body
    _builder.setInsertPoint(body);
    pushLoopContext(step, end);
    codegen(node.body.get());
    popLoopContext();
    _builder.createBr(step);

    // Step: evaluate step expression and loop
    _builder.setInsertPoint(step);
    if (node.step)
        codegenArith(node.step.get());
    _builder.createBr(cond);

    _builder.setInsertPoint(end);
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

    CoreVM::BasicBlock* endBlock = _builder.createBlock("case.end");

    // Create blocks for each clause
    std::vector<CoreVM::BasicBlock*> bodyBlocks;
    std::vector<CoreVM::BasicBlock*> checkBlocks;

    for (size_t i = 0; i < node.clauses.size(); ++i)
    {
        checkBlocks.push_back(_builder.createBlock(std::format("case.check{}", i)));
        bodyBlocks.push_back(_builder.createBlock(std::format("case.body{}", i)));
    }

    // Start checking patterns
    _builder.createBr(checkBlocks.empty() ? endBlock : checkBlocks[0]);

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
        _builder.setInsertPoint(checkBlocks[i]);

        // Check each pattern (pipe-separated)
        // For multiple patterns, we chain the checks: if any pattern matches, go to body
        CoreVM::BasicBlock* nextClause = (i + 1 < checkBlocks.size()) ? checkBlocks[i + 1] : endBlock;

        for (size_t p = 0; p < clause.patterns.size(); ++p)
        {
            auto const& pattern = clause.patterns[p];
            auto* match = _builder.createCallFunction(
                _builder.getBuiltinFunction(*matchCb), { wordValue, _builder.get(pattern) }, "case_match");

            // Create intermediate check block for next pattern (if any)
            CoreVM::BasicBlock* nextPatternCheck =
                (p + 1 < clause.patterns.size())
                    ? _builder.createBlock(std::format("case.check{}.pat{}", i, p + 1))
                    : nextClause;

            _builder.createCondBr(match, bodyBlocks[i], nextPatternCheck);

            if (p + 1 < clause.patterns.size())
                _builder.setInsertPoint(nextPatternCheck);
        }

        // Handle empty patterns (shouldn't happen, but defensive)
        if (clause.patterns.empty())
            _builder.createBr(nextClause);
    }

    // Generate body blocks
    for (size_t i = 0; i < node.clauses.size(); ++i)
    {
        auto const& clause = node.clauses[i];
        _builder.setInsertPoint(bodyBlocks[i]);
        if (clause.body)
            codegen(clause.body.get());
        _builder.createBr(endBlock);
    }

    _builder.setInsertPoint(endBlock);
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
    auto* savedHandler = _builder.handler();
    auto* savedBlock = _builder.getInsertPoint();

    // Create a new handler for the function and switch to it
    auto* funcHandler = _builder.getHandler(node.name);
    _builder.setHandler(funcHandler);
    auto* entryBlock = _builder.createBlock(node.name + ".entry");
    _builder.setInsertPoint(entryBlock);

    pushFunctionContext();
    codegen(node.body.get());
    popFunctionContext();

    // Always add return at the end - the VM will handle duplicate terminators
    _builder.createRet(_builder.get(CoreVM::CoreNumber(0)));

    // Restore to main handler
    _builder.setHandler(savedHandler);
    _builder.setInsertPoint(savedBlock);

    // Register the function name
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*registerCb), { _builder.get(node.name) }, "function_register");
}

void IRGenerator::visit(ast::BreakStmt const& node)
{
    auto* ctx = getLoopContext(node.levels);
    if (!ctx)
    {
        reportTypeError("break: not in a loop");
        return;
    }
    _builder.createBr(ctx->breakTarget);
}

void IRGenerator::visit(ast::ContinueStmt const& node)
{
    auto* ctx = getLoopContext(node.levels);
    if (!ctx)
    {
        reportTypeError("continue: not in a loop");
        return;
    }
    _builder.createBr(ctx->continueTarget);
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
            returnValue = _builder.createS2N(returnValue);
    }
    else
    {
        // Default to last exit code ($?)
        auto* exitStatusCb = findCallback("getvar.exitstatus()S");
        if (exitStatusCb)
        {
            auto* exitStr = _builder.createCallFunction(
                _builder.getBuiltinFunction(*exitStatusCb), {}, "getvar.exitstatus");
            returnValue = _builder.createS2N(exitStr);
        }
        else
        {
            returnValue = _builder.get(CoreVM::CoreNumber(0));
        }
    }

    // Set $? to the return value before exiting
    auto* setExitCb = findCallback("setvar.exitstatus(I)V");
    if (setExitCb)
        _builder.createCallFunction(
            _builder.getBuiltinFunction(*setExitCb), { returnValue }, "setvar.exitstatus");

    _builder.createRet(returnValue);
}

CoreVM::Value* IRGenerator::toBool(CoreVM::Value* value)
{
    if (value->type() == CoreVM::LiteralType::Boolean)
        return value;
    return _builder.createNCmpEQ(value, _builder.get(CoreVM::CoreNumber(0)));
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
    _builder.createCallFunction(
        _builder.getBuiltinFunction(*cmdStartCallback), { _builder.get(programName) }, "cmd_start");

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

        _builder.createCallFunction(_builder.getBuiltinFunction(*cmdArgCallback), { value }, "cmd_arg");
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
    return _builder.createCallFunction(_builder.getBuiltinFunction(*cmdExecCallback), {}, "cmd_exec");
}

CoreVM::Value* IRGenerator::execBuiltCommandPiped(bool lastInChain)
{
    auto* cmdExecCallback = findCallback("internal.cmd_exec_piped(B)I");
    if (!cmdExecCallback)
    {
        reportTypeError("Internal error: internal.cmd_exec_piped builtin not found");
        return nullptr;
    }
    return _builder.createCallFunction(
        _builder.getBuiltinFunction(*cmdExecCallback), { _builder.get(lastInChain) }, "cmd_exec_piped");
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

    return _builder.createCallFunction(_builder.getBuiltinFunction(*cmdExecCallback),
                                       { _builder.get(command) },
                                       "cmd_exec_piped_background");
}

void IRGenerator::generatePrintCall(ast::Expr const* argument, bool appendNewline)
{
    TRACE_SCOPE("generatePrintCall");

    // Evaluate the argument
    CoreVM::Value* argValue = codegen(argument);
    if (!argValue)
    {
        reportTypeError("Failed to evaluate print argument");
        return;
    }

    // Convert to string if needed
    if (argValue->type() == CoreVM::LiteralType::Number)
    {
        argValue = _builder.createN2S(argValue, "print.n2s");
    }
    else if (argValue->type() == CoreVM::LiteralType::Float)
    {
        argValue = _builder.createF2S(argValue, "print.f2s");
    }
    else if (argValue->type() == CoreVM::LiteralType::Boolean)
    {
        // Convert boolean to "true"/"false" string via conditional branch
        auto* trueBlock = _builder.createBlock("print.b2s.true");
        auto* falseBlock = _builder.createBlock("print.b2s.false");
        auto* mergeBlock = _builder.createBlock("print.b2s.merge");
        auto* storage = createAllocaInEntryBlock(CoreVM::LiteralType::String, "print.b2s.tmp");
        _builder.createCondBr(argValue, trueBlock, falseBlock);
        _builder.setInsertPoint(trueBlock);
        _builder.createStore(storage, _builder.get("true"));
        _builder.createBr(mergeBlock);
        _builder.setInsertPoint(falseBlock);
        _builder.createStore(storage, _builder.get("false"));
        _builder.createBr(mergeBlock);
        _builder.setInsertPoint(mergeBlock);
        argValue = _builder.createLoad(storage, "print.b2s");
    }
    else if (argValue->type() == CoreVM::LiteralType::Void || argValue->type() == CoreVM::LiteralType::Object)
    {
        // Dynamically-typed value (e.g., from pattern matching or OGETSLOT)
        argValue = _builder.createN2S(argValue, "print.n2s");
    }
    else if (argValue->type() != CoreVM::LiteralType::String)
    {
        reportTypeError("print/println requires a string or number argument");
        return;
    }

    // Find the appropriate native callback (using short signature format)
    std::string signature = appendNewline ? "println(S)V" : "print(S)V";

    auto* callback = findCallback(signature);
    if (!callback)
    {
        if (appendNewline)
            reportTypeError("println builtin not available");
        else
            reportTypeError("print builtin not available");
        return;
    }

    // Generate native call
    std::string funcName = appendNewline ? "println" : "print";
    _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { argValue }, funcName);
    _result = nullptr; // print/println returns void
}

bool IRGenerator::tryGenerateBuiltinCall(std::string const& name,
                                         std::vector<ast::Expr const*> const& argExprs)
{
    if (name == "fst" || name == "snd")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError(
                "{} requires exactly 1 argument, got {}", std::string_view(name), argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate {} argument", std::string_view(name));
            return true;
        }
        auto slotIndex = (name == "fst") ? 0 : 1;
        _result = _builder.createObjGetSlot(argVal, _builder.get(CoreVM::CoreNumber(slotIndex)), name);
        return true;
    }

    if (name == "string_length")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("string_length requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate string_length argument");
            return true;
        }
        if (argVal->type() != CoreVM::LiteralType::String)
        {
            reportTypeError("string_length requires a string argument");
            return true;
        }
        _result = _builder.createSLen(argVal, "slen");
        return true;
    }

    if (name == "int_of_string")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("int_of_string requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate int_of_string argument");
            return true;
        }
        _result = _builder.createS2N(argVal, "s2n");
        return true;
    }

    if (name == "string_of_int")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("string_of_int requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate string_of_int argument");
            return true;
        }
        _result = _builder.createN2S(argVal, "n2s");
        return true;
    }

    if (name == "not")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("not requires exactly 1 argument, got {}", argExprs.size());
            return true;
        }
        auto* argVal = codegen(argExprs[0]);
        if (!argVal)
        {
            reportTypeError("Failed to evaluate not argument");
            return true;
        }
        _result = _builder.createBNot(toBool(argVal), "not");
        return true;
    }

    return false;
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
    callArguments.insert(callArguments.begin(), _builder.get(programName));
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

bool IRGenerator::needsDynamicCompare(CoreVM::Value* lhs, CoreVM::Value* rhs) const
{
    // Check if either operand has a dynamic type (from OGETSLOT or similar)
    return lhs->type() == CoreVM::LiteralType::Void || lhs->type() == CoreVM::LiteralType::Object
           || rhs->type() == CoreVM::LiteralType::Void || rhs->type() == CoreVM::LiteralType::Object;
}

// ============================================================================
// F# Style Expressions and Statements (Stubs)
// ============================================================================
// F# Phase 2 expressions: if-then-else, tuples, mutable assignment

void IRGenerator::visit(ast::IfExpr const& node)
{
    TRACE_SCOPE("visit(IfExpr)");

    // Allocate result storage in entry block
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "if.result");

    // Codegen condition
    auto* condValue = codegen(node.condition.get());
    if (!condValue)
    {
        reportTypeError("Failed to generate code for if condition");
        return;
    }
    auto* condBool = toBool(condValue);

    // Create basic blocks
    auto* thenBlock = _builder.createBlock("if.then");
    auto* elseBlock = _builder.createBlock("if.else");
    auto* mergeBlock = _builder.createBlock("if.merge");

    _builder.createCondBr(condBool, thenBlock, elseBlock);

    // Then branch
    _builder.setInsertPoint(thenBlock);
    auto* thenResult = codegen(node.thenExpr.get());
    if (thenResult)
    {
        _builder.createStore(resultStorage, thenResult, "if.then.store");
        _builder.createBr(mergeBlock);
    }
    else if (_activeRecursion || _activeMutualRecursion)
    {
        // Tail call in then branch — no merge needed from this path
    }
    else
    {
        reportTypeError("Failed to generate code for if-then branch");
        return;
    }

    // Else branch
    _builder.setInsertPoint(elseBlock);
    auto* elseResult = codegen(node.elseExpr.get());
    if (elseResult)
    {
        _builder.createStore(resultStorage, elseResult, "if.else.store");
        _builder.createBr(mergeBlock);
    }
    else if (_activeRecursion || _activeMutualRecursion)
    {
        // Tail call in else branch — no merge needed from this path
    }
    else
    {
        reportTypeError("Failed to generate code for if-else branch");
        return;
    }

    // Merge block: load result
    _builder.setInsertPoint(mergeBlock);
    _result = _builder.createLoad(resultStorage, "if.result");
}

void IRGenerator::visit(ast::TupleExpr const& node)
{
    TRACE_SCOPE("visit(TupleExpr)");

    if (node.elements.size() < 2 || node.elements.size() > 3)
    {
        reportTypeError("Tuples must have 2 or 3 elements, got {}", node.elements.size());
        return;
    }

    // Determine the type ID
    auto typeId = node.elements.size() == 2 ? CoreVM::BuiltinTypeId::Tuple2 : CoreVM::BuiltinTypeId::Tuple3;

    // Codegen all elements
    std::vector<CoreVM::Value*> elemValues;
    for (auto const& elem: node.elements)
    {
        auto* val = codegen(elem.get());
        if (!val)
        {
            reportTypeError("Failed to generate code for tuple element");
            return;
        }
        elemValues.push_back(val);
    }

    // Allocate the tuple object
    auto* obj = _builder.createObjAlloc(_builder.get(CoreVM::CoreNumber(typeId)), "tuple");

    // Set each slot
    for (size_t i = 0; i < elemValues.size(); ++i)
    {
        _builder.createObjSetSlot(obj, _builder.get(CoreVM::CoreNumber(i)), elemValues[i], "tuple.slot");
    }

    _result = obj;
}

void IRGenerator::visit(ast::MutAssignStmt const& node)
{
    TRACE_SCOPE("visit(MutAssignStmt)");

    // Look up the binding
    auto const* binding = lookupFSharpBinding(node.name);
    if (!binding)
    {
        reportTypeError("Undefined variable: {}", std::string_view(node.name));
        return;
    }

    if (!binding->isMutable)
    {
        reportTypeError(
            "Cannot assign to immutable variable '{}'. Use 'let mut' to declare mutable variables.",
            std::string_view(node.name));
        return;
    }

    // Codegen the new value
    auto* newValue = codegen(node.value.get());
    if (!newValue)
    {
        reportTypeError("Failed to generate code for assignment value");
        return;
    }

    // Store the new value
    _builder.createStore(binding->value, newValue, node.name + ".assign");
    _result = nullptr;
}

// ============================================================================
// These are placeholder implementations. Full implementation will be added
// in a future iteration once the type system and evaluation strategy are finalized.

void IRGenerator::visit(ast::LetBindingStmt const& node)
{
    TRACE_SCOPE("visit(LetBindingStmt)");

    if (node.isFunction())
    {
        // Function definition: let add x y = x + y
        // Store the function for later inlining during application
        // We don't compile it now - we'll inline the body when called

        // For mutual recursion (let rec f ... and g ...), register all names first
        // so that captured-variable analysis can see sibling functions
        auto allRecNames = std::vector<std::string> {};
        if (node.isRecursive)
        {
            allRecNames.push_back(node.name);
            for (auto const& ab: node.andBindings)
                allRecNames.push_back(ab.name);
        }

        auto const isMutual = allRecNames.size() > 1;

        // Register the primary function
        {
            FSharpFunction func;
            func.parameters = node.parameters;
            func.body = node.value.get();
            func.returnsResultOrOption = isBodyResultOrOption(func.body);
            func.isRecursive = node.isRecursive;
            if (isMutual)
                func.mutualGroup = allRecNames;

            auto allBound = node.parameters;
            for (auto const& rn: allRecNames)
                allBound.push_back(rn);
            func.capturedBindings = collectFreeVariables(func.body, allBound);

            registerFSharpFunction(node.name, std::move(func));
        }

        // Register 'and' bindings (mutual recursion partners)
        for (auto const& ab: node.andBindings)
        {
            FSharpFunction func;
            func.parameters = ab.parameters;
            func.body = ab.value.get();
            func.returnsResultOrOption = isBodyResultOrOption(func.body);
            func.isRecursive = true;
            if (isMutual)
                func.mutualGroup = allRecNames;

            auto allBound = ab.parameters;
            for (auto const& rn: allRecNames)
                allBound.push_back(rn);
            func.capturedBindings = collectFreeVariables(func.body, allBound);

            registerFSharpFunction(ab.name, std::move(func));
        }

        _result = nullptr;
        return;
    }

    // Simple binding: let x = expr
    // Special case: let f = fun x -> x * 2 (lambda assigned to variable)
    // We register the lambda as a function under the variable name
    if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(node.value.get()))
    {
        FSharpFunction func;
        func.parameters = lambda->parameters;
        func.body = lambda->body.get();
        func.returnsResultOrOption = isBodyResultOrOption(func.body);
        func.capturedBindings = collectFreeVariables(func.body, func.parameters);
        registerFSharpFunction(node.name, std::move(func));
        _result = nullptr;
        return;
    }

    // Check if the expression produces an object (Option/Result/Tuple)
    // These need special tracking for reference counting
    bool isObjectExpr = dynamic_cast<ast::OptionExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::ResultExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::TryExpr const*>(node.value.get()) != nullptr
                        || dynamic_cast<ast::TupleExpr const*>(node.value.get()) != nullptr;

    // Codegen the value expression
    CoreVM::Value* value = codegen(node.value.get());
    if (!value)
    {
        reportTypeError("Failed to generate code for let binding value");
        return;
    }

    // Check if the value is a function reference (string constant naming a function).
    // This handles: let add5 = add 5  (partial application returns "__lambda_0")
    //               let g = f          (function-as-value returns "f")
    if (value->type() == CoreVM::LiteralType::String)
    {
        if (auto* strConst = dynamic_cast<CoreVM::ConstantString*>(value))
        {
            if (auto const* srcFunc = lookupFSharpFunction(strConst->get()))
            {
                registerFSharpFunction(node.name, *srcFunc);
                _result = nullptr;
                return;
            }
        }
    }

    // Determine the type for storage
    CoreVM::LiteralType storageType = value->type();

    // Create storage (alloca) for the variable in the entry block
    // This ensures proper stack tracking in TargetCodeGenerator even if the value
    // expression created new blocks (like match expressions)
    CoreVM::AllocaInstr* storage = createAllocaInEntryBlock(storageType, node.name);

    // Store the value
    _builder.createStore(storage, value, node.name);

    // Register in F# scope - track objects for ORELEASE at scope exit
    if (isObjectExpr)
    {
        bindFSharpObjectVariable(node.name, storage, node.isMutable);
    }
    else
    {
        bindFSharpVariable(node.name, storage, node.isMutable);
    }

    // Let bindings as statements don't produce a result value
    _result = nullptr;
}

void IRGenerator::visit(ast::LetInExpr const& node)
{
    TRACE_SCOPE("visit(LetInExpr)");

    pushFSharpScope();

    if (node.isFunction())
    {
        // Function binding: let f x = body in expr
        FSharpFunction func;
        func.parameters = node.parameters;
        func.body = node.value.get();
        func.returnsResultOrOption = isBodyResultOrOption(func.body);
        func.isRecursive = node.isRecursive;
        func.capturedBindings = collectFreeVariables(func.body, func.parameters);

        registerFSharpFunction(node.name, std::move(func));
    }
    else
    {
        // Simple binding: let x = expr in body
        auto* value = codegen(node.value.get());
        if (!value)
        {
            popFSharpScope();
            reportTypeError("Failed to evaluate let-in binding value");
            return;
        }

        auto* storage = createAllocaInEntryBlock(value->type(), node.name);
        _builder.createStore(storage, value, node.name + ".store");
        bindFSharpVariable(node.name, storage);
    }

    // Evaluate the body expression with the binding in scope
    _result = codegen(node.body.get());

    popFSharpScope();
}

void IRGenerator::visit(ast::ExprStmt const& node)
{
    TRACE_SCOPE("visit(ExprStmt)");
    // Expression statement: evaluate the expression for its side effects
    // The result is discarded
    codegen(node.expr.get());
    _result = nullptr;
}

void IRGenerator::visit(ast::BinaryExpr const& node)
{
    TRACE_SCOPE("visit(BinaryExpr)");

    // Codegen both operands
    CoreVM::Value* left = codegen(node.left.get());
    if (!left)
    {
        if (_activeRecursion)
        {
            reportTypeError("Non-tail recursive call detected. Recursive calls must be in tail position. "
                            "Use an accumulator parameter to restructure the recursion.");
        }
        return;
    }

    CoreVM::Value* right = codegen(node.right.get());
    if (!right)
    {
        if (_activeRecursion)
        {
            reportTypeError("Non-tail recursive call detected. Recursive calls must be in tail position. "
                            "Use an accumulator parameter to restructure the recursion.");
        }
        return;
    }

    // String concatenation: if + operator and either operand is a string, concat
    if (node.op == ast::BinaryOp::Add
        && (left->type() == CoreVM::LiteralType::String || right->type() == CoreVM::LiteralType::String))
    {
        if (left->type() == CoreVM::LiteralType::Float)
            left = _builder.createF2S(left);
        else if (left->type() != CoreVM::LiteralType::String)
            left = _builder.createN2S(left);
        if (right->type() == CoreVM::LiteralType::Float)
            right = _builder.createF2S(right);
        else if (right->type() != CoreVM::LiteralType::String)
            right = _builder.createN2S(right);
        _result = _builder.createSAdd(left, right, "concat");
        return;
    }

    // Float promotion: if either operand is Float, promote the other to Float
    auto const isFloat = [](CoreVM::Value* v) {
        return v->type() == CoreVM::LiteralType::Float;
    };
    if (isFloat(left) || isFloat(right))
    {
        if (!isFloat(left))
        {
            if (left->type() == CoreVM::LiteralType::String)
                left = _builder.createS2F(left);
            else
                left = _builder.createN2F(left);
        }
        if (!isFloat(right))
        {
            if (right->type() == CoreVM::LiteralType::String)
                right = _builder.createS2F(right);
            else
                right = _builder.createN2F(right);
        }

        switch (node.op)
        {
            case ast::BinaryOp::Add: _result = _builder.createFAdd(left, right, "fadd"); break;
            case ast::BinaryOp::Sub: _result = _builder.createFSub(left, right, "fsub"); break;
            case ast::BinaryOp::Mul: _result = _builder.createFMul(left, right, "fmul"); break;
            case ast::BinaryOp::Div: _result = _builder.createFDiv(left, right, "fdiv"); break;
            case ast::BinaryOp::Mod: _result = _builder.createFRem(left, right, "fmod"); break;
            case ast::BinaryOp::Pow: _result = _builder.createFPow(left, right, "fpow"); break;
            case ast::BinaryOp::Eq: _result = _builder.createFCmpEQ(left, right, "feq"); break;
            case ast::BinaryOp::Ne: _result = _builder.createFCmpNE(left, right, "fne"); break;
            case ast::BinaryOp::Lt: _result = _builder.createFCmpLT(left, right, "flt"); break;
            case ast::BinaryOp::Le: _result = _builder.createFCmpLE(left, right, "fle"); break;
            case ast::BinaryOp::Gt: _result = _builder.createFCmpGT(left, right, "fgt"); break;
            case ast::BinaryOp::Ge: _result = _builder.createFCmpGE(left, right, "fge"); break;
            case ast::BinaryOp::And: _result = _builder.createBAnd(toBool(left), toBool(right), "and"); break;
            case ast::BinaryOp::Or: _result = _builder.createBOr(toBool(left), toBool(right), "or"); break;
        }
        return;
    }

    // For arithmetic and comparison, ensure operands are numbers
    if (left->type() == CoreVM::LiteralType::String)
        left = _builder.createS2N(left);
    if (right->type() == CoreVM::LiteralType::String)
        right = _builder.createS2N(right);

    switch (node.op)
    {
        // Arithmetic operators
        case ast::BinaryOp::Add: _result = _builder.createAdd(left, right, "add"); break;
        case ast::BinaryOp::Sub: _result = _builder.createSub(left, right, "sub"); break;
        case ast::BinaryOp::Mul: _result = _builder.createMul(left, right, "mul"); break;
        case ast::BinaryOp::Div: _result = _builder.createDiv(left, right, "div"); break;
        case ast::BinaryOp::Mod: _result = _builder.createRem(left, right, "mod"); break;
        case ast::BinaryOp::Pow: _result = _builder.createPow(left, right, "pow"); break;

        // Comparison operators (return boolean)
        // Use dynamic comparison (VCmpXX) when operands have unknown compile-time types
        case ast::BinaryOp::Eq:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpEQ(left, right, "eq")
                                                       : _builder.createNCmpEQ(left, right, "eq");
            break;
        case ast::BinaryOp::Ne:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpNE(left, right, "ne")
                                                       : _builder.createNCmpNE(left, right, "ne");
            break;
        case ast::BinaryOp::Lt:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpLT(left, right, "lt")
                                                       : _builder.createNCmpLT(left, right, "lt");
            break;
        case ast::BinaryOp::Le:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpLE(left, right, "le")
                                                       : _builder.createNCmpLE(left, right, "le");
            break;
        case ast::BinaryOp::Gt:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpGT(left, right, "gt")
                                                       : _builder.createNCmpGT(left, right, "gt");
            break;
        case ast::BinaryOp::Ge:
            _result = needsDynamicCompare(left, right) ? _builder.createVCmpGE(left, right, "ge")
                                                       : _builder.createNCmpGE(left, right, "ge");
            break;

        // Logical operators
        case ast::BinaryOp::And: _result = _builder.createBAnd(toBool(left), toBool(right), "and"); break;
        case ast::BinaryOp::Or: _result = _builder.createBOr(toBool(left), toBool(right), "or"); break;
    }
}

void IRGenerator::visit(ast::UnaryExpr const& node)
{
    TRACE_SCOPE("visit(UnaryExpr)");

    CoreVM::Value* operand = codegen(node.operand.get());
    if (!operand)
        return;

    switch (node.op)
    {
        case ast::UnaryOp::Neg:
            if (operand->type() == CoreVM::LiteralType::Float)
                _result = _builder.createFNeg(operand, "fneg");
            else
            {
                // Ensure operand is a number for negation
                if (operand->type() == CoreVM::LiteralType::String)
                    operand = _builder.createS2N(operand);
                _result = _builder.createNeg(operand, "neg");
            }
            break;

        case ast::UnaryOp::Not: _result = _builder.createBNot(toBool(operand), "not"); break;
    }
}

void IRGenerator::visit(ast::PipelineExpr const& node)
{
    TRACE_SCOPE("visit(PipelineExpr)");

    // Pipeline: value |> function
    // This is syntactic sugar for function application: f(value)
    // value |> f is equivalent to f value

    // Evaluate the value (left-hand side)
    CoreVM::Value* value = codegen(node.value.get());
    if (!value)
    {
        reportTypeError("Failed to evaluate pipeline value");
        return;
    }

    // The function (right-hand side) can be:
    // 1. An identifier (named function): 5 |> double
    // 2. A lambda expression: 5 |> (fun x -> x * 2)
    // 3. A parenthesized lambda: 5 |> (fun x -> x * 2)
    FSharpFunction const* func = nullptr;
    std::string funcName;

    // Unwrap ParenExpr if present
    ast::Expr const* funcExpr = node.function.get();
    while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(funcExpr))
        funcExpr = paren->inner.get();

    if (auto const* funcIdent = dynamic_cast<ast::IdentifierExpr const*>(funcExpr))
    {
        // Check for builtin functions first (fst, snd, string_length, etc.)
        // For pipelines, we need to pass the piped value as the single argument
        if (funcIdent->name == "print" || funcIdent->name == "println")
        {
            // Special case: pipe to print/println
            // Convert value to string if needed
            CoreVM::Value* argValue = value;
            if (argValue->type() == CoreVM::LiteralType::Number)
                argValue = _builder.createN2S(argValue, "pipe.n2s");
            else if (argValue->type() == CoreVM::LiteralType::Void
                     || argValue->type() == CoreVM::LiteralType::Object)
                argValue = _builder.createN2S(argValue, "pipe.n2s");

            auto* callback = findCallback(funcIdent->name == "println" ? "println(S)V" : "print(S)V");
            if (callback)
            {
                _builder.createCallFunction(
                    _builder.getBuiltinFunction(*callback), { argValue }, funcIdent->name);
            }
            _result = nullptr;
            return;
        }

        // Check other builtins
        // Build a temporary argument expression list pointing to a synthetic node.
        // Since builtins codegen their args, and we already have the value, we use
        // a different approach: codegen the value manually for builtins.
        if (funcIdent->name == "fst" || funcIdent->name == "snd")
        {
            auto slotIndex = (funcIdent->name == "fst") ? 0 : 1;
            _result = _builder.createObjGetSlot(
                value, _builder.get(CoreVM::CoreNumber(slotIndex)), funcIdent->name);
            return;
        }
        if (funcIdent->name == "string_length")
        {
            if (value->type() != CoreVM::LiteralType::String)
            {
                reportTypeError("string_length requires a string argument");
                return;
            }
            _result = _builder.createSLen(value, "slen");
            return;
        }
        if (funcIdent->name == "int_of_string")
        {
            _result = _builder.createS2N(value, "s2n");
            return;
        }
        if (funcIdent->name == "string_of_int")
        {
            _result = _builder.createN2S(value, "n2s");
            return;
        }
        if (funcIdent->name == "not")
        {
            _result = _builder.createBNot(toBool(value), "not");
            return;
        }

        // Named function or stored lambda
        funcName = funcIdent->name;
        func = lookupFSharpFunction(funcName);
        if (!func)
        {
            reportTypeError("Undefined function in pipeline: {}", std::string_view(funcName));
            return;
        }
    }
    else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(funcExpr))
    {
        // Lambda expression - register it as an anonymous function
        funcName = generateLambdaName();
        FSharpFunction lambdaFunc;
        lambdaFunc.parameters = lambda->parameters;
        lambdaFunc.body = lambda->body.get();
        lambdaFunc.returnsResultOrOption = isBodyResultOrOption(lambdaFunc.body);
        lambdaFunc.capturedBindings = collectFreeVariables(lambdaFunc.body, lambdaFunc.parameters);
        registerFSharpFunction(funcName, std::move(lambdaFunc));
        func = lookupFSharpFunction(funcName);
    }
    else if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(funcExpr))
    {
        // Partial application in pipeline: value |> func arg
        // Flatten the application to get base function + explicit args
        std::vector<ast::Expr const*> explicitArgExprs;
        ast::Expr const* base = funcExpr;
        while (auto const* innerApp = dynamic_cast<ast::ApplicationExpr const*>(base))
        {
            explicitArgExprs.push_back(innerApp->argument.get());
            base = innerApp->function.get();
        }
        std::reverse(explicitArgExprs.begin(), explicitArgExprs.end());

        // Unwrap parens
        while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(base))
            base = paren->inner.get();

        auto const* baseIdent = dynamic_cast<ast::IdentifierExpr const*>(base);
        if (!baseIdent)
        {
            reportTypeError("Pipeline partial application requires a named function");
            return;
        }

        auto const* baseFunc = lookupFSharpFunction(baseIdent->name);
        if (!baseFunc)
        {
            reportTypeError("Undefined function in pipeline: {}", std::string_view(baseIdent->name));
            return;
        }

        // Total args = explicit args + piped value (last parameter)
        if (explicitArgExprs.size() + 1 != baseFunc->arity())
        {
            reportTypeError("Pipeline function '{}' expects {} arguments, got {} (including piped value)",
                            std::string_view(baseIdent->name),
                            baseFunc->arity(),
                            explicitArgExprs.size() + 1);
            return;
        }

        // Evaluate explicit args
        std::vector<CoreVM::Value*> allArgs;
        for (auto const* argExpr: explicitArgExprs)
        {
            auto* argVal = codegen(argExpr);
            if (!argVal)
            {
                reportTypeError("Failed to evaluate pipeline argument");
                return;
            }
            allArgs.push_back(argVal);
        }
        // Piped value is the last argument
        allArgs.push_back(value);

        // Inline the function body with all arguments
        funcName = baseIdent->name;
        func = baseFunc;

        pushFSharpScope();

        // Re-bind captured variables
        for (auto const& [name, storage]: func->capturedBindings)
            bindFSharpVariable(name, storage);

        CoreVM::BasicBlock* returnBlock = nullptr;
        CoreVM::AllocaInstr* returnStorage = nullptr;
        if (func->returnsResultOrOption)
        {
            returnBlock = _builder.createBlock("pipe.return");
            returnStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "pipe.result");
            pushFSharpFunctionContext(returnBlock, returnStorage, true);
        }

        for (size_t i = 0; i < func->parameters.size(); ++i)
        {
            auto storageType = allArgs[i]->type();
            auto* paramStorage =
                _builder.createAlloca(storageType, _builder.get(CoreVM::CoreNumber(1)), func->parameters[i]);
            _builder.createStore(paramStorage, allArgs[i], func->parameters[i]);
            bindFSharpVariable(func->parameters[i], paramStorage);
        }

        auto* bodyResult = codegen(func->body);

        if (func->returnsResultOrOption)
        {
            if (bodyResult)
            {
                _builder.createStore(returnStorage, bodyResult, "store.result");
                _builder.createBr(returnBlock);
            }
            _builder.setInsertPoint(returnBlock);
            _result = _builder.createLoad(returnStorage, "load.result");
            popFSharpFunctionContext();
        }
        else
        {
            _result = bodyResult;
        }

        popFSharpScope();
        return;
    }
    else
    {
        reportTypeError("Pipeline function must be an identifier, lambda, or partial application");
        return;
    }

    // For pipeline, the value becomes the first (and for now, only) argument
    if (func->arity() != 1)
    {
        reportTypeError("Pipeline function '{}' must take exactly 1 argument, got {}",
                        std::string_view(funcName),
                        func->arity());
        return;
    }

    // Handle recursive function via pipeline (e.g., 10 |> countdown)
    if (func->isRecursive)
    {
        // Reuse the same loop-based compilation as ApplicationExpr Case A
        auto* entryBlock = _builder.createBlock("rec.entry");
        auto* exitBlock = _builder.createBlock("rec.exit");

        auto* paramAlloca = createAllocaInEntryBlock(value->type(), "rec.param." + func->parameters[0]);
        _builder.createStore(paramAlloca, value, "rec.param.init");

        auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "rec.result");

        _activeRecursion = RecursiveCallContext {
            .functionName = funcName,
            .entryBlock = entryBlock,
            .paramAllocas = { paramAlloca },
            .resultStorage = resultStorage,
            .exitBlock = exitBlock,
        };

        _builder.createBr(entryBlock);
        _builder.setInsertPoint(entryBlock);

        pushFSharpScope();
        for (auto const& [capName, capStorage]: func->capturedBindings)
            bindFSharpVariable(capName, capStorage);
        bindFSharpVariable(func->parameters[0], paramAlloca);

        auto* bodyResult = codegen(func->body);
        if (bodyResult)
        {
            _builder.createStore(resultStorage, bodyResult, "rec.store.result");
            _builder.createBr(exitBlock);
        }

        popFSharpScope();

        _builder.setInsertPoint(exitBlock);
        _result = _builder.createLoad(resultStorage, "rec.load.result");

        _activeRecursion.reset();
        return;
    }

    // Non-recursive: inline the function body with the piped value as argument
    pushFSharpScope();

    // Re-bind captured variables from the closure
    for (auto const& [capName, capStorage]: func->capturedBindings)
        bindFSharpVariable(capName, capStorage);

    // Only set up return infrastructure for functions that return Result/Option
    CoreVM::BasicBlock* returnBlock = nullptr;
    CoreVM::AllocaInstr* returnStorage = nullptr;

    if (func->returnsResultOrOption)
    {
        returnBlock = _builder.createBlock("pipe.return");
        returnStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "pipe.result");
        pushFSharpFunctionContext(returnBlock, returnStorage, true);
    }

    // Bind the piped value to the parameter
    CoreVM::LiteralType storageType = value->type();
    CoreVM::AllocaInstr* storage =
        _builder.createAlloca(storageType, _builder.get(CoreVM::CoreNumber(1)), func->parameters[0]);
    _builder.createStore(storage, value, func->parameters[0]);
    bindFSharpVariable(func->parameters[0], storage);

    // Inline the function body
    CoreVM::Value* bodyResult = codegen(func->body);

    if (func->returnsResultOrOption)
    {
        // Store result and jump to return block (normal path)
        if (bodyResult)
        {
            _builder.createStore(returnStorage, bodyResult, "store.result");
            _builder.createBr(returnBlock);
        }

        // Continue from return block (merges normal and early return paths)
        _builder.setInsertPoint(returnBlock);
        _result = _builder.createLoad(returnStorage, "load.result");

        popFSharpFunctionContext();
    }
    else
    {
        // Simple case: no error propagation
        _result = bodyResult;
    }

    popFSharpScope();
}

void IRGenerator::visit(ast::ApplicationExpr const& node)
{
    TRACE_SCOPE("visit(ApplicationExpr)");

    // Function application: f x y is parsed as ApplicationExpr(ApplicationExpr(f, x), y)
    // We need to flatten this to get the function name and all arguments

    // Collect arguments in reverse order (innermost first)
    std::vector<ast::Expr const*> argExprs;
    ast::Expr const* current = &node;

    while (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(current))
    {
        argExprs.push_back(app->argument.get());
        current = app->function.get();
    }

    // Reverse to get arguments in correct order (first arg first)
    std::reverse(argExprs.begin(), argExprs.end());

    // Unwrap ParenExpr if present
    while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(current))
        current = paren->inner.get();

    // Check for builtin print/println functions
    if (auto const* funcIdent = dynamic_cast<ast::IdentifierExpr const*>(current))
    {
        if (funcIdent->name == "print" || funcIdent->name == "println")
        {
            if (argExprs.size() != 1)
            {
                reportTypeError("{} requires exactly one string argument", std::string_view(funcIdent->name));
                return;
            }
            generatePrintCall(argExprs[0], funcIdent->name == "println");
            return;
        }

        // Check for standard library builtins (fst, snd, string_length, etc.)
        if (tryGenerateBuiltinCall(funcIdent->name, argExprs))
            return;
    }

    // Evaluate all arguments
    std::vector<CoreVM::Value*> args;
    for (ast::Expr const* argExpr: argExprs)
    {
        CoreVM::Value* argValue = codegen(argExpr);
        if (!argValue)
        {
            reportTypeError("Failed to evaluate function argument");
            return;
        }
        args.push_back(argValue);
    }

    // The base can be:
    // 1. An identifier (named function): double 5
    // 2. A lambda expression: (fun x -> x * 2) 5
    FSharpFunction const* func = nullptr;
    std::string funcName;

    if (auto const* funcIdent = dynamic_cast<ast::IdentifierExpr const*>(current))
    {
        // Named function or stored lambda
        funcName = funcIdent->name;
        func = lookupFSharpFunction(funcName);
        if (!func)
        {
            reportTypeError("Undefined function: {}", std::string_view(funcName));
            return;
        }
    }
    else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(current))
    {
        // Lambda expression - register it as an anonymous function
        funcName = generateLambdaName();
        FSharpFunction lambdaFunc;
        lambdaFunc.parameters = lambda->parameters;
        lambdaFunc.body = lambda->body.get();
        lambdaFunc.returnsResultOrOption = isBodyResultOrOption(lambdaFunc.body);
        lambdaFunc.capturedBindings = collectFreeVariables(lambdaFunc.body, lambdaFunc.parameters);
        registerFSharpFunction(funcName, std::move(lambdaFunc));
        func = lookupFSharpFunction(funcName);
    }
    else
    {
        reportTypeError("Function application requires a function name or lambda");
        return;
    }

    // Check arity: over-application is an error, under-application creates partial application
    if (args.size() > func->arity())
    {
        reportTypeError("Function '{}' expects {} arguments, got {}",
                        std::string_view(funcName),
                        func->arity(),
                        args.size());
        return;
    }

    if (args.size() < func->arity())
    {
        // Partial application: create a new function with remaining parameters
        std::unordered_map<std::string, CoreVM::Value*> newCaptures = func->capturedBindings;
        for (size_t i = 0; i < args.size(); ++i)
        {
            auto const& paramName = func->parameters[i];
            auto* alloca = createAllocaInEntryBlock(args[i]->type(), "partial." + paramName);
            _builder.createStore(alloca, args[i], "partial.store." + paramName);
            newCaptures[paramName] = alloca;
        }

        auto partialName = generateLambdaName();
        FSharpFunction partialFunc;
        partialFunc.parameters = { func->parameters.begin() + static_cast<ptrdiff_t>(args.size()),
                                   func->parameters.end() };
        partialFunc.body = func->body;
        partialFunc.returnsResultOrOption = func->returnsResultOrOption;
        partialFunc.isRecursive = false;
        partialFunc.capturedBindings = std::move(newCaptures);

        registerFSharpFunction(partialName, std::move(partialFunc));
        _result = _builder.get(partialName);
        return;
    }

    // Handle recursive function calls (let rec)
    if (func->isRecursive)
    {
        // === Mutual recursion: dispatch-loop approach ===
        if (!func->mutualGroup.empty())
        {
            // Case B (mutual): Tail-call from within a mutual recursion body
            if (_activeMutualRecursion)
            {
                if (auto const* slot = _activeMutualRecursion->findFunction(funcName))
                {
                    // Store new argument values into the target function's param allocas
                    for (size_t i = 0; i < args.size(); ++i)
                        _builder.createStore(slot->paramAllocas[i], args[i], "mutual.arg.update");

                    // Set dispatch tag to route to the target function
                    _builder.createStore(_activeMutualRecursion->dispatchTag,
                                         _builder.get(CoreVM::CoreNumber(slot->dispatchIndex)),
                                         "mutual.dispatch.update");

                    // Jump back to dispatch loop entry
                    _builder.createBr(_activeMutualRecursion->dispatchBlock);

                    // Create unreachable continuation block (code after tail call is dead)
                    auto* unreachable = _builder.createBlock("mutual.unreachable");
                    _builder.setInsertPoint(unreachable);

                    _result = nullptr;
                    return;
                }
            }

            // Case A (mutual): First external call — set up dispatch loop
            auto* dispatchBlock = _builder.createBlock("mutual.dispatch");
            auto* exitBlock = _builder.createBlock("mutual.exit");
            auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "mutual.result");
            auto* dispatchTag = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "mutual.tag");

            // Build the mutual recursion context with param allocas for every function
            MutualRecursionContext ctx;
            ctx.dispatchBlock = dispatchBlock;
            ctx.exitBlock = exitBlock;
            ctx.resultStorage = resultStorage;
            ctx.dispatchTag = dispatchTag;

            int calledIndex = -1;
            for (size_t i = 0; i < func->mutualGroup.size(); ++i)
            {
                auto const& fnName = func->mutualGroup[i];
                auto const* fn = lookupFSharpFunction(fnName);

                MutualRecursionContext::FunctionSlot slot;
                slot.name = fnName;
                slot.dispatchIndex = static_cast<int>(i);
                for (auto const& param: fn->parameters)
                {
                    auto* alloca = createAllocaInEntryBlock(CoreVM::LiteralType::Number,
                                                            "mutual." + fnName + "." + param);
                    slot.paramAllocas.push_back(alloca);
                }
                ctx.functions.push_back(std::move(slot));

                if (fnName == funcName)
                    calledIndex = static_cast<int>(i);
            }

            // Store initial dispatch tag and arguments for the called function
            _builder.createStore(
                dispatchTag, _builder.get(CoreVM::CoreNumber(calledIndex)), "mutual.tag.init");
            for (size_t i = 0; i < args.size(); ++i)
                _builder.createStore(ctx.functions[calledIndex].paramAllocas[i], args[i], "mutual.arg.init");

            _activeMutualRecursion = std::move(ctx);

            // Jump to the dispatch loop
            _builder.createBr(dispatchBlock);
            _builder.setInsertPoint(dispatchBlock);

            // Create body blocks for each function
            std::vector<CoreVM::BasicBlock*> bodyBlocks;
            for (auto const& fn: func->mutualGroup)
                bodyBlocks.push_back(_builder.createBlock("mutual.body." + fn));

            // Generate dispatch chain: tag == 0 → body[0], tag == 1 → body[1], ...
            auto* tagValue = _builder.createLoad(dispatchTag, "mutual.tag.load");
            for (size_t i = 0; i + 1 < func->mutualGroup.size(); ++i)
            {
                auto* nextCheck = _builder.createBlock("mutual.check." + std::to_string(i + 1));
                auto* cmp =
                    _builder.createNCmpEQ(tagValue, _builder.get(CoreVM::CoreNumber(static_cast<int>(i))));
                _builder.createCondBr(cmp, bodyBlocks[i], nextCheck);
                _builder.setInsertPoint(nextCheck);
            }
            // Last function: unconditional branch
            _builder.createBr(bodyBlocks.back());

            // Generate each function body
            for (size_t i = 0; i < func->mutualGroup.size(); ++i)
            {
                _builder.setInsertPoint(bodyBlocks[i]);
                auto const& fnName = func->mutualGroup[i];
                auto const* fn = lookupFSharpFunction(fnName);
                auto const& slot = _activeMutualRecursion->functions[i];

                pushFSharpScope();
                for (auto const& [capName, capStorage]: fn->capturedBindings)
                    bindFSharpVariable(capName, capStorage);
                for (size_t j = 0; j < fn->parameters.size(); ++j)
                    bindFSharpVariable(fn->parameters[j], slot.paramAllocas[j]);

                // Codegen body (recursive calls within will hit Case B above)
                auto* bodyResult = codegen(fn->body);

                if (bodyResult)
                {
                    _builder.createStore(resultStorage, bodyResult, "mutual.store.result");
                    _builder.createBr(exitBlock);
                }

                popFSharpScope();
            }

            // Continue from exit block
            _builder.setInsertPoint(exitBlock);
            _result = _builder.createLoad(resultStorage, "mutual.load.result");

            _activeMutualRecursion.reset();
            return;
        }

        // === Self-recursion: simple loop approach ===

        // Case B: Recursive tail-call from within body — jump back to entry block
        if (_activeRecursion && _activeRecursion->functionName == funcName)
        {
            // Store new argument values into parameter allocas
            for (size_t i = 0; i < args.size(); ++i)
                _builder.createStore(_activeRecursion->paramAllocas[i], args[i], "rec.arg.update");

            // Jump back to the entry block (tail-call as loop iteration)
            _builder.createBr(_activeRecursion->entryBlock);

            // Create unreachable continuation block (code after tail call is dead)
            auto* unreachable = _builder.createBlock("rec.unreachable");
            _builder.setInsertPoint(unreachable);

            // Signal tail call: result is nullptr (no value produced inline)
            _result = nullptr;
            return;
        }

        // Case A: First (external) call to recursive function — set up loop
        auto* entryBlock = _builder.createBlock("rec.entry");
        auto* exitBlock = _builder.createBlock("rec.exit");

        // Create parameter allocas and result storage in the handler entry block
        std::vector<CoreVM::AllocaInstr*> paramAllocas;
        for (size_t i = 0; i < func->parameters.size(); ++i)
        {
            auto* alloca = createAllocaInEntryBlock(args[i]->type(), "rec.param." + func->parameters[i]);
            _builder.createStore(alloca, args[i], "rec.param.init");
            paramAllocas.push_back(alloca);
        }

        auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "rec.result");

        // Set up the recursion context
        _activeRecursion = RecursiveCallContext {
            .functionName = funcName,
            .entryBlock = entryBlock,
            .paramAllocas = paramAllocas,
            .resultStorage = resultStorage,
            .exitBlock = exitBlock,
        };

        // Jump to entry block and begin the loop
        _builder.createBr(entryBlock);
        _builder.setInsertPoint(entryBlock);

        // Push scope and bind captures and parameters from allocas
        pushFSharpScope();
        for (auto const& [capName, capStorage]: func->capturedBindings)
            bindFSharpVariable(capName, capStorage);
        for (size_t i = 0; i < func->parameters.size(); ++i)
            bindFSharpVariable(func->parameters[i], paramAllocas[i]);

        // Codegen the function body (recursive calls will hit Case B above)
        auto* bodyResult = codegen(func->body);

        // If body produced a result (non-tail path), store it and branch to exit
        if (bodyResult)
        {
            _builder.createStore(resultStorage, bodyResult, "rec.store.result");
            _builder.createBr(exitBlock);
        }

        popFSharpScope();

        // Continue from the exit block, load the result
        _builder.setInsertPoint(exitBlock);
        _result = _builder.createLoad(resultStorage, "rec.load.result");

        // Clear the recursion context
        _activeRecursion.reset();
        return;
    }

    // Non-recursive function: inline the function body
    // 1. Push a new scope for the function call
    // 2. Re-bind captured variables from the closure
    // 3. Set up return infrastructure for ? operator (only if function returns Result/Option)
    // 4. Bind arguments to parameters
    // 5. Evaluate the function body
    // 6. Handle normal return path and merge with early returns (if applicable)
    // 7. Pop scope and return result

    pushFSharpScope();

    // Re-bind captured variables from the closure
    for (auto const& [capName, capStorage]: func->capturedBindings)
        bindFSharpVariable(capName, capStorage);

    // Only set up return infrastructure for functions that return Result/Option
    // This is needed for the ? operator to propagate errors
    CoreVM::BasicBlock* returnBlock = nullptr;
    CoreVM::AllocaInstr* returnStorage = nullptr;

    if (func->returnsResultOrOption)
    {
        returnBlock = _builder.createBlock("func.return");
        returnStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "func.result");
        pushFSharpFunctionContext(returnBlock, returnStorage, true);
    }

    // Bind arguments to parameter names
    for (size_t i = 0; i < func->parameters.size(); ++i)
    {
        CoreVM::LiteralType storageType = args[i]->type();
        CoreVM::AllocaInstr* storage =
            _builder.createAlloca(storageType, _builder.get(CoreVM::CoreNumber(1)), func->parameters[i]);
        _builder.createStore(storage, args[i], func->parameters[i]);
        bindFSharpVariable(func->parameters[i], storage);
    }

    // Inline the function body
    CoreVM::Value* bodyResult = codegen(func->body);

    if (func->returnsResultOrOption)
    {
        // Store result and jump to return block (normal path)
        if (bodyResult)
        {
            _builder.createStore(returnStorage, bodyResult, "store.result");
            _builder.createBr(returnBlock);
        }

        // Continue from return block (merges normal and early return paths)
        _builder.setInsertPoint(returnBlock);
        _result = _builder.createLoad(returnStorage, "load.result");

        popFSharpFunctionContext();
    }
    else
    {
        // Simple case: no error propagation, just use the body result directly
        _result = bodyResult;
    }

    popFSharpScope();
}

void IRGenerator::visit(ast::IdentifierExpr const& node)
{
    TRACE_SCOPE("visit(IdentifierExpr)");

    // Look up in F# scope
    CoreVM::Value* storage = lookupFSharpVariable(node.name);
    if (!storage)
    {
        // Fall back to function name lookup (function-as-value)
        if (lookupFSharpFunction(node.name))
        {
            _result = _builder.get(node.name);
            return;
        }
        reportTypeError("Undefined F# identifier: {}", std::string_view(node.name));
        return;
    }

    // Load the value from storage
    _result = _builder.createLoad(storage, node.name);
}

void IRGenerator::visit(ast::IntLiteralExpr const& node)
{
    // Integer literals can be directly converted to CoreVM numbers
    _result = _builder.get(CoreVM::CoreNumber(node.value));
}

void IRGenerator::visit(ast::FloatLiteralExpr const& node)
{
    _result = _builder.getFloat(node.value);
}

void IRGenerator::visit(ast::BoolLiteralExpr const& node)
{
    _result = _builder.getBoolean(node.value);
}

void IRGenerator::visit(ast::ParenExpr const& node)
{
    // Parentheses just evaluate the inner expression
    if (node.inner)
        codegen(node.inner.get());
}

void IRGenerator::visit(ast::LambdaExpr const& node)
{
    TRACE_SCOPE("visit(LambdaExpr)");

    // Lambda expressions are registered as anonymous functions.
    // When used directly in application/pipeline contexts, they are handled there.
    // When used standalone (e.g., let f = fun x -> x * 2), we register and return
    // a "function reference" that can be looked up later.

    std::string lambdaName = generateLambdaName();

    FSharpFunction func;
    func.parameters = node.parameters;
    func.body = node.body.get();
    func.returnsResultOrOption = isBodyResultOrOption(func.body);
    func.capturedBindings = collectFreeVariables(func.body, func.parameters);
    registerFSharpFunction(lambdaName, std::move(func));

    // Store the lambda name in a way that can be retrieved by the calling context.
    // We use a string constant to represent the function reference.
    // This allows let bindings to store the function name for later lookup.
    _result = _builder.get(lambdaName);
}

void IRGenerator::visit(ast::MatchExpr const& node)
{
    TRACE_SCOPE("visit(MatchExpr)");

    // Evaluate the scrutinee (expression being matched against)
    node.scrutinee->accept(*this);
    CoreVM::Value* scrutinee = _result;
    if (!scrutinee)
    {
        reportTypeError("Failed to evaluate match scrutinee");
        return;
    }

    // Store scrutinee in a local variable so it's available across all arms
    // Use createAllocaInEntryBlock to ensure proper stack tracking
    CoreVM::AllocaInstr* scrutineeStorage = createAllocaInEntryBlock(scrutinee->type(), "scrutinee");
    _builder.createStore(scrutineeStorage, scrutinee, "scrutinee.store");

    // Infer result type from the first arm's body expression
    // This determines the type of storage we need for the match result
    CoreVM::LiteralType resultType = CoreVM::LiteralType::Number; // Default to Number
    if (!node.arms.empty() && node.arms[0].body)
    {
        // Evaluate first arm body to determine its type
        // For now, use a simple heuristic based on AST node types
        auto* body = node.arms[0].body.get();
        if (dynamic_cast<ast::IntLiteralExpr const*>(body))
            resultType = CoreVM::LiteralType::Number;
        else if (dynamic_cast<ast::BoolLiteralExpr const*>(body))
            resultType = CoreVM::LiteralType::Boolean;
        // For other expressions (binary ops, function calls, etc.), default to Number
    }

    // Use createAllocaInEntryBlock to ensure proper stack tracking
    CoreVM::AllocaInstr* resultStorage = createAllocaInEntryBlock(resultType, "match.result");

    // Pre-allocate storage for all bindings from all arms in the entry block
    // This is critical: all allocas must be created before any branching to ensure
    // the TargetCodeGenerator's stack tracking remains consistent across all paths.
    PatternIRGenerator patternIRGenerator(_builder);
    std::vector<std::vector<std::pair<std::string, CoreVM::AllocaInstr*>>> armBindingStorage;

    for (size_t i = 0; i < node.arms.size(); ++i)
    {
        auto const& arm = node.arms[i];
        patternIRGenerator.clearBindings();

        // Compile pattern just to collect bindings (we don't emit branches yet)
        // We use a dummy compilation to extract binding names
        patternIRGenerator.collectBindings(*arm.pattern);

        std::vector<std::pair<std::string, CoreVM::AllocaInstr*>> bindings;
        for (auto const& binding: patternIRGenerator.bindings())
        {
            // Use createAllocaInEntryBlock to ensure proper stack tracking
            auto* storage =
                createAllocaInEntryBlock(scrutinee->type(), binding.name + ".arm" + std::to_string(i));
            bindings.emplace_back(binding.name, storage);
        }
        armBindingStorage.push_back(std::move(bindings));
    }

    // Create the merge block where all arms will eventually converge
    auto* mergeBlock = _builder.createBlock("match.merge");

    // Create blocks for each arm body and pattern check
    std::vector<CoreVM::BasicBlock*> armBodyBlocks;
    std::vector<CoreVM::BasicBlock*> patternCheckBlocks;

    for (size_t i = 0; i < node.arms.size(); ++i)
    {
        patternCheckBlocks.push_back(_builder.createBlock("match.check." + std::to_string(i)));
        armBodyBlocks.push_back(_builder.createBlock("match.arm." + std::to_string(i)));
    }

    // Branch to first pattern check block
    _builder.createBr(patternCheckBlocks[0]);

    // Process each arm
    for (size_t i = 0; i < node.arms.size(); ++i)
    {
        auto const& arm = node.arms[i];

        // Set insert point to this arm's pattern check block
        _builder.setInsertPoint(patternCheckBlocks[i]);

        // Determine where to jump on pattern failure
        CoreVM::BasicBlock* onFailure = (i + 1 < node.arms.size()) ? patternCheckBlocks[i + 1] : mergeBlock;

        // Load the scrutinee for this pattern check
        CoreVM::Value* scrutineeValue = _builder.createLoad(scrutineeStorage, "scrutinee.load");

        // Compile the pattern (this emits the pattern matching IR)
        // Pass scrutineeStorage so the pattern can reload when crossing block boundaries
        patternIRGenerator.clearBindings();

        // Provide pre-allocated binding storage so the pattern compiler stores values
        // in the same basic block where they're extracted (avoiding cross-block references)
        {
            std::unordered_map<std::string, CoreVM::AllocaInstr*> storageMap;
            for (auto const& [name, storage]: armBindingStorage[i])
                storageMap[name] = storage;
            patternIRGenerator.setBindingStorage(std::move(storageMap));
        }

        patternIRGenerator.compile(
            *arm.pattern, scrutineeValue, scrutineeStorage, armBodyBlocks[i], onFailure);

        // Emit the arm body
        _builder.setInsertPoint(armBodyBlocks[i]);

        // Install variable bindings from pattern matching in a new scope
        // Use pre-allocated storage from entry block
        pushFSharpScope();
        auto const& preAllocatedBindings = armBindingStorage[i];

        bool const isTuplePattern = dynamic_cast<pattern::TuplePattern const*>(arm.pattern.get()) != nullptr;

        if (isTuplePattern)
        {
            // Tuple patterns: values were already stored into allocas by PatternIRGenerator
            // Just register the allocas as variable bindings
            for (auto const& [name, storage]: preAllocatedBindings)
            {
                bindFSharpVariable(name, storage);
            }
        }
        // For constructor patterns (Error e, Some x), we need to extract the payload
        // For simple variable patterns, we bind the whole scrutinee
        // Only load the scrutinee if there are actual bindings to store.
        // Dead loads leave values on the stack that accumulate in loops (e.g., let rec).
        else if (!preAllocatedBindings.empty()
                 || dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
        {
            CoreVM::Value* bindingSource = _builder.createLoad(scrutineeStorage, "scrutinee.reload");

            if (auto* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
            {
                // Extract payload from slot 0 for constructor patterns
                if (ctorPat->payload.has_value())
                {
                    bindingSource = _builder.createObjGetSlot(
                        bindingSource, _builder.get(CoreVM::CoreNumber(0)), "ctor.payload");
                }
            }

            for (auto const& [name, storage]: preAllocatedBindings)
            {
                _builder.createStore(storage, bindingSource, name + ".store");
                bindFSharpVariable(name, storage);
            }
        }

        // If there's a guard, evaluate it and branch accordingly
        if (arm.guard)
        {
            auto* guardPassBlock = _builder.createBlock("match.guard." + std::to_string(i) + ".pass");

            arm.guard->accept(*this);
            CoreVM::Value* guardResult = _result;
            if (!guardResult)
            {
                popFSharpScope();
                reportTypeError("Failed to evaluate match guard");
                return;
            }

            // Convert to bool if needed
            CoreVM::Value* guardBool = toBool(guardResult);
            _builder.createCondBr(guardBool, guardPassBlock, onFailure);

            _builder.setInsertPoint(guardPassBlock);
        }

        // Evaluate the arm body
        arm.body->accept(*this);
        CoreVM::Value* bodyResult = _result;

        popFSharpScope();

        if (!bodyResult)
        {
            // A null result inside an active recursion means a tail call was made.
            // The branch back to the entry block has already been emitted, so skip
            // the store-and-branch-to-merge for this arm.
            if (_activeRecursion || _activeMutualRecursion)
                continue;

            reportTypeError("Failed to evaluate match arm body");
            return;
        }

        // Store the result
        _builder.createStore(resultStorage, bodyResult, "match.result.store");

        // Branch to merge block
        _builder.createBr(mergeBlock);
    }

    // Set insert point to merge block and load the result
    _builder.setInsertPoint(mergeBlock);
    _result = _builder.createLoad(resultStorage, "match.result.load");
}

void IRGenerator::visit(ast::ListExpr const& node)
{
    // TODO: Implement list literals - requires list type in CoreVM
    reportTypeError("F# list literals are not yet implemented in IR generator");
    (void) node;
}

void IRGenerator::visit(ast::ListRangeExpr const& node)
{
    // TODO: Implement list range expressions - requires list type in CoreVM
    reportTypeError("F# list range expressions are not yet implemented in IR generator");
    (void) node;
}

void IRGenerator::visit(ast::ListComprehensionExpr const& node)
{
    // TODO: Implement list comprehensions - requires list type and iteration in CoreVM
    reportTypeError("F# list comprehensions are not yet implemented in IR generator");
    (void) node;
}

void IRGenerator::visit(ast::ShellCommandExpr const& node)
{
    // Shell command expression: & git status
    // Captures command output as a string (like command substitution).
    //
    // IR pattern (same as SubstitutionExpr):
    //   1. Start capture - redirects stdout to a pipe
    //   2. Execute the command pipeline
    //   3. End capture - reads captured output and returns as string

    if (!node.command)
    {
        // Empty command - result is empty string
        _result = _builder.get("");
        return;
    }

    // 1. Start capture - redirects stdout to a pipe
    auto* startCb = findCallback("internal.subst_start()V");
    if (!startCb)
    {
        reportTypeError("Internal error: internal.subst_start builtin not found");
        return;
    }
    _builder.createCallFunction(_builder.getBuiltinFunction(*startCb), {}, "subst_start");

    // 2. Execute the command pipeline
    codegen(node.command.get());

    // 3. End capture - reads captured output and returns as string
    auto* endCb = findCallback("internal.subst_end()S");
    if (!endCb)
    {
        reportTypeError("Internal error: internal.subst_end builtin not found");
        return;
    }
    _result = _builder.createCallFunction(_builder.getBuiltinFunction(*endCb), {}, "subst_end");
}

// ============================================================================
// F# Error Handling Expressions
// ============================================================================

void IRGenerator::pushFSharpFunctionContext(CoreVM::BasicBlock* returnBlock,
                                            CoreVM::AllocaInstr* returnStorage,
                                            bool returnsResultOrOption)
{
    _fsharpFunctionContextStack.push_back({ returnBlock, returnStorage, returnsResultOrOption });
}

void IRGenerator::popFSharpFunctionContext()
{
    if (!_fsharpFunctionContextStack.empty())
        _fsharpFunctionContextStack.pop_back();
}

IRGenerator::FSharpFunctionContext* IRGenerator::currentFSharpFunctionContext()
{
    if (_fsharpFunctionContextStack.empty())
        return nullptr;
    return &_fsharpFunctionContextStack.back();
}

void IRGenerator::visit(ast::OptionExpr const& node)
{
    TRACE_SCOPE("visit(OptionExpr)");

    // Option values are represented as TypedObjects:
    // - Tag 0 = None (no payload)
    // - Tag 1 = Some (1 slot payload)

    // Allocate Option object
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Option));
    CoreVM::Value* obj = _builder.createObjAlloc(typeId, "option");

    if (node.isSome)
    {
        // Some value - evaluate the inner expression
        if (!node.value)
        {
            reportTypeError("Some constructor requires a value");
            return;
        }
        CoreVM::Value* innerValue = codegen(node.value.get());
        if (!innerValue)
            return;

        // Set tag to 1 (Some) and store the value in slot 0
        obj = _builder.createObjSetTag(obj, _builder.get(CoreVM::CoreNumber(1)), "option.tag");
        obj = _builder.createObjSetSlot(obj, _builder.get(CoreVM::CoreNumber(0)), innerValue, "option.value");
        _result = obj;
    }
    else
    {
        // None - just set tag to 0, no payload needed
        obj = _builder.createObjSetTag(obj, _builder.get(CoreVM::CoreNumber(0)), "option.tag");
        _result = obj;
    }
}

void IRGenerator::visit(ast::ResultExpr const& node)
{
    TRACE_SCOPE("visit(ResultExpr)");

    // Result values are represented as TypedObjects:
    // - Tag 0 = Error (1 slot payload)
    // - Tag 1 = Ok (1 slot payload)

    if (!node.payload)
    {
        reportTypeError("Result constructor requires a value");
        return;
    }

    CoreVM::Value* payloadValue = codegen(node.payload.get());
    if (!payloadValue)
        return;

    // Allocate Result object
    auto* typeId = _builder.get(CoreVM::CoreNumber(CoreVM::BuiltinTypeId::Result));
    CoreVM::Value* obj = _builder.createObjAlloc(typeId, "result");

    // Set tag (0=Error, 1=Ok) and store the payload in slot 0
    CoreVM::Value* tag = _builder.get(CoreVM::CoreNumber(node.isOk ? 1 : 0));
    obj = _builder.createObjSetTag(obj, tag, "result.tag");
    obj = _builder.createObjSetSlot(obj, _builder.get(CoreVM::CoreNumber(0)), payloadValue, "result.value");
    _result = obj;
}

void IRGenerator::visit(ast::TryExpr const& node)
{
    TRACE_SCOPE("visit(TryExpr)");

    // The ? operator unwraps a Result or Option value:
    // - If the value is Ok/Some (tag=1), extract and return the inner value
    // - If the value is Error/None (tag=0), propagate the error (early return)
    //
    // This requires a function context to know where to jump on error.

    FSharpFunctionContext* funcCtx = currentFSharpFunctionContext();
    if (!funcCtx)
    {
        reportTypeError("Cannot use ? operator outside of a function returning Result/Option");
        return;
    }

    // IMPORTANT: Copy the context values BEFORE calling codegen() below.
    // The operand might be a function application (e.g., `(inc x)?`) which pushes
    // a new FSharpFunctionContext onto _fsharpFunctionContextStack. This can cause
    // the vector to reallocate, invalidating the funcCtx pointer.
    CoreVM::BasicBlock* returnBlock = funcCtx->returnBlock;
    CoreVM::AllocaInstr* returnStorage = funcCtx->returnStorage;

    // Evaluate the operand (should be an Option or Result object)
    // NOTE: This may invalidate funcCtx pointer due to vector reallocation!
    CoreVM::Value* obj = codegen(node.operand.get());
    if (!obj)
        return;

    // Store the object in an alloca so we can reload it in successor blocks.
    // This is necessary because the stack tracking resets at block boundaries.
    CoreVM::AllocaInstr* objStorage = createAllocaInEntryBlock(obj->type(), "try.obj");
    _builder.createStore(objStorage, obj, "try.obj.store");

    // Extract tag using OGETTAG
    CoreVM::Value* tag = _builder.createObjGetTag(obj, "try.tag");

    // Check if success (tag == 1 means Some/Ok)
    CoreVM::Value* isSuccess =
        _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "try.is_success");

    // Pre-allocate result storage in entry block for consistent stack tracking.
    // Use Object type since the inner value could be another Option/Result (nested case).
    CoreVM::AllocaInstr* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "try.result");

    // Create blocks
    auto* successBlock = _builder.createBlock("try.success");
    auto* errorBlock = _builder.createBlock("try.error");
    auto* continueBlock = _builder.createBlock("try.continue");

    _builder.createCondBr(isSuccess, successBlock, errorBlock);

    // Success path: reload object and extract inner value using OGETSLOT
    _builder.setInsertPoint(successBlock);
    CoreVM::Value* objReload1 = _builder.createLoad(objStorage, "try.obj.reload");
    CoreVM::Value* innerValue =
        _builder.createObjGetSlot(objReload1, _builder.get(CoreVM::CoreNumber(0)), "try.inner");

    // Store result and branch to continue
    _builder.createStore(resultStorage, innerValue, "try.result.store");
    _builder.createBr(continueBlock);

    // Error path: reload object and propagate (early return)
    _builder.setInsertPoint(errorBlock);
    CoreVM::Value* objReload2 = _builder.createLoad(objStorage, "try.obj.reload");
    // Store the error object in the function return storage and jump to return block
    // NOTE: Using local copies of returnStorage/returnBlock since funcCtx pointer
    // may have been invalidated by codegen() above.
    _builder.createStore(returnStorage, objReload2, "try.error.store");
    _builder.createBr(returnBlock);

    // Continue with extracted value
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "try.result.load");
}

void IRGenerator::visit(ast::TryWithExpr const& node)
{
    TRACE_SCOPE("visit(TryWithExpr)");

    // try expr with | pattern -> handler | ...
    //
    // 1. Evaluate the body expression (should be an Option or Result object)
    // 2. Check if it's an error (tag == 0)
    // 3. If success, return the inner value
    // 4. If error, match against handlers (similar to match expression)

    if (!node.body)
    {
        reportTypeError("try-with expression requires a body");
        return;
    }

    // Create result storage - use Object type since we may return either unwrapped value or error
    CoreVM::AllocaInstr* resultStorage =
        createAllocaInEntryBlock(CoreVM::LiteralType::Object, "trywith.result");

    // Create blocks
    auto* successBlock = _builder.createBlock("trywith.success");
    auto* errorBlock = _builder.createBlock("trywith.error");
    auto* mergeBlock = _builder.createBlock("trywith.merge");

    // Evaluate body (should be an Option or Result object)
    CoreVM::Value* bodyObj = codegen(node.body.get());
    if (!bodyObj)
        return;

    // Store bodyObj in an alloca for cross-block access
    // This is required because values don't persist across basic block boundaries
    CoreVM::AllocaInstr* bodyStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "trywith.body");
    _builder.createStore(bodyStorage, bodyObj, "trywith.body.store");

    // Extract tag using OGETTAG
    CoreVM::Value* tag = _builder.createObjGetTag(bodyObj, "trywith.tag");
    CoreVM::Value* isSuccess =
        _builder.createNCmpEQ(tag, _builder.get(CoreVM::CoreNumber(1)), "trywith.is_success");

    _builder.createCondBr(isSuccess, successBlock, errorBlock);

    // Success path: reload object and extract inner value using OGETSLOT
    _builder.setInsertPoint(successBlock);
    CoreVM::Value* bodyReload1 = _builder.createLoad(bodyStorage, "trywith.body.reload");
    CoreVM::Value* successValue =
        _builder.createObjGetSlot(bodyReload1, _builder.get(CoreVM::CoreNumber(0)), "trywith.success_value");
    _builder.createStore(resultStorage, successValue, "trywith.success.store");
    _builder.createBr(mergeBlock);

    // Error path: match against handlers
    _builder.setInsertPoint(errorBlock);

    if (node.handlers.empty())
    {
        // No handlers - just return the error object as-is
        CoreVM::Value* bodyReload2 = _builder.createLoad(bodyStorage, "trywith.body.reload");
        _builder.createStore(resultStorage, bodyReload2, "trywith.error.store");
        _builder.createBr(mergeBlock);
    }
    else
    {
        // Reload the body object for error handling
        CoreVM::Value* bodyReload2 = _builder.createLoad(bodyStorage, "trywith.body.reload");

        // Extract error value from slot 0 (the payload of Error/None)
        CoreVM::Value* errorValue = _builder.createObjGetSlot(
            bodyReload2, _builder.get(CoreVM::CoreNumber(0)), "trywith.error_value");

        // Store error value for pattern matching
        CoreVM::AllocaInstr* errorStorage =
            createAllocaInEntryBlock(CoreVM::LiteralType::Object, "trywith.error");
        _builder.createStore(errorStorage, errorValue, "trywith.error.bind");

        // Pre-create blocks for all handlers
        std::vector<CoreVM::BasicBlock*> handlerCheckBlocks;
        std::vector<CoreVM::BasicBlock*> handlerBodyBlocks;
        for (size_t i = 0; i < node.handlers.size(); ++i)
        {
            handlerCheckBlocks.push_back(_builder.createBlock("trywith.check." + std::to_string(i)));
            handlerBodyBlocks.push_back(_builder.createBlock("trywith.body." + std::to_string(i)));
        }

        // Default block - when no handler matches, propagate error as-is
        auto* defaultBlock = _builder.createBlock("trywith.default");

        // Branch to first handler check
        _builder.createBr(handlerCheckBlocks[0]);

        // Process each handler
        for (size_t i = 0; i < node.handlers.size(); ++i)
        {
            auto const& arm = node.handlers[i];

            // Set insert point to this handler's check block
            _builder.setInsertPoint(handlerCheckBlocks[i]);

            // Determine where to jump on pattern/guard failure
            CoreVM::BasicBlock* onFailure =
                (i + 1 < node.handlers.size()) ? handlerCheckBlocks[i + 1] : defaultBlock;

            // Reload error value for pattern matching
            CoreVM::Value* errorReload = _builder.createLoad(errorStorage, "trywith.error.reload");

            // Check if pattern matches
            bool patternAlwaysMatches = false;

            if (auto* varPat = dynamic_cast<pattern::VariablePattern const*>(arm.pattern.get()))
            {
                // Variable pattern always matches
                patternAlwaysMatches = true;
            }
            else if (auto* wildPat = dynamic_cast<pattern::WildcardPattern const*>(arm.pattern.get()))
            {
                // Wildcard always matches
                patternAlwaysMatches = true;
            }
            else if (auto* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
            {
                // Constructor pattern - check payload if it's a literal
                if (ctorPat->payload.has_value())
                {
                    if (auto* litPat = dynamic_cast<pattern::LiteralPattern const*>(ctorPat->payload->get()))
                    {
                        // Compare error value with literal
                        if (auto* intVal = std::get_if<int64_t>(&litPat->value))
                        {
                            CoreVM::Value* litValue = _builder.get(CoreVM::CoreNumber(*intVal));
                            // Use VCmpEQ for dynamic value comparison (errorReload is from OGETSLOT)
                            CoreVM::Value* matches =
                                _builder.createVCmpEQ(errorReload, litValue, "trywith.lit.cmp");
                            _builder.createCondBr(matches, handlerBodyBlocks[i], onFailure);
                        }
                        else
                        {
                            // Non-integer literal patterns not yet supported
                            patternAlwaysMatches = true;
                        }
                    }
                    else if (dynamic_cast<pattern::WildcardPattern const*>(ctorPat->payload->get()))
                    {
                        // Wildcard payload - always matches
                        patternAlwaysMatches = true;
                    }
                    else if (dynamic_cast<pattern::VariablePattern const*>(ctorPat->payload->get()))
                    {
                        // Variable payload - always matches
                        patternAlwaysMatches = true;
                    }
                }
                else
                {
                    // No payload (e.g., just "None") - always matches for that constructor type
                    patternAlwaysMatches = true;
                }
            }
            else if (auto* litPat = dynamic_cast<pattern::LiteralPattern const*>(arm.pattern.get()))
            {
                // Direct literal pattern - compare error value
                if (auto* intVal = std::get_if<int64_t>(&litPat->value))
                {
                    CoreVM::Value* litValue = _builder.get(CoreVM::CoreNumber(*intVal));
                    // Use VCmpEQ for dynamic value comparison (errorReload is from OGETSLOT)
                    CoreVM::Value* matches = _builder.createVCmpEQ(errorReload, litValue, "trywith.lit.cmp");
                    _builder.createCondBr(matches, handlerBodyBlocks[i], onFailure);
                }
                else
                {
                    // Non-integer literal patterns - fall through
                    patternAlwaysMatches = true;
                }
            }

            if (patternAlwaysMatches)
            {
                _builder.createBr(handlerBodyBlocks[i]);
            }

            // Now emit the handler body
            _builder.setInsertPoint(handlerBodyBlocks[i]);

            pushFSharpScope();

            // Bind pattern variables
            if (auto* varPat = dynamic_cast<pattern::VariablePattern const*>(arm.pattern.get()))
            {
                bindFSharpVariable(varPat->name, errorStorage);
            }
            else if (auto* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(arm.pattern.get()))
            {
                if (ctorPat->payload.has_value())
                {
                    if (auto* innerVar =
                            dynamic_cast<pattern::VariablePattern const*>(ctorPat->payload->get()))
                    {
                        bindFSharpVariable(innerVar->name, errorStorage);
                    }
                }
            }

            // Check guard if present
            if (arm.guard)
            {
                auto* guardPassBlock = _builder.createBlock("trywith.guard." + std::to_string(i) + ".pass");

                CoreVM::Value* guardResult = codegen(arm.guard.get());
                if (!guardResult)
                {
                    popFSharpScope();
                    return;
                }
                CoreVM::Value* guardBool = toBool(guardResult);
                _builder.createCondBr(guardBool, guardPassBlock, onFailure);

                _builder.setInsertPoint(guardPassBlock);
            }

            // Evaluate handler body
            CoreVM::Value* handlerResult = codegen(arm.body.get());
            popFSharpScope();

            if (!handlerResult)
                return;

            _builder.createStore(
                resultStorage, handlerResult, "trywith.handler." + std::to_string(i) + ".store");
            _builder.createBr(mergeBlock);
        }

        // Default block: no handler matched - propagate error as-is
        _builder.setInsertPoint(defaultBlock);
        CoreVM::Value* bodyReload3 = _builder.createLoad(bodyStorage, "trywith.body.reload");
        _builder.createStore(resultStorage, bodyReload3, "trywith.default.store");
        _builder.createBr(mergeBlock);
    }

    // Merge block: load result
    _builder.setInsertPoint(mergeBlock);
    _result = _builder.createLoad(resultStorage, "trywith.result.load");
}

} // namespace endo
