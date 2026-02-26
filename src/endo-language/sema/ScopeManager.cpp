// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/ScopeManager.hpp>

#include <CoreVM/CoreVM.hpp>

namespace endo
{

ScopeManager::~ScopeManager()
{
    // Clean up any remaining non-root scopes
    while (_currentScope && _currentScope != _rootScope.get())
    {
        auto* parent = _currentScope->parent;
        delete _currentScope;
        _currentScope = parent;
    }
}

void ScopeManager::pushScope()
{
    auto newScope = std::make_unique<Scope>();
    newScope->parent = _currentScope;
    if (!_rootScope)
    {
        _rootScope = std::move(newScope);
        _currentScope = _rootScope.get();
    }
    else
    {
        _currentScope = newScope.release();
    }
}

std::vector<CoreVM::AllocaInstr*> ScopeManager::popScope()
{
    std::vector<CoreVM::AllocaInstr*> objectVars;
    if (_currentScope)
    {
        objectVars = std::move(_currentScope->objectVariables);
        auto* parent = _currentScope->parent;
        if (_currentScope != _rootScope.get())
            delete _currentScope;
        _currentScope = parent;
    }
    return objectVars;
}

void ScopeManager::bindVariable(std::string const& name,
                                CoreVM::Value* value,
                                bool isMutable,
                                bool isExported,
                                std::optional<SourceLocationRange> location)
{
    if (_currentScope)
        _currentScope->bindings[name] =
            BindingInfo { value, isMutable, isExported, /*isUsed=*/false, std::move(location) };
}

void ScopeManager::bindObjectVariable(std::string const& name,
                                      CoreVM::AllocaInstr* storage,
                                      bool isMutable,
                                      std::optional<SourceLocationRange> location)
{
    if (_currentScope)
    {
        _currentScope->objectVariables.push_back(storage);
        _currentScope->bindings[name] =
            BindingInfo { storage, isMutable, /*isExported=*/false, /*isUsed=*/false, std::move(location) };
    }
}

void ScopeManager::markUsed(std::string const& name)
{
    for (auto* scope = _currentScope; scope != nullptr; scope = scope->parent)
    {
        if (auto it = scope->bindings.find(name); it != scope->bindings.end())
        {
            it->second.isUsed = true;
            return;
        }
    }
}

std::vector<std::pair<std::string, SourceLocationRange>> ScopeManager::getUnusedBindings() const
{
    std::vector<std::pair<std::string, SourceLocationRange>> result;
    if (!_currentScope)
        return result;

    for (auto const& [name, info]: _currentScope->bindings)
    {
        if (!info.isUsed && name != "_" && !info.isExported && info.bindingLocation.has_value())
            result.emplace_back(name, *info.bindingLocation);
    }
    return result;
}

CoreVM::Value* ScopeManager::lookupVariable(std::string const& name) const
{
    for (auto const* scope = _currentScope; scope != nullptr; scope = scope->parent)
    {
        if (auto it = scope->bindings.find(name); it != scope->bindings.end())
            return it->second.value;
    }
    return nullptr;
}

BindingInfo const* ScopeManager::lookupBinding(std::string const& name) const
{
    for (auto const* scope = _currentScope; scope != nullptr; scope = scope->parent)
    {
        if (auto it = scope->bindings.find(name); it != scope->bindings.end())
            return &it->second;
    }
    return nullptr;
}

void ScopeManager::bindFunctionRef(std::string const& varName, std::string const& funcName)
{
    if (_currentScope)
        _currentScope->functionRefs[varName] = funcName;
}

std::optional<std::string> ScopeManager::lookupFunctionRef(std::string const& name) const
{
    for (auto const* scope = _currentScope; scope != nullptr; scope = scope->parent)
    {
        if (auto it = scope->functionRefs.find(name); it != scope->functionRefs.end())
            return it->second;
    }
    return std::nullopt;
}

void ScopeManager::clearObjectVariables()
{
    if (_currentScope)
        _currentScope->objectVariables.clear();
}

} // namespace endo
