// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <editor-protocol/EditorTypes.hpp>

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace endo::lsp
{

// Re-export generic editor types into the LSP namespace for backward compatibility.
using endo::editor_protocol::Location;
using endo::editor_protocol::Position;
using endo::editor_protocol::Range;
using endo::editor_protocol::TextEdit;
using endo::editor_protocol::toRange;
using endo::editor_protocol::WorkspaceEdit;

/// LSP diagnostic severity levels.
enum class DiagnosticSeverity : int
{
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

/// LSP Diagnostic message.
struct Diagnostic
{
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string source;
    std::string message;
};

inline void to_json(nlohmann::json& j, Diagnostic const& d)
{
    j = nlohmann::json {
        { "range", d.range },
        { "severity", static_cast<int>(d.severity) },
        { "source", d.source },
        { "message", d.message },
    };
}

/// LSP MarkupContent for rich hover text.
struct MarkupContent
{
    std::string kind = "markdown"; ///< "plaintext" or "markdown"
    std::string value;
};

inline void to_json(nlohmann::json& j, MarkupContent const& m)
{
    j = nlohmann::json { { "kind", m.kind }, { "value", m.value } };
}

/// LSP Hover result.
struct Hover
{
    MarkupContent contents;
    std::optional<Range> range;
};

inline void to_json(nlohmann::json& j, Hover const& h)
{
    j = nlohmann::json { { "contents", h.contents } };
    if (h.range.has_value())
        j["range"] = *h.range;
}

/// LSP SemanticTokensLegend.
struct SemanticTokensLegend
{
    std::vector<std::string> tokenTypes;
    std::vector<std::string> tokenModifiers;
};

inline void to_json(nlohmann::json& j, SemanticTokensLegend const& l)
{
    j = nlohmann::json { { "tokenTypes", l.tokenTypes }, { "tokenModifiers", l.tokenModifiers } };
}

/// LSP SemanticTokens result (delta-encoded data array).
struct SemanticTokens
{
    std::vector<int> data;
};

inline void to_json(nlohmann::json& j, SemanticTokens const& t)
{
    j = nlohmann::json { { "data", t.data } };
}

/// LSP TextDocumentIdentifier.
struct TextDocumentIdentifier
{
    std::string uri;
};

inline void from_json(nlohmann::json const& j, TextDocumentIdentifier& t)
{
    j.at("uri").get_to(t.uri);
}

/// LSP TextDocumentItem (for didOpen).
struct TextDocumentItem
{
    std::string uri;
    std::string languageId;
    int version = 0;
    std::string text;
};

inline void from_json(nlohmann::json const& j, TextDocumentItem& t)
{
    j.at("uri").get_to(t.uri);
    j.at("languageId").get_to(t.languageId);
    j.at("version").get_to(t.version);
    j.at("text").get_to(t.text);
}

/// LSP VersionedTextDocumentIdentifier (for didChange).
struct VersionedTextDocumentIdentifier
{
    std::string uri;
    int version = 0;
};

inline void from_json(nlohmann::json const& j, VersionedTextDocumentIdentifier& t)
{
    j.at("uri").get_to(t.uri);
    j.at("version").get_to(t.version);
}

/// LSP TextDocumentContentChangeEvent (full sync: just text).
struct TextDocumentContentChangeEvent
{
    std::string text;
};

inline void from_json(nlohmann::json const& j, TextDocumentContentChangeEvent& e)
{
    j.at("text").get_to(e.text);
}

/// LSP ParameterInformation for signature help.
struct ParameterInformation
{
    std::string label;
    std::optional<std::string> documentation;
};

inline void to_json(nlohmann::json& j, ParameterInformation const& p)
{
    j = nlohmann::json { { "label", p.label } };
    if (p.documentation.has_value())
        j["documentation"] = *p.documentation;
}

/// LSP SignatureInformation for signature help.
struct SignatureInformation
{
    std::string label;
    std::optional<std::string> documentation;
    std::vector<ParameterInformation> parameters;
};

inline void to_json(nlohmann::json& j, SignatureInformation const& s)
{
    j = nlohmann::json { { "label", s.label }, { "parameters", s.parameters } };
    if (s.documentation.has_value())
        j["documentation"] = *s.documentation;
}

/// LSP SignatureHelp result.
struct SignatureHelp
{
    std::vector<SignatureInformation> signatures;
    int activeSignature = 0;
    int activeParameter = 0;
};

inline void to_json(nlohmann::json& j, SignatureHelp const& h)
{
    j = nlohmann::json {
        { "signatures", h.signatures },
        { "activeSignature", h.activeSignature },
        { "activeParameter", h.activeParameter },
    };
}

/// LSP SymbolKind enumeration (subset relevant to endo).
enum class SymbolKind : int
{
    File = 1,
    Property = 7, ///< Property bindings (get/set)
    Field = 8,    ///< Record fields
    Enum = 10,    ///< Discriminated union types
    Function = 12,
    Variable = 13,
    EnumMember = 22,    ///< Union variant constructors
    Struct = 23,        ///< Record types
    TypeParameter = 26, ///< For pattern bindings in match arms
};

/// LSP DocumentSymbol (hierarchical symbol representation).
struct DocumentSymbol
{
    std::string name;
    std::optional<std::string> detail; ///< Type signature or other detail string
    SymbolKind kind = SymbolKind::Variable;
    Range range;          ///< Full span of the symbol (including body)
    Range selectionRange; ///< Name span (for highlighting)
    std::vector<DocumentSymbol> children;
};

inline void to_json(nlohmann::json& j, DocumentSymbol const& s)
{
    j = nlohmann::json {
        { "name", s.name },
        { "kind", static_cast<int>(s.kind) },
        { "range", s.range },
        { "selectionRange", s.selectionRange },
    };
    if (s.detail.has_value())
        j["detail"] = *s.detail;
    if (!s.children.empty())
        j["children"] = s.children;
}

/// LSP DocumentHighlightKind enumeration.
enum class DocumentHighlightKind : int
{
    Text = 1,  ///< A textual occurrence
    Read = 2,  ///< Read-access of a symbol (e.g. variable usage)
    Write = 3, ///< Write-access of a symbol (e.g. variable definition)
};

/// LSP DocumentHighlight (a range to highlight in the document).
struct DocumentHighlight
{
    Range range;
    DocumentHighlightKind kind = DocumentHighlightKind::Text;
};

inline void to_json(nlohmann::json& j, DocumentHighlight const& h)
{
    j = nlohmann::json { { "range", h.range }, { "kind", static_cast<int>(h.kind) } };
}

/// LSP CompletionItemKind enumeration (subset relevant to endo).
enum class CompletionItemKind : int
{
    Text = 1,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Module = 9,
    Property = 10,
    Keyword = 14,
    Snippet = 15,
    EnumMember = 20,
};

/// LSP CompletionItem for textDocument/completion.
struct LspCompletionItem
{
    std::string label;
    CompletionItemKind kind = CompletionItemKind::Text;
    std::string detail;
    std::string documentation;
    std::string insertText;
};

inline void to_json(nlohmann::json& j, LspCompletionItem const& c)
{
    j = nlohmann::json {
        { "label", c.label },
        { "kind", static_cast<int>(c.kind) },
    };
    if (!c.detail.empty())
        j["detail"] = c.detail;
    if (!c.documentation.empty())
        j["documentation"] = c.documentation;
    if (!c.insertText.empty())
        j["insertText"] = c.insertText;
}

/// LSP InlayHintKind enumeration.
enum class InlayHintKind : int
{
    Type = 1,      ///< Type annotation hint
    Parameter = 2, ///< Parameter name hint
};

/// LSP InlayHint for inline virtual text.
struct InlayHint
{
    Position position;                  ///< Position where the hint is rendered
    std::string label;                  ///< The hint text (e.g., ": int")
    InlayHintKind kind = InlayHintKind::Type; ///< Kind of inlay hint
    bool paddingLeft = false;           ///< Whether to add padding before the hint
    bool paddingRight = false;          ///< Whether to add padding after the hint
    std::optional<std::string> tooltip; ///< Optional tooltip text
};

inline void to_json(nlohmann::json& j, InlayHint const& h)
{
    j = nlohmann::json {
        { "position", h.position },
        { "label", h.label },
        { "kind", static_cast<int>(h.kind) },
        { "paddingLeft", h.paddingLeft },
        { "paddingRight", h.paddingRight },
    };
    if (h.tooltip.has_value())
        j["tooltip"] = *h.tooltip;
}

} // namespace endo::lsp
