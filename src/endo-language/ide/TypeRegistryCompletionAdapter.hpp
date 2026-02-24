// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ide/CompletionItem.hpp>

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypeRegistry.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

/// Map from type name to its module-level functions.
using ModuleFunctionMap = std::unordered_map<std::string, std::vector<CoreVM::ModuleFunctionInfo>>;

/// @brief Derives record field completion data from the TypeRegistry.
///
/// Iterates Product types with named fields, converting FieldInfo to RecordFieldInfo.
/// Uses nestedTypeName for Object-typed fields and LiteralType name for primitives.
///
/// @param registry The type registry to extract fields from.
/// @return Map from type name to vector of RecordFieldInfo.
[[nodiscard]] std::unordered_map<std::string, std::vector<RecordFieldInfo>> builtinRecordFields(
    CoreVM::TypeRegistry const& registry);

/// @brief Derives module function data from the TypeRegistry.
///
/// Iterates all types with non-empty moduleFunctions.
///
/// @param registry The type registry to extract module functions from.
/// @return Map from type name to vector of ModuleFunctionInfo.
[[nodiscard]] ModuleFunctionMap builtinModuleFunctions(CoreVM::TypeRegistry const& registry);

/// @brief Generates constructor completion candidates from the TypeRegistry.
///
/// Iterates Sum types (Option, Result) and emits variant names as constructors.
/// Skips List (internal type not user-constructible).
///
/// @param registry The type registry to extract constructors from.
/// @return Vector of constructor completion candidates.
[[nodiscard]] std::vector<CompletionCandidate> constructorCandidatesFromRegistry(
    CoreVM::TypeRegistry const& registry);

/// @brief Generates stdlib completion candidates for module functions.
///
/// Emits "TypeName.method" entries (e.g., "DateTime.now", "Size.fromBytes") for
/// the standard library candidate list.
///
/// @param registry The type registry to extract module functions from.
/// @return Vector of stdlib function completion candidates.
[[nodiscard]] std::vector<CompletionCandidate> moduleFunctionStdLibCandidates(
    CoreVM::TypeRegistry const& registry);

/// @brief Derives command -> output type name mappings from the TypeRegistry.
///
/// Iterates types with non-empty producingCommand and emits command -> type name entries.
///
/// @param registry The type registry to extract command mappings from.
/// @return Map from command name to record type name.
[[nodiscard]] std::unordered_map<std::string, std::string> builtinCommandOutputTypes(
    CoreVM::TypeRegistry const& registry);

} // namespace endo
