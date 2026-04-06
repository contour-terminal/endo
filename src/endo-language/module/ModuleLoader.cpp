// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/AST.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/module/ModuleLoader.hpp>
#include <endo-language/module/ModuleSignature.hpp>
#include <endo-language/parser/Parser.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>

namespace endo
{

namespace fs = std::filesystem;

ModuleLoader::ModuleLoader(CoreVM::Runtime& runtime, CoreVM::diagnostics::Report& report, FileSystem& fs):
    _fs(fs), _runtime(runtime), _report(report)
{
}

void ModuleLoader::addSearchPath(std::filesystem::path path)
{
    _searchPaths.emplace_back(std::move(path));
    _availableModulesCache.reset(); // Invalidate cache
}

ModuleDescriptor const* ModuleLoader::loadModule(std::string const& dottedName,
                                                 std::optional<fs::path> const& relativeTo)
{
    // Check cache first
    if (auto it = _cache.find(dottedName); it != _cache.end())
        return it->second.get();

    // Circular dependency detection
    if (_loadingSet.contains(dottedName))
    {
        // Build the cycle chain from the ordered loading stack
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
        // Build the expected relative path for the error message (e.g., "Geometry/Circle.endo")
        auto const segments = splitDottedName(dottedName);
        auto relPath = fs::path {};
        for (size_t i = 0; i < segments.size() - 1; ++i)
            relPath /= segments[i];
        relPath /= segments.back() + ".endo";

        std::string searched;
        if (relativeTo)
            searched += std::format("{}, ", (relativeTo->parent_path() / relPath).string());
        for (auto const& sp: _searchPaths)
            searched += std::format("{}, ", (sp / relPath).string());
        if (searched.size() >= 2)
            searched.erase(searched.size() - 2); // remove trailing ", "
        _report.syntaxError(
            CoreVM::SourceLocation {}, "Module '{}' not found. Searched: {}", dottedName, searched);
        return nullptr;
    }

    // Check cache by resolved path (same file via different relative paths)
    auto const canonicalPath = _fs.weaklyCanonical(*resolved).string();
    if (auto pathIt = _cacheByPath.find(canonicalPath); pathIt != _cacheByPath.end())
        return pathIt->second;

    // Compile the module
    _loadingStack.push_back(dottedName);
    _loadingSet.insert(dottedName);
    auto descriptor = compileModule(dottedName, *resolved);
    _loadingStack.pop_back();
    _loadingSet.erase(dottedName);

    if (!descriptor)
        return nullptr;

    auto* result = descriptor.get();
    if (!result->sourcePath.empty())
        _cacheByPath[result->sourcePath.string()] = result;
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
        if (_fs.exists(candidate))
            return _fs.weaklyCanonical(candidate);
    }

    // 2. Search paths (includes project modules/, user modules, system stdlib)
    for (auto const& searchPath: _searchPaths)
    {
        auto const candidate = searchPath / relPath;
        if (_fs.exists(candidate))
            return _fs.weaklyCanonical(candidate);
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
    if (_availableModulesCache)
    {
        // Merge cached filesystem results with currently loaded modules
        auto names = *_availableModulesCache;
        for (auto const& [name, _]: _cache)
            names.push_back(name);
        std::ranges::sort(names);
        auto const [first, last] = std::ranges::unique(names);
        names.erase(first, last);
        return names;
    }

    auto fsNames = std::vector<std::string> {};
    for (auto const& searchPath: _searchPaths)
    {
        if (!_fs.exists(searchPath) || !_fs.isDirectory(searchPath))
            continue;
        auto entries = _fs.listDirectoryRecursive(searchPath);
        if (!entries)
            continue;
        for (auto const& entry: *entries)
        {
            if (entry.isDirectory || entry.path.extension() != ".endo")
                continue;

            // Build dotted name from relative path (e.g., "Geometry/Circle.endo" -> "Geometry.Circle")
            auto const relPath = entry.path.lexically_relative(searchPath);
            std::string dottedName;
            bool allPascalCase = true;

            for (auto const& component: relPath.parent_path())
            {
                auto const seg = component.string();
                if (!isPascalCase(seg))
                {
                    allPascalCase = false;
                    break;
                }
                if (!dottedName.empty())
                    dottedName += '.';
                dottedName += seg;
            }

            if (!allPascalCase)
                continue;

            auto const stem = relPath.stem().string();
            if (!isPascalCase(stem))
                continue;

            if (!dottedName.empty())
                dottedName += '.';
            dottedName += stem;
            fsNames.push_back(std::move(dottedName));
        }
    }
    _availableModulesCache = fsNames;

    // Also include already-loaded modules
    for (auto const& [name, _]: _cache)
        fsNames.push_back(name);
    std::ranges::sort(fsNames);
    auto const [first, last] = std::ranges::unique(fsNames);
    fsNames.erase(first, last);
    return fsNames;
}

std::unique_ptr<ModuleDescriptor> ModuleLoader::compileModule(std::string const& name, fs::path const& path)
{
    // Read source file
    auto content = _fs.readFile(path);
    if (!content)
    {
        _report.syntaxError(CoreVM::SourceLocation {}, "Cannot open module file: {}", path.string());
        return nullptr;
    }
    auto source = std::move(*content);

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
    descriptor->sourcePath = _fs.weaklyCanonical(path);

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
    tempState.sourceFilePath = path; // Enable relative module resolution from this file
    // Enable nested imports when this loader is managed by a shared_ptr.
    // Stack-allocated loaders (e.g., in tests) skip this — weak_from_this() is empty.
    if (auto self = weak_from_this().lock())
        tempState.moduleLoader = std::move(self);
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
    if (_fs.exists(sigPath))
    {
        auto sigContent = _fs.readFile(sigPath);
        if (auto sig = sigContent ? parseModuleSignature(sigPath.stem().string(), *sigContent) : std::nullopt)
        {
            for (auto const& warning: sig->warnings)
                _report.syntaxError(CoreVM::SourceLocation {}, "Signature warning: {}", warning);
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
    auto start = std::string::size_type { 0 };
    while (start < dottedName.size())
    {
        auto const dotPos = dottedName.find('.', start);
        auto const end = (dotPos != std::string::npos) ? dotPos : dottedName.size();
        if (end > start)
            segments.emplace_back(dottedName, start, end - start);
        start = end + 1;
    }
    return segments;
}

bool ModuleLoader::isPascalCase(std::string_view name)
{
    return !name.empty() && std::isupper(static_cast<unsigned char>(name[0]));
}

} // namespace endo
