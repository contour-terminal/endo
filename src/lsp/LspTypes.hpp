// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <editor-protocol/EditorTypes.hpp>

#include <memory>
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
enum class DiagnosticSeverity : int // NOLINT(performance-enum-size)
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
    std::optional<nlohmann::json> data; ///< Optional structured data (e.g., raw suggestions for code actions)
};

inline void to_json(nlohmann::json& j, Diagnostic const& d)
{
    j = nlohmann::json {
        { "range", d.range },
        { "severity", static_cast<int>(d.severity) },
        { "source", d.source },
        { "message", d.message },
    };
    if (d.data.has_value())
        j["data"] = *d.data;
}

inline void from_json(nlohmann::json const& j, Diagnostic& d)
{
    j.at("range").get_to(d.range);
    d.severity = static_cast<DiagnosticSeverity>(j.at("severity").get<int>());
    j.at("source").get_to(d.source);
    j.at("message").get_to(d.message);
    if (j.contains("data"))
        d.data = j.at("data");
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
    std::optional<std::string> resultId; ///< Token result identifier for delta requests
    std::vector<int> data;
};

inline void to_json(nlohmann::json& j, SemanticTokens const& t)
{
    j = nlohmann::json { { "data", t.data } };
    if (t.resultId.has_value())
        j["resultId"] = *t.resultId;
}

/// LSP SemanticTokensEdit for delta responses.
struct SemanticTokensEdit
{
    int start = 0;         ///< Start offset in the previous data array
    int deleteCount = 0;   ///< Number of elements to delete
    std::vector<int> data; ///< Elements to insert
};

inline void to_json(nlohmann::json& j, SemanticTokensEdit const& e)
{
    j = nlohmann::json { { "start", e.start }, { "deleteCount", e.deleteCount } };
    if (!e.data.empty())
        j["data"] = e.data;
}

/// LSP SemanticTokensDelta response.
struct SemanticTokensDelta
{
    std::string resultId;
    std::vector<SemanticTokensEdit> edits;
};

