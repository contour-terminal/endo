add_library(Shell STATIC
    AST.hpp
    ASTPrinter.hpp
    ASTPrinter.cpp
    CommandCompleter.hpp
    CommandCompleter.cpp
    Completer.hpp
    Completer.cpp
    CompletionContext.hpp
    CompletionContext.cpp
    CompletionProvider.hpp
    DiagnosticsAdapter.hpp
    DiagnosticsAdapter.cpp
    Environment.hpp
    Error.hpp
    FileCompleter.hpp
    FileCompleter.cpp
    History.hpp
    History.cpp
    HistoryCompleter.hpp
    HistoryCompleter.cpp
    IRGenerator.hpp
    IRGenerator.cpp
    Job.hpp
    Job.cpp
    Lexer.hpp
    Lexer.cpp
    OptionCompleter.hpp
    OptionCompleter.cpp
    Parser.hpp
    Parser.cpp
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
    SignalHandler.hpp
    SignalHandler.cpp
    Suggestions.hpp
    Suggestions.cpp
    TTY.hpp
    TTY.cpp
    VariableCompleter.hpp
    VariableCompleter.cpp
    Visitor.hpp
)

# Platform-specific implementations
if(WIN32)
    target_sources(Shell PRIVATE WindowsPipe.cpp WindowsTTY.cpp WindowsProcess.cpp)
endif()

find_package(Threads REQUIRED)

# see https://github.com/llvm/llvm-project/issues/75108
# and https://github.com/ericniebler/range-v3/blob/f013aef2ae81f3661a560e7922a968665bedebff/include/meta/meta_fwd.hpp#L37
target_compile_definitions(Shell PUBLIC META_WORKAROUND_LLVM_28385)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(Shell PUBLIC vtparser crispy::core CoreVM tui)
else()
    # util for pty.h on POSIX systems
    target_link_libraries(Shell PUBLIC vtparser crispy::core CoreVM tui Threads::Threads util)
endif()


if(ENDO_TRACE_VM)
  target_compile_definitions(Shell PUBLIC ENDO_TRACE_VM)
endif()
if(ENDO_TRACE_PARSER)
  target_compile_definitions(Shell PUBLIC ENDO_TRACE_PARSER)
endif()
if(ENDO_TRACE_LEXER)
  target_compile_definitions(Shell PUBLIC ENDO_TRACE_LEXER)
endif()


add_executable(endo
    main.cpp
)
target_link_libraries(endo PUBLIC Shell)

add_executable(test-endo
    test_main.cpp
    Diagnostics_test.cpp
    Lexer_test.cpp
    Shell_test.cpp
)
target_link_libraries(test-endo Shell Catch2::Catch2)

add_test(NAME test-endo COMMAND test-endo)

# Enable sanitizers for all targets
enable_sanitizers(Shell)
enable_sanitizers(endo)
enable_sanitizers(test-endo)
