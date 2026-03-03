// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/lexer/Lexer.hpp>

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace endo::editor_protocol
{

/// Editor position (0-based line and character).
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

/// Editor range (start and end positions).
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

/// Editor location (URI + range).
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

/// Editor text edit (a replacement within a document).
struct TextEdit
{
    Range range;
    std::string newText;
};

inline void to_json(nlohmann::json& j, TextEdit const& e)
{
    j = nlohmann::json { { "range", e.range }, { "newText", e.newText } };
}

/// Editor workspace edit (a set of edits across documents).
struct WorkspaceEdit
{
    std::map<std::string, std::vector<TextEdit>> changes; ///< URI -> edits
};

inline void to_json(nlohmann::json& j, WorkspaceEdit const& w)
{
    j = nlohmann::json { { "changes", nlohmann::json::object() } };
    for (auto const& [uri, edits]: w.changes)
        j["changes"][uri] = edits;
}

/// Converts an endo SourceLocationRange to an editor Range.
/// The endo lexer uses 0-based line and column; editor protocols also use 0-based.
/// @param loc The source location range from the endo lexer (0-based)
/// @return The corresponding editor Range (0-based)
[[nodiscard]] inline Range toRange(SourceLocationRange const& loc)
{
    return Range {
        .start = Position { .line = loc.begin.line, .character = loc.begin.column },
        .end = Position { .line = loc.end.line, .character = loc.end.column },
    };
}

} // namespace endo::editor_protocol
