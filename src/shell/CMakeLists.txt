# Endo Shell Runtime
# This library contains the shell execution runtime, builtins, job control, etc.
# It depends on the endo language library for parsing and IR generation.

add_library(endo-shell STATIC
    CommandResolver.hpp
    CommandResolver.cpp
    Completer.hpp
    Completer.cpp
    CompletionContext.hpp
    CompletionContext.cpp
    CompletionProvider.hpp
    CompletionProviders/CommandCompleter.hpp
    CompletionProviders/CommandCompleter.cpp
    CompletionProviders/FileCompleter.hpp
    CompletionProviders/FileCompleter.cpp
    CompletionProviders/HistoryCompleter.hpp
    CompletionProviders/HistoryCompleter.cpp
    CompletionProviders/OptionCompleter.hpp
    CompletionProviders/OptionCompleter.cpp
    CompletionProviders/VariableCompleter.hpp
    CompletionProviders/VariableCompleter.cpp
    Environment.hpp
    Error.hpp
    History.hpp
    History.cpp
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
)

# Platform-specific implementations
if(WIN32)
    target_sources(endo-shell PRIVATE WindowsPipe.cpp WindowsTTY.cpp WindowsProcess.cpp)
endif()

find_package(Threads REQUIRED)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(endo-shell PUBLIC endo vtparser tui)
else()
    # util for pty.h on POSIX systems
    target_link_libraries(endo-shell PUBLIC endo vtparser tui Threads::Threads util)
endif()

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
    Shell_test.cpp
    Suggestions_test.cpp
    SyntaxHighlighter_test.cpp
)
target_link_libraries(test-endo-shell endo-shell Catch2::Catch2)

add_test(NAME test-endo-shell COMMAND test-endo-shell)

# Enable sanitizers for all targets
enable_sanitizers(endo-shell)
enable_sanitizers(endo-bin)
enable_sanitizers(test-endo-shell)
