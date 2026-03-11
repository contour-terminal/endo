// SPDX-License-Identifier: Apache-2.0
#include <endo-language/module/ModuleLoader.hpp>

#include <endo-language/ast/AST.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/module/ModuleSignature.hpp>
#include <endo-language/parser/Parser.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>

namespace endo
{

namespace fs = std::filesystem;

ModuleLoader::ModuleLoader(CoreVM::Runtime& runtime, CoreVM::diagnostics::Report& report):
    _runtime(runtime), _report(report)
{
}

void ModuleLoader::addSearchPath(std::filesystem::path path)
{
    _searchPaths.emplace_back(std::move(path));
}

ModuleDescriptor const* ModuleLoader::loadModule(std::string const& dottedName,
                                                  std::optional<fs::path> const& relativeTo)
{
    // Check cache first
    if (auto it = _cache.find(dottedName); it != _cache.end())
        return it->second.get();

    // Circular dependency detection
    if (_loadingStack.contains(dottedName))
    {
        // Build the cycle chain for the error message
        std::string chain;
        for (auto const& name: _loadingStack)
        {
            if (!chain.empty())
                chain += " → ";
            chain += name;
        }
        chain += " → " + dottedName;
        _report.syntaxError(CoreVM::SourceLocation {}, "Circular module dependency: {}", chain);
        return nullptr;
    }

    // Resolve the file path
    auto const resolved = resolveModulePath(dottedName, relativeTo);
    if (!resolved)
    {
        // Build search locations for the error message
        std::string searched;
        if (relativeTo)
            searched += std::format("./{}.endo, ", dottedName);
        for (auto const& sp: _searchPaths)
            searched += std::format("{}/{}.endo, ", sp.string(), dottedName);
        if (searched.size() >= 2)
            searched.erase(searched.size() - 2); // remove trailing ", "
        _report.syntaxError(
            CoreVM::SourceLocation {}, "Module '{}' not found. Searched: {}", dottedName, searched);
        return nullptr;
    }

    // Check cache by resolved path (same file via different relative paths)
    auto const canonicalPath = fs::canonical(*resolved).string();
    for (auto const& [name, desc]: _cache)
    {
        if (desc->sourcePath == canonicalPath)
        {
            // Same file loaded under a different name — return existing descriptor
            return desc.get();
        }
    }

    // Compile the module
    _loadingStack.insert(dottedName);
    auto descriptor = compileModule(dottedName, *resolved);
    _loadingStack.erase(dottedName);

    if (!descriptor)
        return nullptr;

    auto* result = descriptor.get();
    _cache[dottedName] = std::move(descriptor);
    return result;
}

std::optional<fs::path> ModuleLoader::resolveModulePath(std::string const& dottedName,
                                                        std::optional<fs::path> relativeTo) const
{
    auto const segments = splitDottedName(dottedName);
    if (segments.empty())
        return std::nullopt;

    // Build the relative file path from segments: "Geometry.Circle" -> "Geometry/Circle.endo"
    auto relPath = fs::path {};
    for (size_t i = 0; i < segments.size() - 1; ++i)
        relPath /= segments[i];
    relPath /= segments.back() + ".endo";

    // 1. Relative to importing file
    if (relativeTo)
    {
        auto const dir = relativeTo->parent_path();
        auto const candidate = dir / relPath;
        if (fs::exists(candidate))
            return fs::canonical(candidate);
    }

    // 2. Search paths (includes project modules/, user modules, system stdlib)
    for (auto const& searchPath: _searchPaths)
    {
        auto const candidate = searchPath / relPath;
        if (fs::exists(candidate))
            return fs::canonical(candidate);
    }

    return std::nullopt;
}

void ModuleLoader::registerInlineModule(std::string const& name, std::unique_ptr<ModuleDescriptor> descriptor)
{
    _cache[name] = std::move(descriptor);
}

ModuleDescriptor const* ModuleLoader::findModule(std::string const& name) const
{
    if (auto it = _cache.find(name); it != _cache.end())
        return it->second.get();
    return nullptr;
}

std::vector<std::string> ModuleLoader::loadedModuleNames() const
{
    auto names = std::vector<std::string> {};
    names.reserve(_cache.size());
    for (auto const& [name, _]: _cache)
        names.push_back(name);
    std::ranges::sort(names);
    return names;
}

std::vector<std::string> ModuleLoader::availableModuleNames() const
{
    auto names = std::vector<std::string> {};
    for (auto const& searchPath: _searchPaths)
    {
        if (!fs::exists(searchPath) || !fs::is_directory(searchPath))
            continue;
        for (auto const& entry: fs::directory_iterator(searchPath))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".endo")
            {
                auto stem = entry.path().stem().string();
                if (isPascalCase(stem))
                    names.push_back(std::move(stem));
            }
        }
    }
    // Also include already-loaded modules
    for (auto const& [name, _]: _cache)
        names.push_back(name);
    std::ranges::sort(names);
    auto const [first, last] = std::ranges::unique(names);
    names.erase(first, last);
    return names;
}

