// SPDX-License-Identifier: Apache-2.0
#include "TestHelper.hpp"

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <unordered_set>
#include <vector>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "IRGenerator.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

namespace endo::test
{

TestRuntime::TestRuntime()
{
    // Register minimal builtins for the parser to work
    runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&TestRuntime::dummyCallProc, this);

    runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&TestRuntime::dummyCallProcPiped, this);

    // Register print builtin (no newline)
    runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&TestRuntime::builtinPrint, this);

    // Register println builtin (with newline)
    runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&TestRuntime::builtinPrintln, this);

    // Register string_repeat builtin: "ha" * 3 → "hahaha"
    runtime.registerFunction("string_repeat")
        .param<CoreVM::CoreString>("str")
        .param<CoreVM::CoreNumber>("count")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto const& str = args.getString(1);
            auto const count = args.getInt(2);
            std::string result;
            if (count > 0)
            {
                result.reserve(static_cast<size_t>(count) * str.size());
                for (int64_t i = 0; i < count; ++i)
                    result += str;
            }
            args.setResult(args.caller()->newString(result));
        });

    // Helper: converts a list TypedObject to string "[1; 2; 3]"
    auto listToString = [](CoreVM::TypedObject* obj) -> std::string {
        std::string result = "[";
        bool first = true;
        while (obj && obj->type->id == CoreVM::BuiltinTypeId::List && obj->tag == 1)
        {
            if (!first)
                result += "; ";
            first = false;
            // slot[0] = head (number), slot[1] = tail (object pointer)
            auto headVal = static_cast<int64_t>(obj->getSlot(0));
            result += std::to_string(headVal);
            obj = reinterpret_cast<CoreVM::TypedObject*>(obj->getSlot(1));
        }
        result += "]";
        return result;
    };

    // Register list_to_string builtin: converts list object to "[1; 2; 3]" string
    runtime.registerFunction("list_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind([listToString](CoreVM::Params& args) {
            auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            args.setResult(args.caller()->newString(listToString(obj)));
        });

    // Register list_concat builtin: concatenates two lists
    runtime.registerFunction("list_concat")
        .param<CoreVM::CoreNumber>("left")
        .param<CoreVM::CoreNumber>("right")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* left = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            auto* right = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(2)));

            // If left is Nil, return right
            if (!left || left->tag == 0)
            {
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(right)));
                return;
            }

            // Collect left list elements
            std::vector<uint64_t> elements;
            auto* cur = left;
            while (cur && cur->tag == 1)
            {
                elements.push_back(cur->getSlot(0));
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }

            // Build result list from right-to-left, starting with right list as tail
            CoreVM::TypedObject* acc = right;
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
            {
                auto* cons = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                cons->tag = 1;
                cons->setSlot(0, *it);
                cons->setSlot(1, reinterpret_cast<uintptr_t>(acc));
                acc = cons;
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // Register object_to_string builtin: runtime dispatch for object printing
    runtime.registerFunction("object_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind([listToString](CoreVM::Params& args) {
            auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            if (obj && obj->type->id == CoreVM::BuiltinTypeId::List)
            {
                args.setResult(args.caller()->newString(listToString(obj)));
            }
            else
            {
                // Fallback: print as number
                args.setResult(args.caller()->newString(std::to_string(args.getInt(1))));
            }
        });

    // Register env.has builtin (returns boolean: true if key exists in mock env)
    runtime.registerFunction("env.has")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([this](CoreVM::Params& args) {
            auto const& key = args.getString(1);
            args.setResult(mockEnv.contains(key));
        });

    // Register env.get builtin (returns string value, empty if not found)
    runtime.registerFunction("env.get")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::String)
        .bind([this](CoreVM::Params& args) {
            auto const& key = args.getString(1);
            if (auto const it = mockEnv.find(key); it != mockEnv.end())
                args.setResult(args.caller()->newString(it->second));
            else
                args.setResult(args.caller()->newString(""));
        });
}

void TestRuntime::dummyCallProc(CoreVM::Params&)
{
}

void TestRuntime::dummyCallProcPiped(CoreVM::Params&)
{
}

void TestRuntime::builtinPrint(CoreVM::Params& params)
{
    capturedOutput += params.getString(1);
}

void TestRuntime::builtinPrintln(CoreVM::Params& params)
{
    capturedOutput += params.getString(1);
    capturedOutput += '\n';
}

void TestRuntime::setMockEnvVar(std::string const& key, std::string const& value)
{
    mockEnv[key] = value;
}

void TestRuntime::clearMockEnvVars()
{
    mockEnv.clear();
}

void TestRuntime::clearErrors()
{
    report.clear();
}

void TestRuntime::clearOutput()
{
    capturedOutput.clear();
}

bool TestRuntime::hasErrors() const
{
    return report.containsFailures();
}

std::string const& TestRuntime::output() const
{
    return capturedOutput;
}

TestRuntime& TestRuntime::instance()
{
    static TestRuntime instance;
    return instance;
}

std::unique_ptr<ast::Statement> parse(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto result = parser.parse();

    if (testRuntime.hasErrors())
    {
        return nullptr;
    }

    return result;
}

std::unique_ptr<CoreVM::IRProgram> generateIR(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();

    if (!ast || testRuntime.hasErrors())
    {
        return nullptr;
    }

    auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime);

    if (!ir || testRuntime.hasErrors())
    {
        return nullptr;
    }

    return ir;
}

bool generatesIRSuccessfully(std::string const& source)
{
    auto ir = generateIR(source);
    return ir != nullptr;
}

