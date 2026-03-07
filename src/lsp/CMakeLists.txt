# Endo LSP Server Library
# Provides Language Server Protocol support for Endo shell scripts.
# Reuses the endo language library for lexing, parsing, and AST.

add_library(endo-lsp STATIC
    # LSP protocol types
    LspTypes.hpp

    # Language intelligence providers
    SymbolCollector.hpp
    SymbolCollector.cpp
    DiagnosticsProvider.hpp
    DiagnosticsProvider.cpp
    SemanticTokens.hpp
    SemanticTokens.cpp
    HoverProvider.hpp
    HoverProvider.cpp
    DefinitionProvider.hpp
    DefinitionProvider.cpp
    ReferencesProvider.hpp
    ReferencesProvider.cpp
    SignatureHelpProvider.hpp
    SignatureHelpProvider.cpp
    DocumentHighlightProvider.hpp
    DocumentHighlightProvider.cpp
    DocumentSymbolProvider.hpp
    DocumentSymbolProvider.cpp
    RenameProvider.hpp
    RenameProvider.cpp
    CompletionProvider.hpp
    CompletionProvider.cpp
    FormattingProvider.hpp
    FormattingProvider.cpp
    InlayHintProvider.hpp
    InlayHintProvider.cpp
    FoldingRangeProvider.hpp
    FoldingRangeProvider.cpp
    SelectionRangeProvider.hpp
    SelectionRangeProvider.cpp
    CodeActionProvider.hpp
    CodeActionProvider.cpp
    RangeFormattingProvider.hpp
    RangeFormattingProvider.cpp
    OnTypeFormattingProvider.hpp
    OnTypeFormattingProvider.cpp
    WorkspaceSymbolProvider.hpp
    WorkspaceSymbolProvider.cpp
    CallHierarchyProvider.hpp
    CallHierarchyProvider.cpp
    TypeDefinitionProvider.hpp
    TypeDefinitionProvider.cpp
    DocumentLinkProvider.hpp
    DocumentLinkProvider.cpp
    CodeLensProvider.hpp
    CodeLensProvider.cpp
    ProgressReporter.hpp
    ProgressReporter.cpp
    InlineValueProvider.hpp
    InlineValueProvider.cpp

    # Server core
    LspServer.hpp
    LspServer.cpp
)

target_link_libraries(endo-lsp PUBLIC endo-editor-protocol endo nlohmann_json::nlohmann_json)
target_include_directories(endo-lsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Tests
add_executable(test-endo-lsp
    test_main.cpp
    LspServer_test.cpp
)
target_link_libraries(test-endo-lsp endo-lsp Catch2::Catch2)
add_test(NAME test-endo-lsp COMMAND test-endo-lsp)

# Enable sanitizers and clang-tidy for all targets
enable_sanitizers(endo-lsp)
enable_clang_tidy(endo-lsp)
enable_sanitizers(test-endo-lsp)
