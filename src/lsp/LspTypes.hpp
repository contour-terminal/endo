// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <endo-language/Lexer.hpp>
#include <nlohmann/json.hpp>

namespace endo::lsp
{

/// LSP Position (0-based line and character).
struct Position
{
    int line = 0;
    int character = 0;
};

inline void to_json(nlohmann::json& j, Position const& p)
{
    j = nlohmann::json { { "line", p.line }, { "character", p.character } };
}

inline void from_json(nlohmann::json const& j, Position& p)
{
    j.at("line").get_to(p.line);
    j.at("character").get_to(p.character);
}

/// LSP Range (start and end positions).
struct Range
{
    Position start;
    Position end;
};

inline void to_json(nlohmann::json& j, Range const& r)
{
    j = nlohmann::json { { "start", r.start }, { "end", r.end } };
}

inline void from_json(nlohmann::json const& j, Range& r)
{
    j.at("start").get_to(r.start);
    j.at("end").get_to(r.end);
}

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

/// Converts an endo SourceLocationRange to an LSP Range.
/// The endo lexer uses 1-based columns; LSP uses 0-based.
/// @param loc The source location range from the endo lexer
/// @return The corresponding LSP Range
[[nodiscard]] inline Range toRange(SourceLocationRange const& loc)
{
    return Range {
        .start =
            Position { .line = loc.begin.line, .character = loc.begin.column > 0 ? loc.begin.column - 1 : 0 },
        .end = Position { .line = loc.end.line, .character = loc.end.column > 0 ? loc.end.column - 1 : 0 },
    };
}

/// Checks if a 0-based LSP position falls within a source location range.
/// The lexer uses 1-based columns, so we convert during comparison.
/// @param range The source location range from the endo lexer
/// @param pos The 0-based LSP position
/// @return true if the position is within the range
[[nodiscard]] inline bool containsPosition(SourceLocationRange const& range, Position pos)
{
    // Convert lexer 1-based columns to 0-based for comparison
    auto const beginCol = range.begin.column > 0 ? range.begin.column - 1 : 0;
    auto const endCol = range.end.column > 0 ? range.end.column - 1 : 0;

    // Check if position is on or after the start
    if (pos.line < range.begin.line)
        return false;
    if (pos.line == range.begin.line && pos.character < beginCol)
        return false;

    // Check if position is before the end
    if (pos.line > range.end.line)
        return false;
    if (pos.line == range.end.line && pos.character >= endCol)
        return false;

    return true;
}

/// LSP Location (URI + range).
struct Location
{
    std::string uri;
    Range range;
};

inline void to_json(nlohmann::json& j, Location const& l)
{
    j = nlohmann::json { { "uri", l.uri }, { "range", l.range } };
}

inline void from_json(nlohmann::json const& j, Location& l)
{
    j.at("uri").get_to(l.uri);
    j.at("range").get_to(l.range);
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

} // namespace endo::lsp
