# Endo Language Core Library
# This library contains the lexer, parser, AST, IR generator, and related components
# for the Endo shell language.

add_library(endo STATIC
    # Lexical Analysis
    lexer/Lexer.hpp
    lexer/Lexer.cpp
    lexer/TokenClassification.hpp
    lexer/ContextAwareTokenizer.hpp
    lexer/ContextAwareTokenizer.cpp

    # AST
    ast/AST.hpp
    ast/Visitor.hpp
    ast/ASTPrinter.hpp
    ast/ASTPrinter.cpp
    ast/Pattern.hpp
    ast/Pattern.cpp

    # Parser
    parser/Parser.hpp
    parser/Parser.cpp
    parser/DiagnosticsAdapter.hpp
    parser/DiagnosticsAdapter.cpp

    # Type System
    types/Type.hpp
    types/Type.cpp
    types/TypeEnv.hpp
    types/TypeEnv.cpp
    types/TypeInferencer.hpp
    types/TypeInferencer.cpp
    types/Unification.hpp
    types/Unification.cpp

    # Semantic Analysis
    sema/TypeRegistry.hpp
    sema/TypeRegistry.cpp
    sema/BuiltinDescriptors.hpp
    sema/BuiltinDescriptors.cpp

    # IR Generation
    codegen/IRGenerator.hpp
    codegen/IRGenerator.cpp
    codegen/PatternIRGenerator.hpp
    codegen/PatternIRGenerator.cpp

    # IDE Intelligence (completion, hover, diagnostics)
    ide/CompletionItem.hpp
    ide/CompletionContext.hpp
    ide/CompletionContext.cpp
    ide/CompletionCandidates.hpp
    ide/CompletionCandidates.cpp
    ide/Completer.hpp
    ide/Completer.cpp
    ide/TypeRegistryCompletionAdapter.hpp
    ide/TypeRegistryCompletionAdapter.cpp
    ide/HoverInfo.hpp
    ide/HoverProvider.hpp
    ide/HoverProvider.cpp
    ide/DiagnosticsCollector.hpp
    ide/DiagnosticsCollector.cpp

    # Builtin Registration (single source of truth for signatures + shared implementations)
    builtins/BuiltinSignatures.hpp
    builtins/BuiltinSignatures.cpp
    builtins/BuiltinImpls.hpp
    builtins/BuiltinImpls.cpp
    builtins/PropertyDescriptors.hpp
    builtins/PropertyDescriptors.cpp
    builtins/StdlibDescriptors.hpp
    builtins/StdlibDescriptors.cpp
    builtins/StubRuntime.hpp

    # Source Code Formatter
    format/FormatConfig.hpp
    format/FormatConfig.cpp
    format/SourceFormatter.hpp
    format/SourceFormatter.cpp

    # Logging (used by parser tracing)
    LogCategories.hpp
    LogCategories.cpp
    LogConfig.hpp
    ScopedLogger.hpp
)

target_link_libraries(endo PUBLIC CoreVM crispy::core nlohmann_json::nlohmann_json)
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
        lexer/Lexer_test.cpp
        ast/Pattern_test.cpp
        parser/Parser_test.cpp
        parser/Diagnostics_test.cpp
        types/Type_test.cpp
        types/TypeInferencer_test.cpp
        codegen/ComputationExpressions_test.cpp
        codegen/CompleterScripts_test.cpp
        codegen/StringBuiltins_test.cpp
        builtins/JsonQuery_test.cpp
        builtins/StdlibDescriptors_test.cpp

        ide/CompletionCandidates_test.cpp
        ide/CompletionContext_test.cpp
        ide/Completer_test.cpp
        ide/HoverProvider_test.cpp
        ide/DiagnosticsCollector_test.cpp
        ide/TypeRegistryCompletionAdapter_test.cpp

        format/SourceFormatter_test.cpp
    )
    target_link_libraries(test-endo-language endo Catch2::Catch2)
    target_compile_definitions(test-endo-language PRIVATE
        ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
        ENDO_COMPLETERS_DIR="${CMAKE_SOURCE_DIR}/data/completers"
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