bool generatesIRWithError(std::string const& source, std::string_view expectedErrorSubstring)
{
    auto ir = generateIR(source);
    if (ir)
        return false; // Expected failure but IR generation succeeded

    auto const& messages = TestRuntime::instance().report.messages();
    for (auto const& msg: messages)
        if (msg.text.find(expectedErrorSubstring) != std::string::npos)
            return true;

    return false;
}

ast::Statement* getFirstStatement(ast::Statement* stmt)
{
    if (auto* compound = dynamic_cast<ast::CompoundStmt*>(stmt))
    {
        if (!compound->statements.empty())
        {
            return dynamic_cast<ast::Statement*>(compound->statements[0].get());
        }
    }
    return nullptr;
}

std::string parseAndPrintAST(std::string const& source)
{
    auto ast = parse(source);
    if (!ast)
    {
        throw ParseError("Parse failed for: \"" + source + "\"");
    }
    return ast::ASTPrinter::print(*ast);
}

ExecutionResult executeSource(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    // Generate IR
    auto ir = generateIR(source);
    if (!ir)
        return std::unexpected(TestError::IRGenerationFailed);

    // Generate target code
    CoreVM::TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(ir.get());
    if (!targetProgram)
        return std::unexpected(TestError::CodeGenerationFailed);

    // Link the program to the runtime (required for native function calls like print/println)
    if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
        return std::unexpected(TestError::LinkFailed);

    // Find the main handler
    CoreVM::Handler const* handler = targetProgram->findHandler("@main");
    if (!handler)
        return std::unexpected(TestError::HandlerNotFound);

    // Execute
    CoreVM::Runner::Globals globals;
    CoreVM::Runner runner(handler, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);

    // Runner::run() returns true if exit code was non-zero, false if it was 0
    bool exitNonZero = runner.run();
    int64_t exitCode = exitNonZero ? 1 : 0;

    return TestExecutionSuccess { .exitCode = exitCode, .output = testRuntime.output() };
}

std::string executeSourceAndGetOutput(std::string const& source)
{
    auto result = executeSource(source);
    if (!result.has_value())
        throw ExecutionError(result.error());
    return std::move(result->output);
}

bool executesSuccessfully(std::string const& source)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == 0;
}

bool executesWithExitCode(std::string const& source, int64_t expectedExitCode)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == expectedExitCode;
}

bool executesWithOutput(std::string const& source, std::string_view expectedOutput)
{
    auto result = executeSource(source);
    return result.has_value() && result->output == expectedOutput;
}

bool executesWithResult(std::string const& source, int64_t expectedExitCode, std::string_view expectedOutput)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == expectedExitCode && result->output == expectedOutput;
}

// =============================================================================
// Multi-prompt (REPL session) test helpers
// =============================================================================

ExecutionResult executeSession(std::vector<std::string> const& prompts)
{
    auto& testRuntime = TestRuntime::instance();
    FSharpPersistentState fsharpState;

    ExecutionResult lastResult = std::unexpected(TestError::ExecutionFailed);

    for (auto const& source: prompts)
    {
        testRuntime.clearErrors();
        testRuntime.clearOutput();

        // Parse
        Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
        if (!fsharpState.functions.empty())
        {
            std::unordered_set<std::string> names;
            for (auto const& [name, _]: fsharpState.functions)
                names.insert(name);
            parser.setKnownFSharpFunctions(std::move(names));
        }
        auto ast = parser.parse();
        if (!ast || testRuntime.hasErrors())
            return std::unexpected(TestError::ParseFailed);

        // Generate IR with persistent state
        auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime, &fsharpState);
        if (!ir || testRuntime.hasErrors())
            return std::unexpected(TestError::IRGenerationFailed);

        // Retain the AST so persisted function body pointers remain valid
        fsharpState.retainedASTs.push_back(std::move(ast));

        // Generate target code
        CoreVM::TargetCodeGenerator codegen;
        auto targetProgram = codegen.generate(ir.get());
        if (!targetProgram)
            return std::unexpected(TestError::CodeGenerationFailed);

        // Link
        if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
            return std::unexpected(TestError::LinkFailed);

        // Find main handler
        CoreVM::Handler const* handler = targetProgram->findHandler("@main");
        if (!handler)
            return std::unexpected(TestError::HandlerNotFound);

        // Execute
        CoreVM::Runner::Globals globals;
        CoreVM::Runner runner(handler, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
        bool exitNonZero = runner.run();
        int64_t exitCode = exitNonZero ? 1 : 0;

        // Save runtime values of mutable bindings for cross-prompt persistence.
        // Allocas are at the bottom of the stack (positions 0, 1, 2, ...),
        // matching the order of persisted value bindings.
        auto const& stack = runner.stack();
        for (size_t i = 0; i < fsharpState.valueBindings.size() && i < stack.size(); ++i)
            if (fsharpState.valueBindings[i].isMutable)
                fsharpState.mutableSnapshots[fsharpState.valueBindings[i].name] = stack[i];

        lastResult = TestExecutionSuccess { .exitCode = exitCode, .output = testRuntime.output() };
    }

    return lastResult;
}

std::string executeSessionAndGetOutput(std::vector<std::string> const& prompts)
{
    auto result = executeSession(prompts);
    if (!result.has_value())
        throw ExecutionError(result.error());
    return std::move(result->output);
}

bool sessionProducesOutput(std::vector<std::string> const& prompts, std::string_view expectedOutput)
{
    auto result = executeSession(prompts);
    return result.has_value() && result->output == expectedOutput;
}

} // namespace endo::test
