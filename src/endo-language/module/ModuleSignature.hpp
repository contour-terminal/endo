// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace endo
{

struct ModuleDescriptor;

/// Represents a single declaration in a module signature (.endoi) file.
///
/// Signature declarations describe the public API of a module:
/// - `val name : type` for value/function declarations
/// - `type Name = ...` for type declarations
struct SignatureEntry
{
    enum class Kind : uint8_t
    {
        Val,  ///< `val name : type`
        Type, ///< `type Name = ...`
    };

    Kind kind;
    std::string name;      ///< Name of the declared item
    std::string signature; ///< Type signature string (e.g., "int -> int -> int")
};

/// Parsed module signature from a `.endoi` file.
struct ModuleSignature
{
    std::string moduleName; ///< Module name derived from filename
    std::vector<SignatureEntry> entries;
    std::vector<std::string> warnings; ///< Warnings for malformed lines during parsing

    /// Returns true if the signature declares a member with the given name.
    [[nodiscard]] bool declares(std::string const& name) const;
};

/// Parses a module signature (.endoi) file.
///
/// @param path Path to the `.endoi` file
/// @return Parsed signature, or std::nullopt on parse failure
[[nodiscard]] std::optional<ModuleSignature> parseModuleSignature(std::filesystem::path const& path);

/// Validates a compiled module against its signature.
///
/// Checks that:
/// - All `val` entries in the signature have corresponding exports in the module
/// - All `type` entries have corresponding type definitions
///
/// @param descriptor The compiled module descriptor
/// @param signature  The parsed signature to validate against
/// @return Vector of mismatch error messages (empty = valid)
[[nodiscard]] std::vector<std::string> validateSignature(ModuleDescriptor const& descriptor,
                                                         ModuleSignature const& signature);

} // namespace endo
