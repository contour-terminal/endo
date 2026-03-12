// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/codegen/IRGenerator.hpp>

#include <CoreVM/CoreVM.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace endo::ast
{
struct Statement;
}

namespace endo
{

/// Describes a loaded module's exports and metadata.
///
/// Populated after compilation of a `.endo` file or inline `module Name = ... end` block.
/// Used by the IRGenerator to resolve qualified access (`Module.member`) and by
/// `open` to bring names into the current scope.
struct ModuleDescriptor
{
    std::string name; ///< Module name (e.g., "Math")

    std::filesystem::path sourcePath; ///< Absolute path (cache key for file-based modules)

    /// Exported function definitions (name -> persisted function metadata).
    std::unordered_map<std::string, FSharpPersistentState::PersistedFunction> functions;

    /// Exported value bindings.
    std::vector<FSharpPersistentState::PersistedValueBinding> valueBindings;

    /// Exported product types (record types).
    std::vector<CoreVM::IRProgram::CustomProductType> productTypes;

    /// Exported sum types (discriminated unions).
    std::vector<CoreVM::IRProgram::CustomSumType> sumTypes;

    /// Constructor info for pattern matching.
    struct ConstructorInfo
    {
        std::string typeName;  ///< Union type name
        uint8_t variantIndex;  ///< Variant tag index
        uint8_t payloadSlots;  ///< Number of payload slots
    };

    /// Maps constructor names to their metadata.
    std::unordered_map<std::string, ConstructorInfo> constructors;

    /// Names marked as `let private` or `type private` (not exported).
    std::unordered_set<std::string> privateNames;

    /// AST nodes retained to keep function body pointers valid for inlining.
    std::vector<std::unique_ptr<ast::Statement>> retainedASTs;

    /// Returns true if the given name is private (not accessible from outside).
    [[nodiscard]] bool isPrivate(std::string const& memberName) const
    {
        return privateNames.contains(memberName);
    }

    /// Returns true if the given name is a public export.
    [[nodiscard]] bool isPublic(std::string const& memberName) const { return !isPrivate(memberName); }
};

} // namespace endo
