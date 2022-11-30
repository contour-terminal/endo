add_executable(contrair-shell
    contrair.cpp
    AST.h
    Executor.h
    Lexer.cpp Lexer.h
    Parser.cpp Parser.h
    Visitor.h
)
target_link_libraries(contrair PUBLIC vtparser)
