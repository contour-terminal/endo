add_library(Shell STATIC
    AST.h
    IRGenerator.cpp IRGenerator.h
    Lexer.cpp Lexer.h
    Parser.cpp Parser.h
    Visitor.h
)


target_sources(Shell
  PUBLIC
    FILE_SET CXX_MODULES FILES
      UnixPipe.cpp
      TTY.cpp
      Shell.cpp
      Prompt.cpp
      ASTPrinter.cpp
)

find_package(Threads REQUIRED)
# util for pty.h
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
