# Endo Shell Runtime
# This library contains the shell execution runtime, builtins, job control, etc.
# It depends on the endo language library for parsing and IR generation.

# Generate the build-derived version header (build/src/shell/EndoVersion.hpp),
# includable as <shell/EndoVersion.hpp>. ENDO_VERSION_STRING / ENDO_VERSION are
# set at top scope by GetVersionInformation() and are visible here via the
# add_subdirectory() scope. (Re-runs at configure time, not on every commit.)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/EndoVersion.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/EndoVersion.hpp @ONLY)

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

    # UI (Prompt, Syntax Highlighting, Grapheme Handling)
    ui/Prompt.hpp
    ui/Prompt.cpp
    ui/PromptComponent.hpp
    ui/PromptComponent.cpp
    ui/PromptColorResolver.hpp
    ui/PromptColorResolver.cpp
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
    completion/VariableCompleter.hpp
    completion/VariableCompleter.cpp
    completion/CommandLineParser.hpp
    completion/CommandLineParser.cpp
    completion/CommandQueryProvider.hpp
    completion/CommandSpec.hpp
    completion/QueryCache.hpp
    completion/QueryCache.cpp
    completion/CompleterFunctionRegistry.hpp
    completion/ScriptedCompleter.hpp
    completion/ScriptedCompleter.cpp
    completion/DirconfigSpec.hpp
    completion/DirconfigSpec.cpp
    completion/HistorySpec.hpp
    completion/HistorySpec.cpp
    completion/BuiltinSpecs.hpp
    completion/BuiltinSpecs.cpp
    completion/GitSpec.hpp
    completion/GitSpec.cpp

    # History
    history/History.hpp
    history/History.cpp
    history/PersistentHistory.hpp
    history/PersistentHistory.cpp
    history/RequiredPaths.hpp
    history/RequiredPaths.cpp

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
    util/ShellQuoting.hpp
    util/ShellQuoting.cpp

    # Directory Configuration
    DirectoryConfig.hpp
    DirectoryConfig.cpp

    # Builtins
    builtins/InlineCommandDescriptor.hpp
    builtins/InlineCommandDescriptors.cpp
    builtins/InlineArgParser.hpp
    builtins/InlineArgParser.cpp
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
    builtins/DirectoryConfigBuiltins.cpp
    builtins/HttpServer.cpp

    # Format
    FormatCommand.hpp
    FormatCommand.cpp

    # Compile-to-WebAssembly (self-gated on ENDO_HAS_WASM)
    CompileCommand.hpp
    CompileCommand.cpp

    # Help
    HelpPrinter.hpp
    HelpPrinter.cpp
    SixelCapability.cpp

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
    commands/GrepCommand.hpp
    commands/GrepCommand.cpp
    commands/TimeoutCommand.hpp
    commands/TimeoutCommand.cpp
    commands/JobProvider.hpp
    commands/JobsCommand.hpp
    commands/JobsCommand.cpp
    commands/BindCommand.hpp
    commands/BindCommand.cpp
    commands/KillCommand.hpp
    commands/KillCommand.cpp
    commands/PkillCommand.hpp
    commands/PkillCommand.cpp
    commands/PgrepCommand.hpp
    commands/PgrepCommand.cpp
    commands/PidofCommand.hpp
    commands/PidofCommand.cpp
    commands/ProcessMatch.hpp
    commands/ProcessMatch.cpp
    completion/ProcessNameQueryProvider.hpp
    completion/ProcessNameQueryProvider.cpp
)

if(NOT ENDO_BUILTIN_CRARHDUMP)
  target_sources(endo-shell PRIVATE
    CrashHandler.hpp
    CrashHandler.cpp
    )
endif()

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
    target_link_libraries(endo-shell PUBLIC endo vtparser tui Threads::Threads)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(endo-shell PUBLIC util) # forkpty() on Linux
    endif()
endif()

target_link_libraries(endo-shell PUBLIC endo-platform net yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json endo-http)
if(ENDO_HAS_WASM)
    # Brings in the ENDO_HAS_WASM=1 compile definition (INTERFACE) and binaryen.
    target_link_libraries(endo-shell PUBLIC CoreVM-wasm)
endif()
if(ENDO_ENABLE_AGENT)
    target_sources(endo-shell PRIVATE AgentModeSession.cpp)
    target_link_libraries(endo-shell PUBLIC endo-agent)
    target_compile_definitions(endo-shell PUBLIC ENDO_ENABLE_AGENT=1)
endif()

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

# Expose the generated EndoVersion.hpp (build/src/shell/EndoVersion.hpp) as
# <shell/EndoVersion.hpp> to this target and its dependents (e.g. endo-bin). The
# binary-tree src dir is already inherited transitively from CoreVM; this makes
# the dependency explicit rather than relying on that.
target_include_directories(endo-shell PUBLIC $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/src>)

