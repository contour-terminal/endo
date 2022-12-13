// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/ir/Constant.h>

#include <string>
#include <vector>

namespace CoreVM
{

class ConstantArray: public Constant
{
  public:
    ConstantArray(LiteralType elementType, std::vector<Constant*> elements, std::string name = ""):
        Constant(makeArrayType(elementType), std::move(name)), _elements(std::move(elements))
    {
    }

    ConstantArray(const std::vector<Constant*>& elements, const std::string& name = ""):
        Constant(makeArrayType(elements.front()->type()), name), _elements(elements)
    {
    }

    ConstantArray(ConstantArray&&) = delete;
    ConstantArray& operator=(ConstantArray&&) = delete;

    ConstantArray(const ConstantArray&) = delete;
    ConstantArray& operator=(const ConstantArray&) = delete;

    [[nodiscard]] const std::vector<Constant*>& get() const { return _elements; }

    [[nodiscard]] LiteralType elementType() const { return _elements[0]->type(); }

  private:
    std::vector<Constant*> _elements;

    LiteralType makeArrayType(LiteralType elementType);
};

} // namespace CoreVM
