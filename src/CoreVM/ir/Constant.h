// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/ir/Value.h>

namespace CoreVM
{

class Constant: public Value
{
  public:
    Constant(LiteralType ty, std::string name): Value(ty, std::move(name)) {}

    [[nodiscard]] std::string to_string() const override;
};

} // namespace CoreVM
