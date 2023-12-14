// SPDX-License-Identifier: Apache-2.0
module;

#include <cinttypes>
#include <iomanip>
#include <iostream>
#include <vector>
#include <fmt/core.h>

#include <CoreVM/util/IPAddress.h>

module CoreVM;
namespace CoreVM
{

// {{{ helpers
template <typename T, typename S>
std::vector<T>&& convert(const std::vector<S>& source)
{
    std::vector<T> target(source.size());

    for (S value: source)
        target[value->id()] = value->get();

    return std::move(target);
}

template <typename T, typename U>
inline bool equals(const std::vector<T>& a, const std::vector<U>& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0, e = a.size(); i != e; ++i)
        if (a[i] != b[i])
            return false;

    return true;
}

template <typename T, typename U>
inline size_t ensureValue(std::vector<std::vector<T>>& vv, const U& array)
{
    for (size_t i = 0, e = vv.size(); i != e; ++i)
    {
        const auto& test = vv[i];

        if (test.size() != array.size())
            continue;

        if (!equals(test, array))
            continue;

        return i;
    }

    // we hand-add each element seperately because it might be,
    // that the source element type is not the same as the target element type
    // (such as std::string -> Buffer)

    vv.push_back(std::vector<T>(array.size()));
    auto& target = vv.back();

    for (size_t i = 0, e = array.size(); i != e; ++i)
        target[i] = array[i];

    return vv.size() - 1;
}

template <typename T, typename U>
inline size_t ensureValue(std::vector<T>& table, const U& literal)
{
    for (size_t i = 0, e = table.size(); i != e; ++i)
        if (table[i] == literal)
            return i;

    table.push_back(literal);
    return table.size() - 1;
}
// }}}

size_t ConstantPool::makeInteger(CoreNumber value)
{
    return ensureValue(_numbers, value);
}

size_t ConstantPool::makeString(const std::string& value)
{
    return ensureValue(_strings, value);
}

size_t ConstantPool::makeIPAddress(const util::IPAddress& value)
{
    return ensureValue(_ipaddrs, value);
}

size_t ConstantPool::makeCidr(const util::Cidr& value)
{
    return ensureValue(_cidrs, value);
}

size_t ConstantPool::makeRegExp(const util::RegExp& value)
{
    return ensureValue(_regularExpressions, value);
}

size_t ConstantPool::makeIntegerArray(const std::vector<CoreNumber>& elements)
{
    return ensureValue(_intArrays, elements);
}

size_t ConstantPool::makeStringArray(const std::vector<std::string>& elements)
{
    for (size_t i = 0, e = _stringArrays.size(); i != e; ++i)
    {
        const auto& array = _stringArrays[i];

        if (array.size() != elements.size())
            continue;

        if (!equals(array, elements))
            continue;

        return i;
    }

    _stringArrays.push_back(elements);
    return _stringArrays.size() - 1;
}

size_t ConstantPool::makeIPaddrArray(const std::vector<util::IPAddress>& elements)
{
    return ensureValue(_ipaddrArrays, elements);
}

size_t ConstantPool::makeCidrArray(const std::vector<util::Cidr>& elements)
{
    return ensureValue(_cidrArrays, elements);
}

size_t ConstantPool::makeMatchDef()
{
    _matchDefs.emplace_back();
    return _matchDefs.size() - 1;
}

size_t ConstantPool::makeNativeHandler(const IRBuiltinHandler* handler)
{
    return makeNativeHandler(handler->signature().to_s());
}

size_t ConstantPool::makeNativeHandler(const std::string& sig)
{
    return ensureValue(_nativeHandlerSignatures, sig);
}

size_t ConstantPool::makeNativeFunction(const IRBuiltinFunction* function)
{
    return makeNativeFunction(function->signature().to_s());
}

size_t ConstantPool::makeNativeFunction(const std::string& sig)
{
    return ensureValue(_nativeFunctionSignatures, sig);
}

size_t ConstantPool::makeHandler(const IRHandler* handler)
{
    return makeHandler(handler->name());
}

size_t ConstantPool::makeHandler(const std::string& name)
{
    size_t i = 0;
    size_t e = _handlers.size();

    while (i != e)
    {
        if (_handlers[i].first == name)
        {
            return i;
        }

        ++i;
    }

    _handlers.resize(i + 1);
    _handlers[i].first = name;
    return i;
}

template <typename T>
void dumpArrays(const std::vector<std::vector<T>>& vv, const char* name)
{
    if (vv.empty())
        return;

    std::cout << "\n; Constant " << name << " Arrays\n";
    for (size_t i = 0, e = vv.size(); i != e; ++i)
    {
        const auto& array = vv[i];
        std::cout << ".const array<" << name << "> " << std::setw(3) << i << " = [";
        for (size_t k = 0, m = array.size(); k != m; ++k)
        {
            if (k)
                std::cout << ", ";
            std::cout << fmt::format("{}", array[k]);
        }
        std::cout << "];\n";
    }
}

