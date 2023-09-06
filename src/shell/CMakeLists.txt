add_library(Shell STATIC
    AST.h
    ASTPrinter.cpp ASTPrinter.h
    IRGenerator.cpp IRGenerator.h
    Lexer.cpp Lexer.h
    Parser.cpp Parser.h
    Prompt.cpp Prompt.h
    Shell.cpp Shell.h
    TTY.cpp TTY.h
    UnixPipe.cpp UnixPipe.h
    Visitor.h
)
target_link_libraries(Shell PUBLIC fmt::fmt-header-only vtparser crispy::core CoreVM InputEditor)

add_executable(crush
    main.cpp
)
target_link_libraries(crush PUBLIC Shell)

add_executable(test-crush
    test_main.cpp
    Lexer_test.cpp
    Shell_test.cpp
)
target_link_libraries(test-crush Shell Catch2::Catch2)

add_test(NAME test-crush COMMAND test-crush)
