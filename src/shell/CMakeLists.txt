add_library(Shell STATIC
    AST.h
    DiagnosticsAdapter.h
    DiagnosticsAdapter.cpp
    Platform.h
    ProcessGroup.h
    Suggestions.h
    Suggestions.cpp
    Visitor.h
)

# Platform-agnostic module sources
set(SHELL_MODULE_SOURCES
    Lexer.cpp
    Process.cpp
    Pipe.cpp
    TTY.cpp
    Shell.cpp
    Prompt.cpp
    ASTPrinter.cpp
    IRGenerator.cpp
    Parser.cpp
)

# Platform-specific implementations
if(WIN32)
    list(APPEND SHELL_MODULE_SOURCES WindowsPipe.cpp WindowsTTY.cpp WindowsProcess.cpp)
endif()

target_sources(Shell
  PUBLIC
    FILE_SET CXX_MODULES FILES
      ${SHELL_MODULE_SOURCES}
)

find_package(Threads REQUIRED)

# see https://github.com/llvm/llvm-project/issues/75108
# and https://github.com/ericniebler/range-v3/blob/f013aef2ae81f3661a560e7922a968665bedebff/include/meta/meta_fwd.hpp#L37
target_compile_definitions(Shell PUBLIC META_WORKAROUND_LLVM_28385)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(Shell PUBLIC vtparser crispy::core CoreVM InputEditor)
else()
    # util for pty.h on POSIX systems
    target_link_libraries(Shell PUBLIC vtparser crispy::core CoreVM InputEditor Threads::Threads util)
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
