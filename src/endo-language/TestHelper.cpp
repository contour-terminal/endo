// SPDX-License-Identifier: Apache-2.0
#include "TestHelper.hpp"

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "IRGenerator.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

namespace endo::test
{

namespace
{
    /// Recursively converts a runtime value (number, tuple, list, option, etc.) to a printable string.
    std::string valueToString(uint64_t rawVal, CoreVM::Runner* runner)
    {
        // Check if the value is a known TypedObject pointer
        if (runner && runner->isKnownObject(rawVal))
        {
            auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawVal));
            auto const typeId = obj->type->id;
            if (typeId == CoreVM::BuiltinTypeId::List)
            {
                std::string result = "[";
                bool first = true;
                while (obj && obj->type->id == CoreVM::BuiltinTypeId::List && obj->tag == 1)
                {
                    if (!first)
                        result += "; ";
                    first = false;
                    result += valueToString(obj->getSlot(0), runner);
                    obj = reinterpret_cast<CoreVM::TypedObject*>(obj->getSlot(1));
                }
                result += "]";
                return result;
            }
            if (typeId == CoreVM::BuiltinTypeId::Tuple2)
            {
                return "(" + valueToString(obj->getSlot(0), runner) + ", "
                       + valueToString(obj->getSlot(1), runner) + ")";
            }
            if (typeId == CoreVM::BuiltinTypeId::Tuple3)
            {
                return "(" + valueToString(obj->getSlot(0), runner) + ", "
                       + valueToString(obj->getSlot(1), runner) + ", "
                       + valueToString(obj->getSlot(2), runner) + ")";
            }
            if (typeId == CoreVM::BuiltinTypeId::Option)
            {
                if (obj->tag == 0)
                    return "None";
                return "Some " + valueToString(obj->getSlot(0), runner);
            }
            if (typeId == CoreVM::BuiltinTypeId::Result)
            {
                if (obj->tag == 0)
                    return "Error " + valueToString(obj->getSlot(0), runner);
                return "Ok " + valueToString(obj->getSlot(0), runner);
            }
            if (obj->type->kind == CoreVM::TypeKind::Product)
            {
                // Record type: { field1 = val1; field2 = val2 }
                std::string result = "{ ";
                for (size_t i = 0; i < obj->type->fields.size(); ++i)
                {
                    if (i > 0)
                        result += "; ";
                    result += obj->type->fields[i].name;
                    result += " = ";
                    auto slotVal = obj->getSlot(static_cast<uint8_t>(i));
                    switch (obj->type->fields[i].type)
                    {
                        case CoreVM::LiteralType::String: {
                            auto const* str =
                                reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(slotVal));
                            result += str ? *str : "(null)";
                            break;
                        }
                        case CoreVM::LiteralType::Boolean: result += slotVal ? "true" : "false"; break;
                        default: result += std::to_string(static_cast<int64_t>(slotVal)); break;
                    }
                }
                result += " }";
                return result;
            }
            // Unknown object type — fallback
            return std::to_string(static_cast<int64_t>(rawVal));
        }
        return std::to_string(static_cast<int64_t>(rawVal));
    }
} // namespace

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

    // Helper: converts a list TypedObject to string "[1; 2; 3]" (delegates to valueToString)
    auto listToString = [](CoreVM::TypedObject* obj, CoreVM::Runner* runner) -> std::string {
        return valueToString(reinterpret_cast<uintptr_t>(obj), runner);
    };

    // Register list_to_string builtin: converts list object to "[1; 2; 3]" string
    runtime.registerFunction("list_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind([listToString](CoreVM::Params& args) {
            auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            args.setResult(args.caller()->newString(listToString(obj, args.caller())));
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

    // Register list_head builtin: returns Option (Some head | None)
    runtime.registerFunction("list_head")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            if (!list || list->tag == 0)
            {
                // Nil → None
                auto* none = args.caller()->allocObject(CoreVM::BuiltinTypeId::Option);
                none->tag = 0;
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
            }
            else
            {
                // Cons → Some(head)
                auto* some = args.caller()->allocObject(CoreVM::BuiltinTypeId::Option);
                some->tag = 1;
                some->setSlot(0, list->getSlot(0));
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
            }
        });

    // Register list_tail builtin: returns tail of list (or [] for empty)
    runtime.registerFunction("list_tail")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            if (!list || list->tag == 0)
            {
                // Nil → return new Nil
                auto* nil = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                nil->tag = 0;
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(nil)));
            }
            else
            {
                // Cons → return tail pointer
                args.setResult(static_cast<CoreVM::CoreNumber>(list->getSlot(1)));
            }
        });

    // Register list_length builtin: returns number of elements
    runtime.registerFunction("list_length")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            int64_t count = 0;
            while (cur && cur->tag == 1)
            {
                ++count;
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(count));
        });

    // Register list_isEmpty builtin: returns true if list is Nil
    runtime.registerFunction("list_isEmpty")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            args.setResult(!list || list->tag == 0);
        });

    // Register object_to_string builtin: runtime dispatch for object printing
    runtime.registerFunction("object_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto rawVal = static_cast<uint64_t>(args.getInt(1));
            args.setResult(args.caller()->newString(valueToString(rawVal, args.caller())));
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

    // --- String standard library builtins ---

    // string_trim: removes leading/trailing whitespace
    runtime.registerFunction("string_trim")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto str = std::string(args.getString(1));
            auto const start = str.find_first_not_of(" \t\n\r");
            if (start == std::string::npos)
                args.setResult(args.caller()->newString(""));
            else
            {
                auto const end = str.find_last_not_of(" \t\n\r");
                args.setResult(args.caller()->newString(str.substr(start, end - start + 1)));
            }
        });

    // string_toLower: converts string to lowercase
    runtime.registerFunction("string_toLower")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto str = std::string(args.getString(1));
            std::ranges::transform(str, str.begin(), ::tolower);
            args.setResult(args.caller()->newString(str));
        });

    // string_toUpper: converts string to uppercase
    runtime.registerFunction("string_toUpper")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto str = std::string(args.getString(1));
            std::ranges::transform(str, str.begin(), ::toupper);
            args.setResult(args.caller()->newString(str));
        });

    // string_contains: checks if haystack contains needle
    runtime.registerFunction("string_contains")
        .param<CoreVM::CoreString>("haystack")
        .param<CoreVM::CoreString>("needle")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            args.setResult(args.getString(1).find(args.getString(2)) != std::string_view::npos);
        });

    // string_startsWith: checks if text starts with prefix
    runtime.registerFunction("string_startsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("prefix")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) { args.setResult(args.getString(1).starts_with(args.getString(2))); });

    // string_endsWith: checks if text ends with suffix
    runtime.registerFunction("string_endsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("suffix")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) { args.setResult(args.getString(1).ends_with(args.getString(2))); });

    // string_replace: replaces all occurrences of old with new in text
    runtime.registerFunction("string_replace")
        .param<CoreVM::CoreString>("old_str")
        .param<CoreVM::CoreString>("new_str")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto text = std::string(args.getString(3));
            auto const old_s = args.getString(1);
            auto const new_s = args.getString(2);
            if (!old_s.empty())
            {
                size_t pos = 0;
                while ((pos = text.find(old_s, pos)) != std::string::npos)
                {
                    text.replace(pos, old_s.size(), new_s);
                    pos += new_s.size();
                }
            }
            args.setResult(args.caller()->newString(text));
        });

    // string_split: splits text by delimiter into list<str>
    runtime.registerFunction("string_split")
        .param<CoreVM::CoreString>("delimiter")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
        .bind([](CoreVM::Params& args) {
            auto const text = std::string(args.getString(2));
            auto const delim = std::string(args.getString(1));
            auto* runner = args.caller();

            std::vector<std::string> parts;
            if (delim.empty())
            {
                for (auto c: text)
                    parts.emplace_back(1, c);
            }
            else
            {
                size_t pos = 0;
                size_t found = 0;
                while ((found = text.find(delim, pos)) != std::string::npos)
                {
                    parts.push_back(text.substr(pos, found - pos));
                    pos = found + delim.size();
                }
                parts.push_back(text.substr(pos));
            }

            // Build cons-cell list right-to-left
            auto* list = runner->allocObject(CoreVM::BuiltinTypeId::List);
            list->tag = 0; // Nil
            for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            {
                auto* cons = runner->allocObject(CoreVM::BuiltinTypeId::List);
                cons->tag = 1; // Cons
                cons->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(*it)));
                cons->setSlot(1, reinterpret_cast<uintptr_t>(list));
                list = cons;
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // string_join: joins list<str> with separator
    runtime.registerFunction("string_join")
        .param<CoreVM::CoreString>("separator")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto const sep = std::string(args.getString(1));
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(2)));

            std::string result;
            bool first = true;
            while (list && list->tag == 1)
            {
                if (!first)
                    result += sep;
                auto* str = reinterpret_cast<CoreVM::CoreString*>(list->getSlot(0));
                if (str)
                    result += *str;
                list = reinterpret_cast<CoreVM::TypedObject*>(list->getSlot(1));
                first = false;
            }
            args.setResult(args.caller()->newString(result));
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
