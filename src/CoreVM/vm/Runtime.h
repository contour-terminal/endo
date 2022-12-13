// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>
#include <CoreVM/NativeCallback.h>
#include <CoreVM/Signature.h>
#include <CoreVM/util/unbox.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CoreVM
{

class IRProgram;
class IRBuilder;
class NativeCallback;
class Runner;

class Runtime
{
  public:
    using Value = uint64_t;

    virtual ~Runtime() = default;

    virtual bool import(const std::string& name,
                        const std::string& path,
                        std::vector<NativeCallback*>* builtins);

    [[nodiscard]] NativeCallback* find(const std::string& signature) const noexcept;
    [[nodiscard]] NativeCallback* find(const Signature& signature) const noexcept;
    [[nodiscard]] auto builtins() { return util::unbox(_builtins); }

    NativeCallback& registerHandler(const std::string& name);
    NativeCallback& registerFunction(const std::string& name);
    NativeCallback& registerFunction(const std::string& name, LiteralType returnType);

    void invoke(int id, int argc, Value* argv, Runner* cx);

    /**
     * Verifies all call instructions.
     *
     * @param program the IRProgram to verify.
     * @param builder the IRBuilder to pass to each verify(), that can be used for modifications.
     *
     * @see bool NativeCallback::verify(Instr* call, IRBuilder* irBuilder);
     */
    bool verifyNativeCalls(IRProgram* program, IRBuilder* builder) const;

  private:
    std::vector<std::unique_ptr<NativeCallback>> _builtins;
};

} // namespace CoreVM
