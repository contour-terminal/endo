// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/SemanticAnalyzer.hpp>

namespace endo
{

SemanticAnalyzer::SemanticAnalyzer()
{
    _types.registerBuiltins();
}

} // namespace endo
