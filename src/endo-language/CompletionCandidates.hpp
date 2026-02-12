// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <endo-language/CompletionItem.hpp>

namespace endo
{

/// @brief Returns F# keyword completion candidates.
/// Keywords: let, rec, mut, fun, match, with, when, if, then, else, type, of, try, finally, true, false.
[[nodiscard]] std::vector<CompletionCandidate> keywordCandidates();

/// @brief Returns shell builtin completion candidates.
/// Builtins: cd, exit, export, set, unset, read, echo, sleep, print, println, etc.
[[nodiscard]] std::vector<CompletionCandidate> builtinCandidates();

/// @brief Returns shell control flow keyword candidates.
/// Keywords: if/then/else/elif/fi, for/while/do/done, case/esac/in, function, return, break, continue.
[[nodiscard]] std::vector<CompletionCandidate> shellKeywordCandidates();

/// @brief Returns type constructor candidates.
/// Constructors: Some, None, Ok, Error.
[[nodiscard]] std::vector<CompletionCandidate> constructorCandidates();

/// @brief Returns dot-access completion candidates.
///
/// Handles four scenarios:
/// - "Option." -> Option module methods (map, bind, defaultValue)
/// - "_." -> all record fields from all types (with type info in description)
/// - "knownVar." -> only fields of the variable's known record type (when in variableTypes)
/// - "unknownVar." -> both Option methods and record fields (fallback)
///
/// @param objectPart The part before the last dot (e.g., "Option", "_", "myVar").
/// @param memberPrefix The part after the last dot (may be empty).
/// @param recordFields Record type fields (type name -> field info with types).
/// @param variableTypes Variable name -> record type name for type-specific completion.
/// @return Matching completion candidates.
[[nodiscard]] std::vector<CompletionCandidate> dotAccessCandidates(
    std::string const& objectPart,
    std::string const& memberPrefix,
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> const& recordFields,
    std::unordered_map<std::string, std::string> const& variableTypes = {});

/// @brief Returns symbol-based completion candidates from the given definitions.
///
/// Formats function signatures (e.g., "add(x, y)") and value binding descriptions
/// ("value", "mutable value") into completion candidates.
///
/// @param symbols The available symbol definitions.
/// @return Completion candidates for all symbols.
[[nodiscard]] std::vector<CompletionCandidate> symbolCandidates(
    std::vector<SymbolDefinitionInfo> const& symbols);

} // namespace endo
