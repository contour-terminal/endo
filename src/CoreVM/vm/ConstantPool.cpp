// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/util.hpp>

#include <cinttypes>
#include <format>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <sstream>
#include <vector>

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

    for (auto const i: std::views::iota(0uz, a.size()))
        if (a[i] != b[i])
            return false;

    return true;
}

template <typename T, typename U>
inline size_t ensureValue(std::vector<std::vector<T>>& vv, const U& array)
{
    for (auto const i: std::views::iota(0uz, vv.size()))
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

    for (auto const i: std::views::iota(0uz, array.size()))
        target[i] = array[i];

    return vv.size() - 1;
}

template <typename T, typename U>
inline size_t ensureValue(std::vector<T>& table, const U& literal)
{
    for (auto const i: std::views::iota(0uz, table.size()))
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

size_t ConstantPool::makeFloat(double value)
{
    return ensureValue(_floats, value);
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
    for (auto const i: std::views::iota(0uz, _stringArrays.size()))
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

size_t ConstantPool::makeNativeFunction(const IRBuiltinFunction* function)
{
    return makeNativeFunction(function->signature().to_s());
}

size_t ConstantPool::makeNativeFunction(const std::string& sig)
{
    return ensureValue(_nativeFunctionSignatures, sig);
}

size_t ConstantPool::makeFunction(const IRFunction* function)
{
    return makeFunction(function->name());
}

size_t ConstantPool::makeFunction(const std::string& name)
{
    size_t i = 0;
    size_t e = _functions.size();

    while (i != e)
    {
        if (_functions[i].first == name)
        {
            return i;
        }

        ++i;
    }

    _functions.resize(i + 1);
    _functions[i].first = name;
    return i;
}

template <typename T>
void dumpArrays(std::ostream& out, const std::vector<std::vector<T>>& vv, const char* name)
{
    if (vv.empty())
        return;

    out << "\n; Constant " << name << " Arrays\n";
    for (auto const i: std::views::iota(0uz, vv.size()))
    {
        const auto& array = vv[i];
        out << ".const array<" << name << "> " << std::setw(3) << i << " = [";
        for (auto const k: std::views::iota(0uz, array.size()))
        {
            if (k)
                out << ", ";
            if constexpr (std::is_arithmetic_v<T>)
                out << array[k];
            else
                out << array[k].str();
        }
        out << "];\n";
    }
}

void ConstantPool::dump() const
{
    std::cerr << dumpToString();
}

std::string ConstantPool::dumpToString() const
{
    std::ostringstream sstr;
    sstr << "; Program\n";

    if (!_modules.empty())
    {
        sstr << "\n; Modules\n";
        for (auto const i: std::views::iota(0uz, _modules.size()))
        {
            if (_modules[i].second.empty())
                sstr << std::format(".module '{}'\n", _modules[i].first);
            else
                sstr << std::format(".module '{}' from '{}'\n", _modules[i].first, _modules[i].second);
        }
    }

    if (!_nativeFunctionSignatures.empty())
    {
        sstr << "\n; External Functions\n";
        for (auto const i: std::views::iota(0uz, _nativeFunctionSignatures.size()))
        {
            sstr << std::format(".extern function {:3} = {:<20}\n", i, _nativeFunctionSignatures[i]);
        }
    }

    if (!_numbers.empty())
    {
        sstr << "\n; Integer Constants\n";
        for (auto const i: std::views::iota(0uz, _numbers.size()))
        {
            sstr << std::format(".const integer {:5} = {}\n", i, static_cast<CoreNumber>(_numbers[i]));
        }
    }

    if (!_floats.empty())
    {
        sstr << "\n; Float Constants\n";
        for (auto const i: std::views::iota(0uz, _floats.size()))
        {
            sstr << std::format(".const float {:7} = {:g}\n", i, _floats[i]);
        }
    }

    if (!_strings.empty())
    {
        sstr << "\n; String Constants\n";
        for (auto const i: std::views::iota(0uz, _strings.size()))
        {
            sstr << std::format(".const string {:6} = '{}'\n", i, _strings[i]);
        }
    }

    if (!_ipaddrs.empty())
    {
        sstr << "\n; IP Constants\n";
        for (auto const i: std::views::iota(0uz, _ipaddrs.size()))
        {
            sstr << std::format(".const ipaddr {:6} = {}\n", i, _ipaddrs[i].str());
        }
    }

    if (!_cidrs.empty())
    {
        sstr << "\n; CIDR Constants\n";
        for (auto const i: std::views::iota(0uz, _cidrs.size()))
        {
            sstr << std::format(".const cidr {:8} = {}\n", i, _cidrs[i].str());
        }
    }

    if (!_regularExpressions.empty())
    {
        sstr << "\n; Regular Expression Constants\n";
        for (auto const i: std::views::iota(0uz, _regularExpressions.size()))
        {
            sstr << std::format(".const regex {:7} = /{}/\n", i, _regularExpressions[i].c_str());
        }
    }

    if (!_stringArrays.empty())
    {
        sstr << "\n; Constant String Arrays\n";
        for (auto const i: std::views::iota(0uz, _stringArrays.size()))
        {
            const std::vector<std::string>& array = _stringArrays[i];
            sstr << ".const array<string> " << std::setw(3) << i << " = [";
            for (auto const k: std::views::iota(0uz, array.size()))
            {
                if (k)
                    sstr << ", ";
                sstr << '"' << array[k] << '"';
            }
            sstr << "];\n";
        }
    }

    dumpArrays(sstr, _intArrays, "Integer");
    dumpArrays(sstr, _ipaddrArrays, "IPAddress");
    dumpArrays(sstr, _cidrArrays, "Cidr");

    if (!_matchDefs.empty())
    {
        sstr << "\n; Match Table\n";
        for (auto const i: std::views::iota(0uz, _matchDefs.size()))
        {
            const MatchDef& def = _matchDefs[i];
            sstr << std::format(".const match {:7} = function {}, op {}, elsePC {} ; {}\n",
                                i,
                                def.functionId,
                                tos(def.op),
                                def.elsePC,
                                _functions[def.functionId].first);

            for (auto const k: std::views::iota(0uz, def.cases.size()))
            {
                const MatchCaseDef& one = def.cases[k];

                sstr << std::format(
                    "                       case {:3} = label {:2}, pc {:4} ; ", k, one.label, one.pc);

                if (def.op == MatchClass::RegExp)
                {
                    sstr << std::format("/{}/\n", _regularExpressions[one.label].c_str());
                }
                else
                {
                    sstr << std::format("'{}'\n", _strings[one.label]);
                }
            }
        }
    }

    for (const auto& function: getFunctions())
    {
        const auto& name = function.first;
        const auto& code = function.second;

        sstr << std::format("\n.function {:<27} ; ({} stack size, {} instructions)\n",
                            name,
                            computeStackSize(code.data(), code.size()),
                            code.size());
        sstr << disassemble(code.data(), code.size(), "  ", this);
    }

    sstr << "\n\n";
    return sstr.str();
}

} // namespace CoreVM
