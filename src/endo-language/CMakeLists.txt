# Endo Language Core Library
# This library contains the lexer, parser, AST, IR generator, and related components
# for the Endo shell language.

add_library(endo STATIC
    # Lexical Analysis
    Lexer.hpp
    Lexer.cpp

    # AST
    AST.hpp
    Visitor.hpp
    ASTPrinter.hpp
    ASTPrinter.cpp

    # Parser
    Parser.hpp
    Parser.cpp

    # IR Generation
    IRGenerator.hpp
    IRGenerator.cpp

    # Diagnostics
    DiagnosticsAdapter.hpp
    DiagnosticsAdapter.cpp

    # Logging (used by parser tracing)
    LogCategories.hpp
    LogCategories.cpp
    LogConfig.hpp
    ScopedLogger.hpp
)

target_link_libraries(endo PUBLIC CoreVM crispy::core)
target_include_directories(endo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Tracing support
if(ENDO_TRACE_PARSER)
    target_compile_definitions(endo PUBLIC ENDO_TRACE_PARSER)
endif()
if(ENDO_TRACE_LEXER)
    target_compile_definitions(endo PUBLIC ENDO_TRACE_LEXER)
endif()

# Tests
add_executable(test-endo-language
    test_main.cpp
    Lexer_test.cpp
    Diagnostics_test.cpp
)
target_link_libraries(test-endo-language endo Catch2::Catch2)
add_test(NAME test-endo-language COMMAND test-endo-language)

enable_sanitizers(endo)
enable_sanitizers(test-endo-language)
