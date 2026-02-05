// SPDX-License-Identifier: Apache-2.0
module;

#include <format>

module CoreVM;
namespace CoreVM
{

std::string Constant::to_string() const
{
    return std::format("Constant '{}': {}", name(), type());
}

} // namespace CoreVM
