# Endo Shell Runtime
# This library contains the shell execution runtime, builtins, job control, etc.
# It depends on the endo language library for parsing and IR generation.

add_library(endo-shell STATIC
    # Core
    Shell.hpp
    Shell.cpp
    Error.hpp
    Job.hpp
    Job.cpp
    ProcessGroup.hpp
    TTY.hpp
    TTY.cpp
    CrashHandler.hpp
    CrashHandler.cpp

    # UI (Prompt, Syntax Highlighting, Grapheme Handling)
    ui/Prompt.hpp
    ui/Prompt.cpp
    ui/PromptComponent.hpp
    ui/PromptComponent.cpp
    ui/PromptConfig.hpp
    ui/PromptLayoutEngine.hpp
    ui/PromptLayoutEngine.cpp
    ui/PromptModule.hpp
    ui/PromptPresets.hpp
    ui/PromptPresets.cpp
    ui/Gradient.hpp
    ui/Gradient.cpp
    ui/RichConsoleReport.hpp
    ui/RichConsoleReport.cpp
    ui/SyntaxHighlighter.hpp
    ui/SyntaxHighlighter.cpp
    ui/SourceOffsetUtils.hpp
    ui/SourceOffsetUtils.cpp
    ui/GraphemeCluster.hpp

    # UI Modules
    ui/modules/BatteryModule.hpp
    ui/modules/BatteryModule.cpp
    ui/modules/ClockModule.hpp
    ui/modules/ClockModule.cpp
    ui/modules/DurationModule.hpp
    ui/modules/DurationModule.cpp
    ui/modules/ExitStatusModule.hpp
    ui/modules/ExitStatusModule.cpp
    ui/modules/FSharpModeModule.hpp
    ui/modules/FSharpModeModule.cpp
    ui/modules/GitModule.hpp
    ui/modules/GitModule.cpp
    ui/modules/HostnameModule.hpp
    ui/modules/HostnameModule.cpp
    ui/modules/IndicatorModule.hpp
    ui/modules/IndicatorModule.cpp
    ui/modules/PathModule.hpp
    ui/modules/PathModule.cpp
    ui/modules/ShellLevelModule.hpp
    ui/modules/ShellLevelModule.cpp
    ui/modules/StructuredOutputModule.hpp
    ui/modules/StructuredOutputModule.cpp
    ui/modules/ToolchainModule.hpp
    ui/modules/ToolchainModule.cpp

    # Completion
    completion/Completer.hpp
    completion/Completer.cpp
    completion/CompletionProvider.hpp
    completion/CompletionAdapter.hpp
    completion/CompletionAdapter.cpp
    completion/BuiltinArgumentCompleter.hpp
    completion/BuiltinArgumentCompleter.cpp
    completion/CommandCompleter.hpp
    completion/CommandCompleter.cpp
    completion/CommandSpecCompleter.hpp
    completion/CommandSpecCompleter.cpp
    completion/FileCompleter.hpp
    completion/FileCompleter.cpp
    completion/FSharpCompleter.hpp
    completion/FSharpCompleter.cpp
    completion/HistoryCompleter.hpp
    completion/HistoryCompleter.cpp
    completion/LetBindingCompleter.hpp
    completion/LetBindingCompleter.cpp
    completion/OptionCompleter.hpp
    completion/OptionCompleter.cpp
    completion/VariableCompleter.hpp
    completion/VariableCompleter.cpp
    completion/CommandLineParser.hpp
    completion/CommandLineParser.cpp
    completion/CommandQueryProvider.hpp
    completion/CommandSpec.hpp
    completion/QueryCache.hpp
    completion/QueryCache.cpp
    completion/CmakeSpec.hpp
    completion/CmakeSpec.cpp
    completion/GitSpec.hpp
    completion/GitSpec.cpp
    completion/SshSpec.hpp
    completion/SshSpec.cpp

    # History
    history/History.hpp
    history/History.cpp
    history/PersistentHistory.hpp
    history/PersistentHistory.cpp

    # Structured Output
    output/OutputDefinition.hpp
    output/OutputDefinitionRegistry.hpp
    output/OutputDefinitionRegistry.cpp
    output/OutputParser.hpp
    output/OutputParser.cpp
    output/FileTypeStyle.hpp
    output/FileTypeStyle.cpp
    output/TableFormatter.hpp
    output/TableFormatter.cpp

    # Utilities
    util/CommandResolver.hpp
    util/CommandResolver.cpp
    util/GlobMatcher.hpp
    util/GlobMatcher.cpp
    util/Suggestions.hpp
    util/Suggestions.cpp

    # Builtins
    builtins/InlineCommands.cpp
    builtins/Registration.cpp
    builtins/ProcessExecution.cpp
    builtins/Environment.cpp
    builtins/CommandBuilder.cpp
    builtins/Redirects.cpp
    builtins/Substitution.cpp
    builtins/Expansion.cpp
    builtins/FlowControl.cpp
    builtins/JobControl.cpp
    builtins/ReadCommand.cpp
    builtins/UserCommands.cpp
    builtins/Output.cpp

    # Format
    FormatCommand.hpp
    FormatCommand.cpp

    # Structured Commands
    commands/StructuredCommand.hpp
    commands/PsCommand.hpp
    commands/PsCommand.cpp
    commands/LsCommand.hpp
    commands/LsCommand.cpp
    commands/FindExpression.hpp
    commands/FindExpression.cpp
    commands/FindCommand.hpp
    commands/FindCommand.cpp
    commands/JobProvider.hpp
    commands/JobsCommand.hpp
    commands/JobsCommand.cpp
)

