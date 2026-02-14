# Endo Shell Runtime
# This library contains the shell execution runtime, builtins, job control, etc.
# It depends on the endo language library for parsing and IR generation.

add_library(endo-shell STATIC
    CommandResolver.hpp
    CommandResolver.cpp
    Completer.hpp
    Completer.cpp
    CompletionAdapter.hpp
    CompletionAdapter.cpp
    CompletionProvider.hpp
    CompletionProviders/BuiltinArgumentCompleter.hpp
    CompletionProviders/BuiltinArgumentCompleter.cpp
    CompletionProviders/CommandCompleter.hpp
    CompletionProviders/CommandCompleter.cpp
    CompletionProviders/FSharpCompleter.hpp
    CompletionProviders/FSharpCompleter.cpp
    CompletionProviders/FileCompleter.hpp
    CompletionProviders/FileCompleter.cpp
    CompletionProviders/HistoryCompleter.hpp
    CompletionProviders/HistoryCompleter.cpp
    CompletionProviders/OptionCompleter.hpp
    CompletionProviders/OptionCompleter.cpp
    CompletionProviders/LetBindingCompleter.hpp
    CompletionProviders/LetBindingCompleter.cpp
    CompletionProviders/VariableCompleter.hpp
    CompletionProviders/VariableCompleter.cpp
    Environment.hpp
    Error.hpp
    Gradient.hpp
    Gradient.cpp
    History.hpp
    History.cpp
    PersistentHistory.hpp
    PersistentHistory.cpp
    Job.hpp
    Job.cpp
    Pipe.hpp
    Pipe.cpp
    Platform.hpp
    Process.hpp
    Process.cpp
    ProcessGroup.hpp
    Prompt.hpp
    Prompt.cpp
    PromptComponent.hpp
    PromptComponent.cpp
    PromptConfig.hpp
    PromptLayoutEngine.hpp
    PromptLayoutEngine.cpp
    PromptModule.hpp
    PromptPresets.hpp
    PromptPresets.cpp
    Shell.hpp
    Shell.cpp
    SourceOffsetUtils.hpp
    SourceOffsetUtils.cpp
    SyntaxHighlighter.hpp
    SyntaxHighlighter.cpp
    SignalHandler.hpp
    SignalHandler.cpp
    Suggestions.hpp
    Suggestions.cpp
    TTY.hpp
    TTY.cpp
    commands/ProcessProvider.hpp
    commands/StructuredCommand.hpp
    commands/PsCommand.hpp
    commands/PsCommand.cpp
    commands/FileInfoProvider.hpp
    commands/LsCommand.hpp
    commands/LsCommand.cpp
    commands/JobProvider.hpp
    commands/JobsCommand.hpp
    commands/JobsCommand.cpp
    modules/BatteryModule.hpp
    modules/BatteryModule.cpp
    modules/ClockModule.hpp
    modules/ClockModule.cpp
    modules/DurationModule.hpp
    modules/DurationModule.cpp
    modules/ExitStatusModule.hpp
    modules/ExitStatusModule.cpp
    modules/FSharpModeModule.hpp
    modules/FSharpModeModule.cpp
    modules/GitModule.hpp
    modules/GitModule.cpp
    modules/HostnameModule.hpp
    modules/HostnameModule.cpp
    modules/IndicatorModule.hpp
    modules/IndicatorModule.cpp
    modules/PathModule.hpp
    modules/PathModule.cpp
    modules/StructuredOutputModule.hpp
    modules/StructuredOutputModule.cpp
    modules/ToolchainModule.hpp
    modules/ToolchainModule.cpp
    OutputDefinition.hpp
    OutputDefinitionRegistry.hpp
    OutputDefinitionRegistry.cpp
    OutputParser.hpp
    OutputParser.cpp
    TableFormatter.hpp
    TableFormatter.cpp
)

# Platform-specific implementations
if(WIN32)
    target_sources(endo-shell PRIVATE platform/WindowsPipe.cpp platform/WindowsTTY.cpp platform/WindowsProcess.cpp)
else()
    target_sources(endo-shell PRIVATE
        platform/LinuxProcessProvider.hpp
        platform/LinuxProcessProvider.cpp
        platform/LinuxFileInfoProvider.hpp
        platform/LinuxFileInfoProvider.cpp
    )
endif()

find_package(Threads REQUIRED)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(endo-shell PUBLIC endo vtparser tui)
else()
    # util for pty.h on POSIX systems
    target_link_libraries(endo-shell PUBLIC endo vtparser tui Threads::Threads util)
endif()

target_link_libraries(endo-shell PUBLIC yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json endo-http)

target_compile_definitions(endo-shell PRIVATE
    ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
)

if(ENDO_TRACE_VM)
  target_compile_definitions(endo-shell PUBLIC ENDO_TRACE_VM)
endif()

add_executable(endo-bin
    main.cpp
)
set_target_properties(endo-bin PROPERTIES OUTPUT_NAME endo)
target_link_libraries(endo-bin PUBLIC endo-shell endo-lsp)

add_executable(test-endo-shell
    test_main.cpp
    FSharpCompleter_test.cpp
    OutputDefinitionRegistry_test.cpp
    OutputParser_test.cpp
    PersistentHistory_test.cpp
    Shell_test.cpp
    Suggestions_test.cpp
    SyntaxHighlighter_test.cpp
)
target_link_libraries(test-endo-shell endo-shell Catch2::Catch2)
target_compile_definitions(test-endo-shell PRIVATE
    ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
)

add_test(NAME test-endo-shell COMMAND test-endo-shell)

# Enable sanitizers for all targets
enable_sanitizers(endo-shell)
enable_sanitizers(endo-bin)
enable_sanitizers(test-endo-shell)
