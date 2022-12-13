// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/ir/Constant.h>

#include <iostream>
#include <string>

namespace CoreVM::util
{
class RegExp;
class Cidr;
class IPAddress;
} // namespace CoreVM::util

namespace CoreVM
{

template <typename T, const LiteralType Ty>
class ConstantValue: public Constant
{
  public:
    ConstantValue(const T& value, const std::string& name = ""): Constant(Ty, name), _value(value) {}

    [[nodiscard]] T get() const { return _value; }

    [[nodiscard]] std::string to_string() const override
    {
        return fmt::format("Constant '{}': {} = {}", name(), type(), _value);
    }

  private:
    T _value;
};

using ConstantInt = ConstantValue<int64_t, LiteralType::Number>;
using ConstantBoolean = ConstantValue<bool, LiteralType::Boolean>;
using ConstantString = ConstantValue<std::string, LiteralType::String>;
using ConstantIP = ConstantValue<util::IPAddress, LiteralType::IPAddress>;
using ConstantCidr = ConstantValue<util::Cidr, LiteralType::Cidr>;
using ConstantRegExp = ConstantValue<util::RegExp, LiteralType::RegExp>;

} // namespace CoreVM
