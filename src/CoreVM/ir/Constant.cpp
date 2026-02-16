// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <format>

namespace CoreVM
{

std::string Constant::to_string() const
{
    return "Constant '" + std::string(name()) + "': " + tos(type());
}

} // namespace CoreVM