std::unique_ptr<ModuleDescriptor> ModuleLoader::compileModule(std::string const& name,
                                                              fs::path const& path)
{
    // Read source file
    auto ifs = std::ifstream(path);
    if (!ifs)
    {
        _report.syntaxError(CoreVM::SourceLocation {}, "Cannot open module file: {}", path.string());
        return nullptr;
    }
    auto source = std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

    // Parse the module source
    auto lexerSource = std::make_unique<StringSource>(source);
    auto parser = Parser(_runtime, _report, std::move(lexerSource));
    parser.setSourceText(source);

    auto ast = parser.parse();
    if (!ast)
        return nullptr;

    // Create descriptor
    auto descriptor = std::make_unique<ModuleDescriptor>();
    descriptor->name = name;
    descriptor->sourcePath = fs::canonical(path);

    // Extract private names from the AST before IR generation
    if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get()))
    {
        for (auto const& stmt: compound->statements)
        {
            if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(stmt.get()))
            {
                if (letStmt->visibility == ast::Visibility::Private)
                    descriptor->privateNames.insert(letStmt->name);
            }
        }
    }

    // Generate IR to get full function metadata.
    // Use a temporary persistent state to capture the definitions.
    auto tempState = FSharpPersistentState {};
    auto ir = IRGenerator::generate(*ast, _report, _runtime, &tempState);
    if (!ir)
        return nullptr;

    // Transfer functions from the temporary state to the module descriptor
    for (auto& [funcName, func]: tempState.functions)
    {
        descriptor->functions[funcName] = std::move(func);
    }

    // Transfer value bindings
    descriptor->valueBindings = std::move(tempState.valueBindings);

    // Transfer type definitions from the IR program
    for (auto const& pt: ir->customProductTypes())
        descriptor->productTypes.push_back(pt);
    for (auto const& st: ir->customSumTypes())
        descriptor->sumTypes.push_back(st);

    // Retain the AST so function body pointers remain valid
    descriptor->retainedASTs.push_back(std::move(ast));

    // Also retain any ASTs from the temporary state
    for (auto& retained: tempState.retainedASTs)
        descriptor->retainedASTs.push_back(std::move(retained));

    // Validate against signature file (.endoi) if present
    auto sigPath = path;
    sigPath.replace_extension(".endoi");
    if (fs::exists(sigPath))
    {
        if (auto sig = parseModuleSignature(sigPath))
        {
            auto const errors = validateSignature(*descriptor, *sig);
            for (auto const& err: errors)
                _report.syntaxError(CoreVM::SourceLocation {}, "{}", err);
            if (!errors.empty())
                return nullptr;
        }
    }

    return descriptor;
}

std::vector<std::string> ModuleLoader::splitDottedName(std::string const& dottedName)
{
    auto segments = std::vector<std::string> {};
    auto ss = std::istringstream(dottedName);
    auto segment = std::string {};
    while (std::getline(ss, segment, '.'))
    {
        if (!segment.empty())
            segments.push_back(std::move(segment));
    }
    return segments;
}

bool ModuleLoader::isPascalCase(std::string_view name)
{
    return !name.empty() && std::isupper(static_cast<unsigned char>(name[0]));
}

} // namespace endo