void ConstantPool::dump() const
{
    printf("; Program\n");

    if (!_modules.empty())
    {
        printf("\n; Modules\n");
        for (size_t i = 0, e = _modules.size(); i != e; ++i)
        {
            if (_modules[i].second.empty())
                printf(".module '%s'\n", _modules[i].first.c_str());
            else
                printf(".module '%s' from '%s'\n", _modules[i].first.c_str(), _modules[i].second.c_str());
        }
    }

    if (!_nativeFunctionSignatures.empty())
    {
        printf("\n; External Functions\n");
        for (size_t i = 0, e = _nativeFunctionSignatures.size(); i != e; ++i)
        {
            printf(".extern function %3zu = %-20s\n", i, _nativeFunctionSignatures[i].c_str());
        }
    }

    if (!_nativeHandlerSignatures.empty())
    {
        printf("\n; External Handlers\n");
        for (size_t i = 0, e = _nativeHandlerSignatures.size(); i != e; ++i)
        {
            printf(".extern handler %4zu = %-20s\n", i, _nativeHandlerSignatures[i].c_str());
        }
    }

    if (!_numbers.empty())
    {
        printf("\n; Integer Constants\n");
        for (size_t i = 0, e = _numbers.size(); i != e; ++i)
        {
            printf(".const integer %5zu = %" PRIi64 "\n", i, (CoreNumber) _numbers[i]);
        }
    }

    if (!_strings.empty())
    {
        printf("\n; String Constants\n");
        for (size_t i = 0, e = _strings.size(); i != e; ++i)
        {
            printf(".const string %6zu = '%s'\n", i, _strings[i].c_str());
        }
    }

    if (!_ipaddrs.empty())
    {
        printf("\n; IP Constants\n");
        for (size_t i = 0, e = _ipaddrs.size(); i != e; ++i)
        {
            printf(".const ipaddr %6zu = %s\n", i, _ipaddrs[i].str().c_str());
        }
    }

    if (!_cidrs.empty())
    {
        printf("\n; CIDR Constants\n");
        for (size_t i = 0, e = _cidrs.size(); i != e; ++i)
        {
            printf(".const cidr %8zu = %s\n", i, _cidrs[i].str().c_str());
        }
    }

    if (!_regularExpressions.empty())
    {
        printf("\n; Regular Expression Constants\n");
        for (size_t i = 0, e = _regularExpressions.size(); i != e; ++i)
        {
            printf(".const regex %7zu = /%s/\n", i, _regularExpressions[i].c_str());
        }
    }

    if (!_stringArrays.empty())
    {
        std::cout << "\n; Constant String Arrays\n";
        for (size_t i = 0, e = _stringArrays.size(); i != e; ++i)
        {
            const std::vector<std::string>& array = _stringArrays[i];
            std::cout << ".const array<string> " << std::setw(3) << i << " = [";
            for (size_t k = 0, m = array.size(); k != m; ++k)
            {
                if (k)
                    std::cout << ", ";
                std::cout << '"' << array[k] << '"';
            }
            std::cout << "];\n";
        }
    }

    dumpArrays(_intArrays, "Integer");
    dumpArrays(_ipaddrArrays, "IPAddress");
    dumpArrays(_cidrArrays, "Cidr");

    if (!_matchDefs.empty())
    { // {{{
        printf("\n; Match Table\n");
        for (size_t i = 0, e = _matchDefs.size(); i != e; ++i)
        {
            const MatchDef& def = _matchDefs[i];
            printf(".const match %7zu = handler %zu, op %s, elsePC %" PRIu64 " ; %s\n",
                   i,
                   def.handlerId,
                   tos(def.op).c_str(),
                   def.elsePC,
                   _handlers[def.handlerId].first.c_str());

            for (size_t k = 0, m = def.cases.size(); k != m; ++k)
            {
                const MatchCaseDef& one = def.cases[k];

                printf("                       case %3zu = label %2" PRIu64 ", pc %4" PRIu64 " ; ",
                       k,
                       one.label,
                       one.pc);

                if (def.op == MatchClass::RegExp)
                {
                    printf("/%s/\n", _regularExpressions[one.label].c_str());
                }
                else
                {
                    printf("'%s'\n", _strings[one.label].c_str());
                }
            }
        }
    } // }}}

    for (const auto& handler: getHandlers())
    {
        const auto& name = handler.first;
        const auto& code = handler.second;

        printf("\n.handler %-27s ; (%zu stack size, %zu instructions)\n",
               name.c_str(),
               computeStackSize(code.data(), code.size()),
               code.size());
        printf("%s", disassemble(code.data(), code.size(), "  ", this).c_str());
    }

    printf("\n\n");
}

} // namespace CoreVM
