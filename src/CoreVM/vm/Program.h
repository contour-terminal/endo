// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h> // CoreNumber
#include <CoreVM/util/Cidr.h>
#include <CoreVM/util/IPAddress.h>
#include <CoreVM/util/unbox.h>
#include <CoreVM/vm/ConstantPool.h>
#include <CoreVM/vm/Instruction.h>

#include <memory>
#include <utility>
#include <vector>

namespace CoreVM
{

class NativeCallback;
class Runner;
class Runtime;
class Handler;
class Match;
class MatchDef;
class ConstantPool;

namespace diagnostics
{
    class Report;
}

class Program
{
  public:
    explicit Program(ConstantPool&& cp);
    Program(Program&) = delete;
    Program& operator=(Program&) = delete;
    ~Program() = default;

    const ConstantPool& constants() const noexcept { return _cp; }
    ConstantPool& constants() noexcept { return _cp; }

    // accessors to linked data
    const Match* match(size_t index) const { return _matches[index].get(); }
    Handler* handler(size_t index) const;
    NativeCallback* nativeHandler(size_t index) const { return _nativeHandlers[index]; }
    NativeCallback* nativeFunction(size_t index) const { return _nativeFunctions[index]; }

    // bulk accessors
    auto matches() { return util::unbox(_matches); }

    std::vector<std::string> handlerNames() const;
    int indexOf(const Handler* handler) const noexcept;
    Handler* findHandler(const std::string& name) const noexcept;

    bool link(Runtime* runtime, diagnostics::Report* report);

    void dump();

  private:
    void setup();

    // builders
    using Code = ConstantPool::Code;
    Handler* createHandler(const std::string& name);
    Handler* createHandler(const std::string& name, const Code& instructions);

  private:
    ConstantPool _cp;

    // linked data
    Runtime* _runtime;
    mutable std::vector<std::unique_ptr<Handler>> _handlers;
    std::vector<std::unique_ptr<Match>> _matches;
    std::vector<NativeCallback*> _nativeHandlers;
    std::vector<NativeCallback*> _nativeFunctions;
};

} // namespace CoreVM
