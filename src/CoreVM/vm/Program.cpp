// SPDX-License-Identifier: Apache-2.0
module;

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

module CoreVM;
namespace CoreVM
{

/* {{{ possible binary file format
 * ----------------------------------------------
 * u32                  magic number (0xbeafbabe)
 * u32                  version
 * u64                  flags
 * u64                  register count
 * u64                  code start
 * u64                  code size
 * u64                  integer const-table start
 * u64                  integer const-table element count
 * u64                  string const-table start
 * u64                  string const-table element count
 * u64                  regex const-table start (stored as string)
 * u64                  regex const-table element count
 * u64                  debug source-lines-table start
 * u64                  debug source-lines-table element count
 *
 * u32[]                code segment
 * u64[]                integer const-table segment
 * u64[]                string const-table segment
 * {u32, u8[]}[]        strings
 * {u32, u32, u32}[]    debug source lines segment
 */  // }}}

Program::Program(ConstantPool&& cp):
    _cp(std::move(cp)), _runtime(nullptr), _handlers(), _matches(), _nativeHandlers(), _nativeFunctions()
{
    setup();
}

Handler* Program::handler(size_t index) const
{
    return _handlers[index].get();
}

void Program::setup()
{
    for (const auto& handler: _cp.getHandlers())
        createHandler(handler.first, handler.second);

    const std::vector<MatchDef>& matches = _cp.getMatchDefs();
    for (size_t i = 0, e = matches.size(); i != e; ++i)
    {
        const MatchDef& def = matches[i];
        switch (def.op)
        {
            case MatchClass::Same: _matches.emplace_back(std::make_unique<MatchSame>(def, this)); break;
            case MatchClass::Head: _matches.emplace_back(std::make_unique<MatchHead>(def, this)); break;
            case MatchClass::Tail: _matches.emplace_back(std::make_unique<MatchTail>(def, this)); break;
            case MatchClass::RegExp: _matches.emplace_back(std::make_unique<MatchRegEx>(def, this)); break;
        }
    }
}

Handler* Program::createHandler(const std::string& name)
{
    return createHandler(name, {});
}

Handler* Program::createHandler(const std::string& name, const Code& code)
{
    _handlers.emplace_back(std::make_unique<Handler>(this, name, code));
    return _handlers.back().get();
}

Handler* Program::findHandler(const std::string& name) const noexcept
{
    for (auto& handler: _handlers)
        if (handler->name() == name)
            return handler.get();

    return nullptr;
}

std::vector<std::string> Program::handlerNames() const
{
    std::vector<std::string> result;
    result.reserve(_handlers.size());

    for (auto& handler: _handlers)
        result.emplace_back(handler->name());

    return result;
}

int Program::indexOf(const Handler* that) const noexcept
{
    for (int i = 0, e = _handlers.size(); i != e; ++i)
        if (_handlers[i].get() == that)
            return i;

    return -1;
}

void Program::dump()
{
    _cp.dump();
}

/**
 * Maps all native functions/handlers to their implementations (report
 *unresolved symbols)
 *
 * \param runtime the runtime to link this program against, resolving any
 *external native symbols.
 * \retval true Linking succeed.
 * \retval false Linking failed due to unresolved native signatures not found in
 *the runtime.
 */
bool Program::link(Runtime* runtime, diagnostics::Report* report)
{
    _runtime = runtime;
    int errors = 0;

    // load runtime modules
    for (const auto& module: _cp.getModules())
    {
        if (!runtime->import(module.first, module.second, nullptr))
        {
            errors++;
        }
    }

    // link nattive handlers
    _nativeHandlers.resize(_cp.getNativeHandlerSignatures().size());
    size_t i = 0;
    for (const auto& signature: _cp.getNativeHandlerSignatures())
    {
        // map to _nativeHandlers[i]
        if (NativeCallback* cb = runtime->find(signature))
        {
            _nativeHandlers[i] = cb;
        }
        else
        {
            _nativeHandlers[i] = nullptr;
            report->linkError("Unresolved symbol to native handler signature: {}", signature);
            // TODO _unresolvedSymbols.push_back(signature);
            errors++;
        }
        ++i;
    }

    // link nattive functions
    _nativeFunctions.resize(_cp.getNativeFunctionSignatures().size());
    i = 0;
    for (const auto& signature: _cp.getNativeFunctionSignatures())
    {
        if (NativeCallback* cb = runtime->find(signature))
        {
            _nativeFunctions[i] = cb;
        }
        else
        {
            _nativeFunctions[i] = nullptr;
            report->linkError("Unresolved native function signature: {}", signature);
            errors++;
        }
        ++i;
    }

    return errors == 0;
}

} // namespace CoreVM
