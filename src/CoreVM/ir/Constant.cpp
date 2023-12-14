// SPDX-License-Identifier: Apache-2.0
module;
#include <fmt/format.h>

module CoreVM;
namespace CoreVM
{

std::string Constant::to_string() const
{
    return fmt::format("Constant '{}': {}", name(), type());
}

} // namespace CoreVM
