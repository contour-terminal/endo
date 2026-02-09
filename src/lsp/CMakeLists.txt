# Endo LSP Server Library
# Provides Language Server Protocol support for Endo shell scripts.
# Reuses the endo language library for lexing, parsing, and AST.

add_library(endo-lsp STATIC
    # JSON-RPC transport
    JsonRpc.hpp
    JsonRpc.cpp

    # LSP protocol types
    LspTypes.hpp

    # Document management
    DocumentStore.hpp
    DocumentStore.cpp

    # Language intelligence providers
    DiagnosticsProvider.hpp
    DiagnosticsProvider.cpp
    SemanticTokens.hpp
    SemanticTokens.cpp
    HoverProvider.hpp
    HoverProvider.cpp

    # Server core
    LspServer.hpp
    LspServer.cpp
)

target_link_libraries(endo-lsp PUBLIC endo nlohmann_json::nlohmann_json)
target_include_directories(endo-lsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Tests
add_executable(test-endo-lsp
    test_main.cpp
    LspServer_test.cpp
)
target_link_libraries(test-endo-lsp endo-lsp Catch2::Catch2)
add_test(NAME test-endo-lsp COMMAND test-endo-lsp)

# Enable sanitizers for all targets
enable_sanitizers(endo-lsp)
enable_sanitizers(test-endo-lsp)
