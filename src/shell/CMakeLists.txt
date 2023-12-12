add_library(Shell STATIC
    AST.h
    Visitor.h
)


target_sources(Shell
  PUBLIC
    FILE_SET CXX_MODULES FILES
      Lexer.cpp
      UnixPipe.cpp
      TTY.cpp
      Shell.cpp
      Prompt.cpp
      ASTPrinter.cpp
      IRGenerator.cpp
      Parser.cpp
)

find_package(Threads REQUIRED)
# util for pty.h

# see https://github.com/llvm/llvm-project/issues/75108
# and https://github.com/ericniebler/range-v3/blob/f013aef2ae81f3661a560e7922a968665bedebff/include/meta/meta_fwd.hpp#L37
target_compile_definitions(Shell PUBLIC META_WORKAROUND_LLVM_28385)
target_link_libraries(Shell PUBLIC fmt::fmt-header-only vtparser crispy::core CoreVM InputEditor Threads::Threads util)

add_executable(endo
    main.cpp
)
target_link_libraries(endo PUBLIC Shell)

add_executable(test-endo
    test_main.cpp
    Lexer_test.cpp
    Shell_test.cpp
)
target_link_libraries(test-endo Shell Catch2::Catch2)

add_test(NAME test-endo COMMAND test-endo)