inline void to_json(nlohmann::json& j, SemanticTokensDelta const& d)
{
    j = nlohmann::json { { "resultId", d.resultId }, { "edits", d.edits } };
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
enum class SymbolKind : int // NOLINT(performance-enum-size)
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
enum class DocumentHighlightKind : int // NOLINT(performance-enum-size)
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
enum class CompletionItemKind : int // NOLINT(performance-enum-size)
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
enum class InlayHintKind : int // NOLINT(performance-enum-size)
{
    Type = 1,      ///< Type annotation hint
    Parameter = 2, ///< Parameter name hint
};

/// LSP InlayHint for inline virtual text.
struct InlayHint
{
    Position position;                        ///< Position where the hint is rendered
    std::string label;                        ///< The hint text (e.g., ": int")
    InlayHintKind kind = InlayHintKind::Type; ///< Kind of inlay hint
    bool paddingLeft = false;                 ///< Whether to add padding before the hint
    bool paddingRight = false;                ///< Whether to add padding after the hint
    std::optional<std::string> tooltip;       ///< Optional tooltip text
    std::optional<nlohmann::json> data;       ///< Optional data for resolve
};

inline void to_json(nlohmann::json& j, InlayHint const& h)
{
    j = nlohmann::json {
        { "position", h.position },           { "label", h.label },
        { "kind", static_cast<int>(h.kind) }, { "paddingLeft", h.paddingLeft },
        { "paddingRight", h.paddingRight },
    };
    if (h.tooltip.has_value())
        j["tooltip"] = *h.tooltip;
    if (h.data.has_value())
        j["data"] = *h.data;
}

inline void from_json(nlohmann::json const& j, InlayHint& h)
{
    j.at("position").get_to(h.position);
    j.at("label").get_to(h.label);
    if (j.contains("kind"))
        h.kind = static_cast<InlayHintKind>(j.at("kind").get<int>());
    if (j.contains("paddingLeft"))
        j.at("paddingLeft").get_to(h.paddingLeft);
    if (j.contains("paddingRight"))
        j.at("paddingRight").get_to(h.paddingRight);
    if (j.contains("tooltip"))
        h.tooltip = j.at("tooltip").get<std::string>();
    if (j.contains("data"))
        h.data = j.at("data");
}

/// LSP FoldingRange for collapsible regions.
struct FoldingRange
{
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;
    std::optional<std::string> kind; ///< "comment", "imports", or "region"
};

inline void to_json(nlohmann::json& j, FoldingRange const& r)
{
    j = nlohmann::json {
        { "startLine", r.startLine },
        { "startCharacter", r.startCharacter },
        { "endLine", r.endLine },
        { "endCharacter", r.endCharacter },
    };
    if (r.kind.has_value())
        j["kind"] = *r.kind;
}

/// LSP SelectionRange for smart expand/shrink selection.
struct SelectionRange
{
    Range range;
    std::unique_ptr<SelectionRange> parent;
};

inline void to_json(nlohmann::json& j, SelectionRange const& s)
{
    j = nlohmann::json { { "range", s.range } };
    if (s.parent)
        j["parent"] = *s.parent;
}

/// LSP CodeActionContext (sent by client in codeAction request).
struct CodeActionContext
{
    std::vector<Diagnostic> diagnostics;
};

inline void from_json(nlohmann::json const& j, CodeActionContext& c)
{
    j.at("diagnostics").get_to(c.diagnostics);
}

/// LSP CodeAction for quick fixes and refactorings.
struct CodeAction
{
    std::string title;
    std::string kind = "quickfix";       ///< CodeActionKind (e.g., "quickfix", "refactor")
    std::optional<WorkspaceEdit> edit;   ///< Workspace edit to apply
    std::vector<Diagnostic> diagnostics; ///< Associated diagnostics
    bool isPreferred = false;            ///< Whether this is the preferred action
    std::optional<nlohmann::json> data;  ///< Optional data for resolve
};

inline void to_json(nlohmann::json& j, CodeAction const& a)
{
    j = nlohmann::json {
        { "title", a.title },
        { "kind", a.kind },
    };
    if (a.edit.has_value())
        j["edit"] = *a.edit;
    if (!a.diagnostics.empty())
        j["diagnostics"] = a.diagnostics;
    if (a.isPreferred)
        j["isPreferred"] = true;
    if (a.data.has_value())
        j["data"] = *a.data;
}

inline void from_json(nlohmann::json const& j, CodeAction& a)
{
    j.at("title").get_to(a.title);
    if (j.contains("kind"))
        j.at("kind").get_to(a.kind);
    if (j.contains("isPreferred"))
        j.at("isPreferred").get_to(a.isPreferred);
    if (j.contains("data"))
        a.data = j.at("data");
}

/// LSP SymbolInformation for workspace symbol search.
struct SymbolInformation
{
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    Location location;
    std::optional<std::string> containerName;
};

inline void to_json(nlohmann::json& j, SymbolInformation const& s)
{
    j = nlohmann::json {
        { "name", s.name },
        { "kind", static_cast<int>(s.kind) },
        { "location", s.location },
    };
    if (s.containerName.has_value())
        j["containerName"] = *s.containerName;
}

/// LSP CallHierarchyItem.
struct CallHierarchyItem
{
    std::string name;
    SymbolKind kind = SymbolKind::Function;
    std::string uri;
    Range range;
    Range selectionRange;
    std::optional<std::string> detail;
    std::optional<nlohmann::json> data;
};

inline void to_json(nlohmann::json& j, CallHierarchyItem const& c)
{
    j = nlohmann::json {
        { "name", c.name },   { "kind", static_cast<int>(c.kind) },   { "uri", c.uri },
        { "range", c.range }, { "selectionRange", c.selectionRange },
    };
    if (c.detail.has_value())
        j["detail"] = *c.detail;
    if (c.data.has_value())
        j["data"] = *c.data;
}

inline void from_json(nlohmann::json const& j, CallHierarchyItem& c)
{
    j.at("name").get_to(c.name);
    if (j.contains("kind"))
        c.kind = static_cast<SymbolKind>(j.at("kind").get<int>());
    j.at("uri").get_to(c.uri);
    j.at("range").get_to(c.range);
    j.at("selectionRange").get_to(c.selectionRange);
    if (j.contains("detail"))
        c.detail = j.at("detail").get<std::string>();
    if (j.contains("data"))
        c.data = j.at("data");
}

/// LSP CallHierarchyIncomingCall.
struct CallHierarchyIncomingCall
{
    CallHierarchyItem from;
    std::vector<Range> fromRanges;
};

inline void to_json(nlohmann::json& j, CallHierarchyIncomingCall const& c)
{
    j = nlohmann::json { { "from", c.from }, { "fromRanges", c.fromRanges } };
}

/// LSP CallHierarchyOutgoingCall.
struct CallHierarchyOutgoingCall
{
    CallHierarchyItem to;
    std::vector<Range> fromRanges;
};

inline void to_json(nlohmann::json& j, CallHierarchyOutgoingCall const& c)
{
    j = nlohmann::json { { "to", c.to }, { "fromRanges", c.fromRanges } };
}

/// LSP DocumentLink.
struct DocumentLink
{
    Range range;
    std::optional<std::string> target;
    std::optional<std::string> tooltip;
    std::optional<nlohmann::json> data;
};

inline void to_json(nlohmann::json& j, DocumentLink const& l)
{
    j = nlohmann::json { { "range", l.range } };
    if (l.target.has_value())
        j["target"] = *l.target;
    if (l.tooltip.has_value())
        j["tooltip"] = *l.tooltip;
    if (l.data.has_value())
        j["data"] = *l.data;
}

inline void from_json(nlohmann::json const& j, DocumentLink& l)
{
    j.at("range").get_to(l.range);
    if (j.contains("target"))
        l.target = j.at("target").get<std::string>();
    if (j.contains("tooltip"))
        l.tooltip = j.at("tooltip").get<std::string>();
    if (j.contains("data"))
        l.data = j.at("data");
}

/// LSP Command (for CodeLens and other features).
struct LspCommand
{
    std::string title;
    std::string command;
    std::optional<nlohmann::json> arguments;
};

inline void to_json(nlohmann::json& j, LspCommand const& c)
{
    j = nlohmann::json { { "title", c.title }, { "command", c.command } };
    if (c.arguments.has_value())
        j["arguments"] = *c.arguments;
}

/// LSP CodeLens.
struct CodeLens
{
    Range range;
    std::optional<LspCommand> command;
    std::optional<nlohmann::json> data;
};

inline void to_json(nlohmann::json& j, CodeLens const& l)
{
    j = nlohmann::json { { "range", l.range } };
    if (l.command.has_value())
        j["command"] = *l.command;
    if (l.data.has_value())
        j["data"] = *l.data;
}

inline void from_json(nlohmann::json const& j, CodeLens& l)
{
    j.at("range").get_to(l.range);
    if (j.contains("data"))
        l.data = j.at("data");
}

/// LSP MessageType for window notifications.
enum class MessageType : int // NOLINT(performance-enum-size)
{
    Error = 1,
    Warning = 2,
    Info = 3,
    Log = 4,
    Debug = 5,
};

/// LSP WorkDoneProgressBegin.
struct WorkDoneProgressBegin
{
    std::string title;
    std::optional<std::string> message;
    std::optional<int> percentage;
    bool cancellable = false;
};

inline void to_json(nlohmann::json& j, WorkDoneProgressBegin const& p)
{
    j = nlohmann::json { { "kind", "begin" }, { "title", p.title } };
    if (p.message.has_value())
        j["message"] = *p.message;
    if (p.percentage.has_value())
        j["percentage"] = *p.percentage;
    if (p.cancellable)
        j["cancellable"] = true;
}

/// LSP WorkDoneProgressReport.
struct WorkDoneProgressReport
{
    std::optional<std::string> message;
    std::optional<int> percentage;
    bool cancellable = false;
};

inline void to_json(nlohmann::json& j, WorkDoneProgressReport const& p)
{
    j = nlohmann::json { { "kind", "report" } };
    if (p.message.has_value())
        j["message"] = *p.message;
    if (p.percentage.has_value())
        j["percentage"] = *p.percentage;
    if (p.cancellable)
        j["cancellable"] = true;
}

/// LSP WorkDoneProgressEnd.
struct WorkDoneProgressEnd
{
    std::optional<std::string> message;
};

inline void to_json(nlohmann::json& j, WorkDoneProgressEnd const& p)
{
    j = nlohmann::json { { "kind", "end" } };
    if (p.message.has_value())
        j["message"] = *p.message;
}

/// LSP InlineValueVariableLookup for inline variable values during debugging.
struct InlineValueVariableLookup
{
    Range range;
    std::string variableName;
    bool caseSensitiveLookup = true;
};

inline void to_json(nlohmann::json& j, InlineValueVariableLookup const& v)
{
    j = nlohmann::json {
        { "range", v.range },
        { "variableName", v.variableName },
        { "caseSensitiveLookup", v.caseSensitiveLookup },
    };
}

} // namespace endo::lsp
