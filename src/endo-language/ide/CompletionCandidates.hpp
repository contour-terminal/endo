// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ide/CompletionItem.hpp>
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace endo
{

/// @brief Returns F# keyword completion candidates.
/// Keywords: let, rec, mut, fun, match, with, when, if, then, else, type, of, try, finally, true, false.
[[nodiscard]] std::vector<CompletionCandidate> keywordCandidates();

/// @brief Returns shell builtin completion candidates.
/// Builtins: cd, exit, export, set, unset, read, echo, sleep, print, println, etc.
[[nodiscard]] std::vector<CompletionCandidate> builtinCandidates();

/// @brief Returns control flow keyword candidates.
/// Keywords: if/then/else/elif, for/while/do/end, in, return, break, continue.
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
/// @param pipelineElementType When non-empty, restricts underscore completions to this type only.
/// @param moduleFunctions Module-level functions per type (e.g., Option.map, DateTime.now).
/// @return Matching completion candidates.
[[nodiscard]] std::vector<CompletionCandidate> dotAccessCandidates(
    std::string const& objectPart,
    std::string const& memberPrefix,
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> const& recordFields,
    std::unordered_map<std::string, std::string> const& variableTypes = {},
    std::string const& pipelineElementType = {},
    ModuleFunctionMap const& moduleFunctions = {});

/// @brief Checks if a command is a builtin whose argument space is fully handled.
///
/// Returns true for builtins/properties that either accept enumerated values (e.g., shell_prompt_preset)
/// or free-form input (e.g., shell_prompt_indicator), indicating that generic constructor
/// and symbol candidates should not be offered.
///
/// @param commandName The command name to check.
/// @return true if the command's argument completion is fully handled.
[[nodiscard]] bool isBuiltinWithArgumentCompletion(std::string const& commandName);

/// @brief Returns argument value completion candidates for a given builtin command.
///
/// For properties/builtins that accept enumerated string values (e.g., shell_prompt_preset,
/// shell_prompt_layout), returns matching value candidates filtered by prefix.
///
/// @param commandName The property or builtin command name (e.g., "shell_prompt_preset").
/// @param prefix The argument prefix to filter by (may be empty for all values).
/// @return Matching completion candidates with CompletionKind::EnumValue.
[[nodiscard]] std::vector<CompletionCandidate> builtinArgumentCandidates(std::string const& commandName,
                                                                         std::string const& prefix);

/// @brief Returns standard library function completion candidates.
/// Includes type conversion, string operations, list operations, HOFs, transforms, and system functions.
[[nodiscard]] std::vector<CompletionCandidate> standardLibraryCandidates();

/// @brief Resolves the record type name of the pipeline source from a full input line.
///
/// Looks for the first command before `|>` and maps it via commandOutputTypes.
/// Supports both simple commands ("ls" -> "FileInfo") and multi-word commands
/// with NUL-separated keys ("docker\0ps" -> "DockerContainer").
///
/// @param fullInput The complete input line (e.g., "ls |> map _.").
/// @param commandOutputTypes Command name -> record type name mapping.
/// @return The record type name, or empty string if no pipeline or no matching type.
[[nodiscard]] std::string resolvePipelineSourceType(
    std::string_view fullInput, std::unordered_map<std::string, std::string> const& commandOutputTypes);

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
