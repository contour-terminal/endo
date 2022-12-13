// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/Constant.h>

namespace CoreVM
{

std::string Constant::to_string() const
{
    return fmt::format("Constant '{}': {}", name(), type());
}

} // namespace CoreVM
