// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

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
    _cp(std::move(cp)), _runtime(nullptr), _functions(), _matches(), _nativeFunctions()
{
    setup();
}

Function* Program::function(size_t index) const
{
    return _functions[index].get();
}

void Program::setup()
{
    auto const& functions = _cp.getFunctions();
    for (size_t i = 0; i < functions.size(); ++i)
    {
        Function* fn = createFunction(functions[i].first, functions[i].second);
        auto const& locationTable = _cp.getFunctionLocationTable(i);
        if (!locationTable.empty())
            fn->setLocationTable(locationTable);
    }

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

Function* Program::createFunction(const std::string& name)
{
    return createFunction(name, {});
}

Function* Program::createFunction(const std::string& name, const Code& code)
{
    _functions.emplace_back(std::make_unique<Function>(this, name, code));
    return _functions.back().get();
}

Function* Program::findFunction(const std::string& name) const noexcept
{
    for (auto& fn: _functions)
        if (fn->name() == name)
            return fn.get();

    return nullptr;
}

std::vector<std::string> Program::functionNames() const
{
    std::vector<std::string> result;
    result.reserve(_functions.size());

    for (auto& fn: _functions)
        result.emplace_back(fn->name());

    return result;
}

int Program::indexOf(const Function* that) const noexcept
{
    for (int i = 0, e = _functions.size(); i != e; ++i)
        if (_functions[i].get() == that)
            return i;

    return -1;
}

void Program::dump()
{
    _cp.dump();
}

std::string Program::dumpToString() const
{
    return _cp.dumpToString();
}

/**
 * Maps all native functions to their implementations (report
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

    // link native functions
    _nativeFunctions.resize(_cp.getNativeFunctionSignatures().size());
    size_t i = 0;
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