target_compile_definitions(endo-shell PRIVATE
    ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
    ENDO_COMPLETERS_DIR="${CMAKE_SOURCE_DIR}/data/completers"
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
target_link_libraries(endo-bin PUBLIC endo-shell endo-lsp endo-dap)
enable_static_linking(endo-bin)

if(ENDO_TESTING)
    add_executable(test-endo-shell
        test_main.cpp
        Shell_test.cpp
        output/TableFormatter_test.cpp
        DirectoryConfig_test.cpp
        builtins/InlineArgParser_test.cpp
        builtins/CatRenderMode_test.cpp
        commands/FindExpression_test.cpp
        commands/GrepCommand_test.cpp
        commands/TimeoutCommand_test.cpp
        commands/PkillCommand_test.cpp
        commands/PgrepCommand_test.cpp
        commands/PidofCommand_test.cpp
        ui/PromptComponent_test.cpp
        ui/PromptPresets_test.cpp
        ui/modules/GitModule_test.cpp
        ui/modules/HostnameModule_test.cpp
        ui/modules/PathModule_test.cpp
        ui/Gradient_test.cpp
        completion/Completer_test.cpp
        completion/FSharpCompleter_test.cpp
        completion/CommandSpecCompleter_test.cpp
        completion/CompleterFunctionRegistry_test.cpp
        completion/ScriptedCompleter_test.cpp
        history/History_test.cpp
        history/PersistentHistory_test.cpp
        history/RequiredPaths_test.cpp
        output/OutputDefinitionRegistry_test.cpp
        output/OutputParser_test.cpp
        util/CommandResolver_test.cpp
        util/Suggestions_test.cpp
        util/ShellQuoting_test.cpp
        ui/RichConsoleReport_test.cpp
        ui/SyntaxHighlighter_test.cpp
    )

    if(NOT ENDO_BUILTIN_CRARHDUMP)
        target_sources(test-endo-shell PUBLIC
            CrashHandler_test.cpp
        )
    endif()

    target_link_libraries(test-endo-shell endo-shell Catch2::Catch2)
    target_compile_definitions(test-endo-shell PRIVATE
        ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
        ENDO_COMPLETERS_DIR="${CMAKE_SOURCE_DIR}/data/completers"
    )

    add_test(NAME test-endo-shell COMMAND test-endo-shell)
    set_tests_properties(test-endo-shell PROPERTIES TIMEOUT 120)
    if(WIN32)
        set_tests_properties(test-endo-shell PROPERTIES TIMEOUT 300)
    endif()

    # Documentation snippet validation test
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(
            NAME check-doc-snippets
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/scripts/check-doc-snippets.py
                --endo-path $<TARGET_FILE:endo-bin>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(check-doc-snippets PROPERTIES
            TIMEOUT 120
            LABELS "docs"
        )
    endif()
endif()

# Installation

# Installing discards the build-tree RPATH, so libraries that live outside the
# dynamic loader's search path must be re-declared here or the installed shell
# fails to start.  Binaryen is the only such dependency; FindBinaryen.cmake
# leaves Binaryen_INSTALL_RPATH empty when the loader finds it unaided.
if(ENDO_HAS_WASM AND Binaryen_INSTALL_RPATH)
    set_property(TARGET endo-bin APPEND PROPERTY INSTALL_RPATH "${Binaryen_INSTALL_RPATH}")
endif()

install(TARGETS endo-bin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install runtime DLL dependencies alongside the executable (Windows/vcpkg).
# TARGET_RUNTIME_DLLS collects all DLLs from imported targets (e.g. vcpkg).
if(WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:endo-bin> DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/definitions/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/endo/definitions)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/completers/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/endo/completers)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/stdlib/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/endo/stdlib)

# Guards the RPATH above: the build-tree binary resolves its libraries even when
# the installed one cannot, so only an installed binary can prove the install is
# usable. Unix-only, where RPATH is the mechanism being tested.
if(ENDO_TESTING AND UNIX AND NOT EMSCRIPTEN)
    add_test(
        NAME installed-binary
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DCONFIG=$<CONFIG>
            -P ${CMAKE_SOURCE_DIR}/cmake/tests/TestInstalledBinary.cmake
    )
    set_tests_properties(installed-binary PROPERTIES TIMEOUT 120)
endif()

# Enable sanitizers and clang-tidy for all targets
enable_sanitizers(endo-shell)
enable_sanitizers(endo-bin)
enable_clang_tidy(endo-shell)
enable_clang_tidy(endo-bin)
enable_sanitizers(test-endo-shell)
enable_clang_tidy(test-endo-shell)
