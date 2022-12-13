// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/NativeCallback.h>
#include <CoreVM/Signature.h>
#include <CoreVM/ir/Constant.h>

#include <list>
#include <string>
#include <vector>

namespace CoreVM
{

class IRBuiltinHandler: public Constant
{
  public:
    explicit IRBuiltinHandler(const NativeCallback& cb):
        Constant(LiteralType::Boolean, cb.signature().name()), _native(cb)
    {
    }

    const Signature& signature() const { return _native.signature(); }
    const NativeCallback& getNative() const { return _native; }

  private:
    const NativeCallback& _native;
};

} // namespace CoreVM
