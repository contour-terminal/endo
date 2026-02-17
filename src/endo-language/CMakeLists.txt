# Endo Language Core Library
# This library contains the lexer, parser, AST, IR generator, and related components
# for the Endo shell language.

add_library(endo STATIC
    # Lexical Analysis
    Lexer.hpp
    Lexer.cpp
    TokenClassification.hpp
    ContextAwareTokenizer.hpp
    ContextAwareTokenizer.cpp

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
    PatternIRGenerator.hpp
    PatternIRGenerator.cpp

    # Type System
    Type.hpp
    Type.cpp
    TypeEnv.hpp
    TypeEnv.cpp
    TypeInferencer.hpp
    TypeInferencer.cpp
    Unification.hpp
    Unification.cpp

    # Pattern AST
    Pattern.hpp
    Pattern.cpp

    # Diagnostics
    DiagnosticsAdapter.hpp
    DiagnosticsAdapter.cpp
    DiagnosticsCollector.hpp
    DiagnosticsCollector.cpp

    # Builtin Registration (single source of truth for signatures + shared implementations)
    BuiltinSignatures.hpp
    BuiltinSignatures.cpp
    BuiltinImpls.hpp
    BuiltinImpls.cpp

    # Hover & Stub Runtime
    HoverInfo.hpp
    HoverProvider.hpp
    HoverProvider.cpp
    StubRuntime.hpp

    # Completion (shared between shell and LSP)
    CompletionItem.hpp
    CompletionContext.hpp
    CompletionContext.cpp
    CompletionCandidates.hpp
    CompletionCandidates.cpp
    Completer.hpp
    Completer.cpp

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

if(ENDO_TESTING)
    # Tests
    add_executable(test-endo-language
        test_main.cpp
        TestHelper.hpp
        TestHelper.cpp
        Lexer_test.cpp
        Diagnostics_test.cpp
        DiagnosticsCollector_test.cpp
        HoverProvider_test.cpp
        Type_test.cpp
        Pattern_test.cpp
        Parser_test.cpp
        IRGenerator_test.cpp
        TypeInferencer_test.cpp
        CompletionCandidates_test.cpp
        CompletionContext_test.cpp
        Completer_test.cpp
    )
    target_link_libraries(test-endo-language endo Catch2::Catch2)
    target_compile_definitions(test-endo-language PRIVATE
        ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
    )
    add_test(NAME test-endo-language COMMAND test-endo-language)

    enable_sanitizers(endo)
    enable_sanitizers(test-endo-language)
endif()

# WASM playground target (Emscripten only)
if(EMSCRIPTEN)
    add_executable(endo-wasm wasm_bridge.cpp)
    target_link_libraries(endo-wasm PRIVATE endo)
    set_target_properties(endo-wasm PROPERTIES
        OUTPUT_NAME "endo-playground"
        SUFFIX ".js"
    )
    target_link_options(endo-wasm PRIVATE
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_NAME='EndoModule'"
        "SHELL:-s EXPORTED_FUNCTIONS=['_endo_eval','_endo_reset','_endo_version','_malloc','_free']"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=16777216"
        "SHELL:-s MAXIMUM_MEMORY=268435456"
        "SHELL:-s ENVIRONMENT='web,worker'"
        "SHELL:-s DISABLE_EXCEPTION_CATCHING=0"
        "SHELL:-s NO_EXIT_RUNTIME=1"
    )
    target_compile_options(endo-wasm PRIVATE -Os)
endif()
