// SPDX-License-Identifier: Apache-2.0
#include <endo-language/CompileToIR.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

namespace endo
{

FSharpPersistentState makeDefaultPersistentState()
{
    FSharpPersistentState state;
    CoreVM::TypeRegistry registry;
    for (auto const& type: registry.allTypes())
    {
        if (!type->producingCommand.empty())
        {
            state.structuredCommands[type->producingCommand] = {
                .builtinCallbackName = "structured_" + type->producingCommand,
                .recordTypeId = type->id,
                .recordTypeName = type->name,
            };
        }
    }
    if (auto it = state.structuredCommands.find("ls"); it != state.structuredCommands.end())
        it->second.defaultStringArg = ".";
    return state;
}

std::unique_ptr<CoreVM::IRProgram> compileToIR(std::string source,
                                               CoreVM::Runtime& runtime,
                                               CoreVM::diagnostics::Report& report,
                                               std::string_view sourceName,
                                               bool unusedValueDetection)
{
    Parser parser(runtime, report, std::make_unique<StringSource>(std::move(source), sourceName));
    auto rootNode = parser.parse();
    if (!rootNode || report.containsFailures())
        return nullptr;

    auto persistentState = makeDefaultPersistentState();
    auto irProgram =
        IRGenerator::generate(*rootNode, report, runtime, &persistentState, unusedValueDetection);
    if (!irProgram || report.containsFailures())
        return nullptr;

    return irProgram;
}

} // namespace endo
