// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/SemanticAnalyzer.hpp>

#include <format>

namespace endo
{

SemanticAnalyzer::SemanticAnalyzer()
{
    _types.registerBuiltins();
}

std::optional<std::string> SemanticAnalyzer::validateMutAssignTarget(std::string const& name) const
{
    auto const dotPos = name.find('.');
    if (dotPos == std::string::npos)
        return std::nullopt;

    auto const prefix = name.substr(0, dotPos);
    auto const* binding = _scopes.lookupBinding(prefix);
    if (binding && binding->isRefCell)
        return std::format(
            "To mutate a ref cell, use '{} <- <value>' instead of '{} <- <value>'.", prefix, name);

    return std::nullopt;
}

} // namespace endo
