// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/enums.hpp>
#include <CoreVM/util.hpp>

#include <memory>
#include <string>
#include <vector>

namespace CoreVM
{

class NativeCallback;
class NativeProperty;
class IRProgram;
class IRBuilder;
class Runner;
class Instr;
class Signature;

namespace diagnostics
{
    class Report;
}

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

    NativeCallback& registerFunction(const std::string& name);
    NativeCallback& registerFunction(const std::string& name, LiteralType returnType);

    NativeProperty& registerProperty(std::string const& name, LiteralType type);

    [[nodiscard]] NativeProperty* findProperty(std::string const& name) const noexcept;

    [[nodiscard]] auto const& properties() const noexcept { return _properties; }

    void invoke(int id, int argc, Value* argv, Runner* cx);

    bool verifyNativeCalls(IRProgram* program, IRBuilder* builder) const;

  private:
    std::vector<std::unique_ptr<NativeCallback>> _builtins;
    std::vector<std::unique_ptr<NativeProperty>> _properties;
};

} // namespace CoreVM