# Platform-specific implementations
if(WIN32)
    target_sources(endo-shell PRIVATE
        platform/WindowsTTY.cpp
        platform/WindowsTestPTY.cpp
    )
endif()

find_package(Threads REQUIRED)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(endo-shell PUBLIC endo vtparser tui psapi)
else()
    # util for pty.h on POSIX systems
    target_link_libraries(endo-shell PUBLIC endo vtparser tui Threads::Threads util)
endif()

target_link_libraries(endo-shell PUBLIC endo-platform yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json endo-http endo-agent)

# C++23 std::stacktrace may require -lstdc++exp (GCC/Clang with libstdc++)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <stacktrace>
int main() { return int(std::stacktrace::current().size()); }
" ENDO_STACKTRACE_NATIVE)

if(NOT ENDO_STACKTRACE_NATIVE AND NOT MSVC)
    set(CMAKE_REQUIRED_LIBRARIES stdc++exp)
    check_cxx_source_compiles("
    #include <stacktrace>
    int main() { return int(std::stacktrace::current().size()); }
    " ENDO_STACKTRACE_WITH_EXP)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(ENDO_STACKTRACE_WITH_EXP)
        target_link_libraries(endo-shell PRIVATE stdc++exp)
    endif()
endif()

target_compile_definitions(endo-shell PRIVATE
    ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
)

if(ENDO_TRACE_VM)
  target_compile_definitions(endo-shell PUBLIC ENDO_TRACE_VM)
endif()

add_executable(endo-bin
    main.cpp
)
if(MSVC)
    target_sources(endo-bin PRIVATE endo.manifest)
endif()
set_target_properties(endo-bin PROPERTIES OUTPUT_NAME endo)
target_link_libraries(endo-bin PUBLIC endo-shell endo-lsp)
enable_static_linking(endo-bin)

add_executable(test-endo-shell
    test_main.cpp
    Shell_test.cpp
    commands/FindExpression_test.cpp
    CrashHandler_test.cpp
    ui/PromptComponent_test.cpp
    ui/Gradient_test.cpp
    completion/Completer_test.cpp
    completion/FSharpCompleter_test.cpp
    completion/CommandSpecCompleter_test.cpp
    history/History_test.cpp
    history/PersistentHistory_test.cpp
    output/OutputDefinitionRegistry_test.cpp
    output/OutputParser_test.cpp
    util/Suggestions_test.cpp
    ui/RichConsoleReport_test.cpp
    ui/SyntaxHighlighter_test.cpp
)

target_link_libraries(test-endo-shell endo-shell Catch2::Catch2)
target_compile_definitions(test-endo-shell PRIVATE
    ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
)

add_test(NAME test-endo-shell COMMAND test-endo-shell)

# Documentation snippet validation test
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(
        NAME check-doc-snippets
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/check-doc-snippets.py
            --endo-path $<TARGET_FILE:endo-bin>
            --allow-failures
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(check-doc-snippets PROPERTIES
        TIMEOUT 120
        LABELS "docs"
    )
endif()

# Enable sanitizers for all targets
enable_sanitizers(endo-shell)
enable_sanitizers(endo-bin)
enable_sanitizers(test-endo-shell)
