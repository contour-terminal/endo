// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/module/ModuleDescriptor.hpp>

#include <CoreVM/CoreVM.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace endo
{

/// Resolves, compiles, and caches modules.
///
/// Implements import-once semantics: each module is loaded exactly once per session.
/// Supports file-based modules (.endo files) and inline module definitions.
/// Detects circular dependencies via a loading stack.
class ModuleLoader: public std::enable_shared_from_this<ModuleLoader>
{
  public:
    /// Constructs a module loader with the given runtime and diagnostics report.
    ModuleLoader(CoreVM::Runtime& runtime, CoreVM::diagnostics::Report& report);

    /// Adds a directory to the module search path.
    void addSearchPath(std::filesystem::path path);

    /// Loads a module by dotted name (e.g., "Math" or "Geometry.Circle").
    ///
    /// Returns the cached descriptor if already loaded. Otherwise resolves the file,
    /// compiles it, caches the result, and returns the descriptor.
    ///
    /// @param dottedName    The module name (e.g., "Math", "Geometry.Circle").
    /// @param relativeTo    Optional path of the importing file (for relative resolution).
    /// @return Pointer to the module descriptor, or nullptr on failure.
    ModuleDescriptor const* loadModule(std::string const& dottedName,
                                       std::optional<std::filesystem::path> const& relativeTo = std::nullopt);

    /// Resolves a dotted module name to a filesystem path.
    ///
    /// Search order:
    /// 1. Relative to importing file (if `relativeTo` is provided)
    /// 2. Each search path in order (caller-configured, typically:
    ///    project `modules/`, user `~/.config/endo/modules/`, system stdlib)
    ///
    /// @return Resolved absolute path, or std::nullopt if not found.
    [[nodiscard]] std::optional<std::filesystem::path> resolveModulePath(
        std::string const& dottedName, std::optional<std::filesystem::path> relativeTo) const;

    /// Registers an inline module (from `module Name = ... end`).
    void registerInlineModule(std::string const& name, std::unique_ptr<ModuleDescriptor> descriptor);

    /// Looks up a previously loaded or registered module by name.
    [[nodiscard]] ModuleDescriptor const* findModule(std::string const& name) const;

    /// Returns the list of all loaded module names (for completion).
    [[nodiscard]] std::vector<std::string> loadedModuleNames() const;

    /// Scans search paths for available `.endo` files (for import completion).
    [[nodiscard]] std::vector<std::string> availableModuleNames() const;

  private:
    /// Compiles a module from a source file.
    std::unique_ptr<ModuleDescriptor> compileModule(std::string const& name,
                                                    std::filesystem::path const& path);

    /// Splits a dotted module name into segments (e.g., "Geometry.Circle" -> ["Geometry", "Circle"]).
    [[nodiscard]] static std::vector<std::string> splitDottedName(std::string const& dottedName);

    /// Checks if a name starts with an uppercase letter (PascalCase convention).
    [[nodiscard]] static bool isPascalCase(std::string_view name);

    /// Ordered loading stack for cycle chain reporting.
    std::vector<std::string> _loadingStack;
    /// O(1) lookup companion for cycle detection.
    std::unordered_set<std::string> _loadingSet;

    /// Module cache keyed by canonical name.
    std::unordered_map<std::string, std::unique_ptr<ModuleDescriptor>> _cache;

    /// Search paths for module resolution.
    std::vector<std::filesystem::path> _searchPaths;

    CoreVM::Runtime& _runtime;            // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    CoreVM::diagnostics::Report& _report; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

} // namespace endo
