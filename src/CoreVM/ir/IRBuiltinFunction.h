// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/NativeCallback.h>
#include <CoreVM/Signature.h>
#include <CoreVM/ir/Constant.h>

namespace CoreVM
{

class IRBuiltinFunction: public Constant
{
  public:
    explicit IRBuiltinFunction(const NativeCallback& cb):
        Constant(cb.signature().returnType(), cb.signature().name()), _native(cb)
    {
    }

    [[nodiscard]] const Signature& signature() const { return _native.signature(); }
    [[nodiscard]] const NativeCallback& getNative() const { return _native; }

  private:
    const NativeCallback& _native;
};

} // namespace CoreVM
