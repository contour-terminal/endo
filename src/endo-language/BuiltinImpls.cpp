// SPDX-License-Identifier: Apache-2.0
#include "BuiltinImpls.hpp"

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <algorithm>
#include <bit>
#include <format>
#include <locale>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace endo::builtins
{

// ---------------------------------------------------------------------------
// Value-to-string conversion
// ---------------------------------------------------------------------------

std::string slotValueToString(uint64_t rawVal,
                              CoreVM::LiteralType type,
                              CoreVM::Runner* runner,
                              bool quoteStrings)
{
    switch (type)
    {
        case CoreVM::LiteralType::String: {
            auto const* str = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(rawVal));
            if (!str)
                return "(null)";
            if (quoteStrings)
                return "\"" + std::string(*str) + "\"";
            return std::string(*str);
        }
        case CoreVM::LiteralType::Boolean: return rawVal ? "true" : "false";
        case CoreVM::LiteralType::Float: {
            auto const f = std::bit_cast<double>(rawVal);
            auto s = std::to_string(f);
            // Remove trailing zeros after decimal point, but keep at least one digit
            if (s.find('.') != std::string::npos)
            {
                auto last = s.find_last_not_of('0');
                if (s[last] == '.')
                    ++last;
                s.erase(last + 1);
            }
            return s;
        }
        default: break;
    }
    // Void/Object/Number — delegate to valueToString for recursive container handling
    return valueToString(rawVal, runner);
}

std::string valueToString(uint64_t rawVal, CoreVM::Runner* runner)
{
    // Check if the value is a string pointer
    if (runner && runner->isKnownString(rawVal))
    {
        auto const* str = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(rawVal));
        return *str;
    }

    // Check if the value is a known TypedObject pointer
    if (runner && runner->isKnownObject(rawVal))
    {
        auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawVal));
        auto const typeId = obj->type->id;
        if (typeId == CoreVM::BuiltinTypeId::List)
        {
            // Read element type from type tag slot 2
            auto const elemType = static_cast<CoreVM::LiteralType>(obj->getSlot(2));
            std::string result = "[";
            bool first = true;
            while (obj && obj->type->id == CoreVM::BuiltinTypeId::List && obj->tag == 1)
            {
                if (!first)
                    result += "; ";
                first = false;
                result += slotValueToString(obj->getSlot(0), elemType, runner);
                obj = reinterpret_cast<CoreVM::TypedObject*>(obj->getSlot(1));
            }
            result += "]";
            return result;
        }
        if (typeId == CoreVM::BuiltinTypeId::Tuple2)
        {
            // Read packed type tags from slot 2
            auto const packed = obj->getSlot(2);
            auto const t0 = CoreVM::unpackTypeTag(packed, 0);
            auto const t1 = CoreVM::unpackTypeTag(packed, 1);
            return "(" + slotValueToString(obj->getSlot(0), t0, runner) + ", "
                   + slotValueToString(obj->getSlot(1), t1, runner) + ")";
        }
        if (typeId == CoreVM::BuiltinTypeId::Tuple3)
        {
            // Read packed type tags from slot 3
            auto const packed = obj->getSlot(3);
            auto const t0 = CoreVM::unpackTypeTag(packed, 0);
            auto const t1 = CoreVM::unpackTypeTag(packed, 1);
            auto const t2 = CoreVM::unpackTypeTag(packed, 2);
            return "(" + slotValueToString(obj->getSlot(0), t0, runner) + ", "
                   + slotValueToString(obj->getSlot(1), t1, runner) + ", "
                   + slotValueToString(obj->getSlot(2), t2, runner) + ")";
        }
        if (typeId == CoreVM::BuiltinTypeId::Option)
        {
            if (obj->tag == 0)
                return "None";
            auto const innerType = static_cast<CoreVM::LiteralType>(obj->getSlot(1));
            return "Some " + slotValueToString(obj->getSlot(0), innerType, runner);
        }
        if (typeId == CoreVM::BuiltinTypeId::Result)
        {
            auto const innerType = static_cast<CoreVM::LiteralType>(obj->getSlot(1));
            if (obj->tag == 0)
                return "Error " + slotValueToString(obj->getSlot(0), innerType, runner);
            return "Ok " + slotValueToString(obj->getSlot(0), innerType, runner);
        }
        if (obj->type->kind == CoreVM::TypeKind::Product)
        {
            // Check if this is a ProcessInfo record (has "cpu" float field)
            bool const isProcessInfo = obj->type->id == CoreVM::BuiltinTypeId::ProcessInfo;

            std::string result = "{ ";
            for (size_t i = 0; i < obj->type->fields.size(); ++i)
            {
                if (i > 0)
                    result += "; ";
                result += obj->type->fields[i].name;
                result += " = ";
                auto slotVal = obj->getSlot(static_cast<uint8_t>(i));

                // ProcessInfo "cpu" field stores a double as bit_cast<uint64_t>
                if (isProcessInfo && obj->type->fields[i].name == "cpu")
                {
                    auto const cpuVal = std::bit_cast<double>(slotVal);
                    result += std::format("{:.1f}", cpuVal);
                }
                else
                {
                    switch (obj->type->fields[i].type)
                    {
                        case CoreVM::LiteralType::String: {
                            auto const* str =
                                reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(slotVal));
                            result += str ? *str : "(null)";
                            break;
                        }
                        case CoreVM::LiteralType::Boolean: result += slotVal ? "true" : "false"; break;
                        default: result += std::to_string(static_cast<int64_t>(slotVal)); break;
                    }
                }
            }
            result += " }";
            return result;
        }
        if (obj->type->kind == CoreVM::TypeKind::Sum)
        {
            auto const* variantInfo = obj->type->getVariant(obj->tag);
            std::string result = variantInfo ? variantInfo->name : "?";
            if (variantInfo && variantInfo->payloadSlots > 0)
            {
                auto const hasNamedFields = !variantInfo->fields.empty();
                result += hasNamedFields ? "(" : " ";
                for (uint8_t i = 0; i < variantInfo->payloadSlots; ++i)
                {
                    if (i > 0)
                        result += ", ";
                    if (hasNamedFields && i < variantInfo->fields.size())
                        result += variantInfo->fields[i].name + ": ";
                    result += slotValueToString(obj->getSlot(i), CoreVM::LiteralType::Number, runner, false);
                }
                if (hasNamedFields)
                    result += ")";
            }
            return result;
        }
        // Unknown object type — fallback
        return std::to_string(static_cast<int64_t>(rawVal));
    }
    return std::to_string(static_cast<int64_t>(rawVal));
}

// ---------------------------------------------------------------------------
// List operations
// ---------------------------------------------------------------------------

void listConcat(CoreVM::Params& args)
{
    auto* left = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto* right = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(2)));

    if (!left || left->tag == 0)
    {
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(right)));
        return;
    }

    std::vector<uint64_t> elements;
    auto* cur = left;
    while (cur && cur->tag == 1)
    {
        elements.push_back(cur->getSlot(0));
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }

    // Propagate element type from source list's type tag slot
    auto elemType = static_cast<CoreVM::LiteralType>(left->getSlot(2));
    CoreVM::TypedObject* acc = right;
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        acc = args.caller()->makeConsCell(*it, acc, elemType);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
}

void listHead(CoreVM::Params& args)
{
    auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    if (!list || list->tag == 0)
    {
        auto* none = args.caller()->makeNoneOption();
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
    }
    else
    {
        auto elemType = static_cast<CoreVM::LiteralType>(list->getSlot(2));
        auto* some = args.caller()->makeSomeOption(list->getSlot(0), elemType);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
    }
}

void listTail(CoreVM::Params& args)
{
    auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    if (!list || list->tag == 0)
    {
        auto* nil = args.caller()->makeNilList();
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(nil)));
    }
    else
    {
        args.setResult(static_cast<CoreVM::CoreNumber>(list->getSlot(1)));
    }
}

void listLength(CoreVM::Params& args)
{
    auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    int64_t count = 0;
    while (cur && cur->tag == 1)
    {
        ++count;
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }
    args.setResult(static_cast<CoreVM::CoreNumber>(count));
}

void listIsEmpty(CoreVM::Params& args)
{
    auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    args.setResult(!list || list->tag == 0);
}

void listSort(CoreVM::Params& args)
{
    auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    std::vector<int64_t> elements;
    while (cur && cur->tag == 1)
    {
        elements.push_back(static_cast<int64_t>(cur->getSlot(0)));
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }
    std::ranges::sort(elements);
    auto* acc = args.caller()->makeNilList(CoreVM::LiteralType::Number);
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        acc = args.caller()->makeConsCell(static_cast<uint64_t>(*it), acc, CoreVM::LiteralType::Number);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
}

void listDistinct(CoreVM::Params& args)
{
    auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    std::vector<int64_t> elements;
    std::unordered_set<int64_t> seen;
    while (cur && cur->tag == 1)
    {
        auto val = static_cast<int64_t>(cur->getSlot(0));
        if (seen.insert(val).second)
            elements.push_back(val);
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }
    auto* acc = args.caller()->makeNilList(CoreVM::LiteralType::Number);
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        acc = args.caller()->makeConsCell(static_cast<uint64_t>(*it), acc, CoreVM::LiteralType::Number);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
}

void listSortPairs(CoreVM::Params& args)
{
    auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    std::vector<std::pair<int64_t, uint64_t>> pairs;
    while (cur && cur->tag == 1)
    {
        auto* tuple = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(0));
        auto key = static_cast<int64_t>(tuple->getSlot(0));
        auto elem = tuple->getSlot(1);
        pairs.emplace_back(key, elem);
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }
    // Reverse to restore original order, then stable sort by key
    std::ranges::reverse(pairs);
    std::ranges::stable_sort(pairs, {}, &std::pair<int64_t, uint64_t>::first);
    // Rebuild list of elements only, right-to-left
    auto* acc = args.caller()->makeNilList(CoreVM::LiteralType::Void);
    for (auto it = pairs.rbegin(); it != pairs.rend(); ++it)
        acc = args.caller()->makeConsCell(it->second, acc, CoreVM::LiteralType::Void);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
}

void listGroupPairs(CoreVM::Params& args)
{
    auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    std::vector<std::pair<int64_t, uint64_t>> pairs;
    while (cur && cur->tag == 1)
    {
        auto* tuple = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(0));
        auto key = static_cast<int64_t>(tuple->getSlot(0));
        auto elem = tuple->getSlot(1);
        pairs.emplace_back(key, elem);
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }
    // Reverse to restore original order
    std::ranges::reverse(pairs);
    // Group by key preserving first-seen group order
    std::vector<int64_t> groupOrder;
    std::unordered_map<int64_t, std::vector<uint64_t>> groups;
    for (auto const& [key, elem]: pairs)
    {
        if (groups.find(key) == groups.end())
            groupOrder.push_back(key);
        groups[key].push_back(elem);
    }
    // Build outer list right-to-left: List<Tuple2<key, List<elem>>>
    auto* outerAcc = args.caller()->makeNilList(CoreVM::LiteralType::Object);
    for (auto it = groupOrder.rbegin(); it != groupOrder.rend(); ++it)
    {
        auto key = *it;
        auto const& elems = groups[key];
        // Build inner list right-to-left
        auto* innerAcc = args.caller()->makeNilList(CoreVM::LiteralType::Void);
        for (auto eit = elems.rbegin(); eit != elems.rend(); ++eit)
            innerAcc = args.caller()->makeConsCell(*eit, innerAcc, CoreVM::LiteralType::Void);
        // Build Tuple2(key, innerList)
        auto* tuple = args.caller()->allocObject(CoreVM::BuiltinTypeId::Tuple2);
        tuple->setSlot(0, static_cast<uint64_t>(key));
        tuple->setSlot(1, reinterpret_cast<uintptr_t>(innerAcc));
        tuple->setSlot(2, CoreVM::packTypeTag(CoreVM::LiteralType::Number, CoreVM::LiteralType::Object));
        // Cons tuple onto outer list
        outerAcc = args.caller()->makeConsCell(
            reinterpret_cast<uintptr_t>(tuple), outerAcc, CoreVM::LiteralType::Object);
    }
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(outerAcc)));
}

void listNth(CoreVM::Params& args)
{
    auto index = args.getInt(1);
    auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(2)));
    int64_t i = 0;
    while (cur && cur->tag == 1 && i < index)
    {
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
        ++i;
    }
    if (cur && cur->tag == 1 && i == index)
    {
        auto elemType = static_cast<CoreVM::LiteralType>(cur->getSlot(2));
        auto* some = args.caller()->makeSomeOption(cur->getSlot(0), elemType);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
    }
    else
    {
        auto* none = args.caller()->makeNoneOption();
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
    }
}

void listLast(CoreVM::Params& args)
{
    auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    if (!cur || cur->tag == 0)
    {
        auto* none = args.caller()->makeNoneOption();
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
    }
    else
    {
        while (cur->tag == 1)
        {
            auto* next = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            if (!next || next->tag == 0)
                break;
            cur = next;
        }
        auto elemType = static_cast<CoreVM::LiteralType>(cur->getSlot(2));
        auto* some = args.caller()->makeSomeOption(cur->getSlot(0), elemType);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
    }
}

void listReplicate(CoreVM::Params& args)
{
    auto count = args.getInt(1);
    auto value = static_cast<uint64_t>(args.getInt(2));
    auto* acc = args.caller()->makeNilList(CoreVM::LiteralType::Void);
    for (int64_t i = 0; i < count; ++i)
        acc = args.caller()->makeConsCell(value, acc, CoreVM::LiteralType::Void);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
}

void listCharRange(CoreVM::Params& args)
{
    auto startOrd = args.getInt(1);
    auto endOrd = args.getInt(2);
    auto* acc = args.caller()->makeNilList(CoreVM::LiteralType::String);
    if (startOrd <= endOrd)
    {
        for (auto ord = endOrd; ord >= startOrd; --ord)
        {
            auto* str = args.caller()->newString(std::string(1, static_cast<char>(ord)));
            acc = args.caller()->makeConsCell(
                reinterpret_cast<uintptr_t>(str), acc, CoreVM::LiteralType::String);
        }
    }
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
}

void listRange(CoreVM::Params& args)
{
    auto start = args.getInt(1);
    auto step = args.getInt(2);
    auto end = args.getInt(3);

    // Collect values first, then build list right-to-left
    std::vector<int64_t> values;
    if (step > 0)
    {
        for (auto val = start; val <= end; val += step)
            values.push_back(val);
    }
    else if (step < 0)
    {
        for (auto val = start; val >= end; val += step)
            values.push_back(val);
    }

    auto* acc = args.caller()->makeNilList(CoreVM::LiteralType::Number);
    for (auto it = values.rbegin(); it != values.rend(); ++it)
        acc = args.caller()->makeConsCell(static_cast<uint64_t>(*it), acc, CoreVM::LiteralType::Number);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
}

void listToString(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    args.setResult(args.caller()->newString(valueToString(reinterpret_cast<uintptr_t>(obj), args.caller())));
}

void objectToString(CoreVM::Params& args)
{
    auto rawVal = static_cast<uint64_t>(args.getInt(1));
    args.setResult(args.caller()->newString(valueToString(rawVal, args.caller())));
}

// ---------------------------------------------------------------------------
// String operations
// ---------------------------------------------------------------------------

void stringRepeat(CoreVM::Params& args)
{
    auto const& str = args.getString(1);
    auto const count = args.getInt(2);
    std::string result;
    if (count > 0)
    {
        result.reserve(static_cast<size_t>(count) * str.size());
        for (int64_t i = 0; i < count; ++i)
            result += str;
    }
    args.setResult(args.caller()->newString(result));
}

void stringReplace(CoreVM::Params& args)
{
    auto text = std::string(args.getString(3));
    auto const old_s = args.getString(1);
    auto const new_s = args.getString(2);
    if (!old_s.empty())
    {
        size_t pos = 0;
        while ((pos = text.find(old_s, pos)) != std::string::npos)
        {
            text.replace(pos, old_s.size(), new_s);
            pos += new_s.size();
        }
    }
    args.setResult(args.caller()->newString(text));
}

void stringSplit(CoreVM::Params& args)
{
    auto const text = std::string(args.getString(2));
    auto const delim = std::string(args.getString(1));
    auto* runner = args.caller();

    std::vector<std::string> parts;
    if (delim.empty())
    {
        for (auto c: text)
            parts.emplace_back(1, c);
    }
    else
    {
        size_t pos = 0;
        size_t found = 0;
        while ((found = text.find(delim, pos)) != std::string::npos)
        {
            parts.push_back(text.substr(pos, found - pos));
            pos = found + delim.size();
        }
        parts.push_back(text.substr(pos));
    }

    // Build cons-cell list right-to-left
    auto* list = runner->makeNilList(CoreVM::LiteralType::String);
    for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        list = runner->makeConsCell(
            reinterpret_cast<uintptr_t>(runner->newString(*it)), list, CoreVM::LiteralType::String);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
}

void stringJoin(CoreVM::Params& args)
{
    auto const sep = std::string(args.getString(1));
    auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(2)));

    std::string result;
    bool first = true;
    while (list && list->tag == 1)
    {
        if (!first)
            result += sep;
        auto* str = reinterpret_cast<CoreVM::CoreString*>(list->getSlot(0));
        if (str)
            result += *str;
        list = reinterpret_cast<CoreVM::TypedObject*>(list->getSlot(1));
        first = false;
    }
    args.setResult(args.caller()->newString(result));
}

void stringTrim(CoreVM::Params& args)
{
    auto str = std::string(args.getString(1));
    auto const start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
        args.setResult(args.caller()->newString(""));
    else
    {
        auto const end = str.find_last_not_of(" \t\n\r");
        args.setResult(args.caller()->newString(str.substr(start, end - start + 1)));
    }
}

void stringToLower(CoreVM::Params& args)
{
    auto str = std::string(args.getString(1));
    std::ranges::transform(str, str.begin(), ::tolower);
    args.setResult(args.caller()->newString(str));
}

void stringToUpper(CoreVM::Params& args)
{
    auto str = std::string(args.getString(1));
    std::ranges::transform(str, str.begin(), ::toupper);
    args.setResult(args.caller()->newString(str));
}

void stringContains(CoreVM::Params& args)
{
    args.setResult(args.getString(1).find(args.getString(2)) != std::string_view::npos);
}

void stringStartsWith(CoreVM::Params& args)
{
    args.setResult(args.getString(1).starts_with(args.getString(2)));
}

void stringEndsWith(CoreVM::Params& args)
{
    args.setResult(args.getString(1).ends_with(args.getString(2)));
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

void formatDatetime(CoreVM::Params& args)
{
    auto const epoch = static_cast<time_t>(args.getInt(1));
    struct tm tm {};
#ifdef _WIN32
    gmtime_s(&tm, &epoch);
#else
    gmtime_r(&epoch, &tm);
#endif
    auto result = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                              tm.tm_year + 1900,
                              tm.tm_mon + 1,
                              tm.tm_mday,
                              tm.tm_hour,
                              tm.tm_min,
                              tm.tm_sec);
    args.setResult(args.caller()->newString(result));
}

void formatMode(CoreVM::Params& args)
{
    auto const mode = static_cast<int>(args.getInt(1));
    std::string result;
    result += (mode & 0400) ? 'r' : '-';
    result += (mode & 0200) ? 'w' : '-';
    result += (mode & 0100) ? 'x' : '-';
    result += (mode & 0040) ? 'r' : '-';
    result += (mode & 0020) ? 'w' : '-';
    result += (mode & 0010) ? 'x' : '-';
    result += (mode & 0004) ? 'r' : '-';
    result += (mode & 0002) ? 'w' : '-';
    result += (mode & 0001) ? 'x' : '-';
    args.setResult(args.caller()->newString(result));
}

namespace
{
    /// Formats an integer with the given thousand separator string.
    std::string formatNumberWithSeparator(std::string_view separator, int64_t number)
    {
        auto const isNegative = number < 0;
        auto digits = std::to_string(isNegative ? -number : number);

        std::string result;
        auto const len = digits.size();
        for (size_t i = 0; i < len; ++i)
        {
            if (i > 0 && (len - i) % 3 == 0)
                result.append(separator);
            result += digits[i];
        }

        if (isNegative)
            result.insert(0, "-");

        return result;
    }
} // namespace

void formatNumber(CoreVM::Params& args)
{
    auto const separator = args.getString(1);
    auto const number = args.getInt(2);
    args.setResult(args.caller()->newString(formatNumberWithSeparator(separator, number)));
}

void formatNumberWithLocale(CoreVM::Params& args)
{
    auto const number = args.getInt(1);
    try
    {
        auto const loc = std::locale("");
        auto const sep = std::string(1, std::use_facet<std::numpunct<char>>(loc).thousands_sep());
        args.setResult(args.caller()->newString(formatNumberWithSeparator(sep, number)));
    }
    catch (std::runtime_error const&)
    {
        // Fallback: no separator if locale is unavailable
        args.setResult(args.caller()->newString(std::to_string(number)));
    }
}

void modeIsReadable(CoreVM::Params& args)
{
    auto const mode = static_cast<int>(args.getInt(1));
    args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0444) != 0 ? 1 : 0));
}

void modeIsWritable(CoreVM::Params& args)
{
    auto const mode = static_cast<int>(args.getInt(1));
    args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0222) != 0 ? 1 : 0));
}

void modeIsExecutable(CoreVM::Params& args)
{
    auto const mode = static_cast<int>(args.getInt(1));
    args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0111) != 0 ? 1 : 0));
}

// ---------------------------------------------------------------------------
// Random number generation
// ---------------------------------------------------------------------------

void randNoArgs(CoreVM::Params& args)
{
    static thread_local std::mt19937_64 rng { std::random_device {}() };
    std::uniform_int_distribution<int64_t> dist(1, INT64_MAX);
    args.setResult(static_cast<CoreVM::CoreNumber>(dist(rng)));
}

void randRange(CoreVM::Params& args)
{
    static thread_local std::mt19937_64 rng { std::random_device {}() };
    auto const minVal = args.getInt(1);
    auto const maxVal = args.getInt(2);
    std::uniform_int_distribution<int64_t> dist(minVal, maxVal);
    args.setResult(static_cast<CoreVM::CoreNumber>(dist(rng)));
}

// ---------------------------------------------------------------------------
// Shared implementation resolver
// ---------------------------------------------------------------------------

std::optional<CoreVM::NativeCallback::Functor> resolveSharedImpl(std::string_view name, size_t arity)
{
    // List operations
    if (name == "list_concat" && arity == 2)
        return &listConcat;
    if (name == "list_head" && arity == 1)
        return &listHead;
    if (name == "list_tail" && arity == 1)
        return &listTail;
    if (name == "list_length" && arity == 1)
        return &listLength;
    if (name == "list_isEmpty" && arity == 1)
        return &listIsEmpty;
    if (name == "list_sort" && arity == 1)
        return &listSort;
    if (name == "list_distinct" && arity == 1)
        return &listDistinct;
    if (name == "list_sort_pairs" && arity == 1)
        return &listSortPairs;
    if (name == "list_group_pairs" && arity == 1)
        return &listGroupPairs;
    if (name == "list_nth" && arity == 2)
        return &listNth;
    if (name == "list_last" && arity == 1)
        return &listLast;
    if (name == "list_replicate" && arity == 2)
        return &listReplicate;
    if (name == "list_char_range" && arity == 2)
        return &listCharRange;
    if (name == "list_range" && arity == 3)
        return &listRange;
    if (name == "list_to_string" && arity == 1)
        return &listToString;
    if (name == "object_to_string" && arity == 1)
        return &objectToString;

    // String operations
    if (name == "string_repeat" && arity == 2)
        return &stringRepeat;
    if (name == "string_replace" && arity == 3)
        return &stringReplace;
    if (name == "string_split" && arity == 2)
        return &stringSplit;
    if (name == "string_join" && arity == 2)
        return &stringJoin;
    if (name == "string_trim" && arity == 1)
        return &stringTrim;
    if (name == "string_toLower" && arity == 1)
        return &stringToLower;
    if (name == "string_toUpper" && arity == 1)
        return &stringToUpper;
    if (name == "string_contains" && arity == 2)
        return &stringContains;
    if (name == "string_startsWith" && arity == 2)
        return &stringStartsWith;
    if (name == "string_endsWith" && arity == 2)
        return &stringEndsWith;

    // Formatting helpers
    if (name == "format_datetime" && arity == 1)
        return &formatDatetime;
    if (name == "format_mode" && arity == 1)
        return &formatMode;
    if (name == "format_number" && arity == 2)
        return &formatNumber;
    if (name == "format_number" && arity == 1)
        return &formatNumberWithLocale;
    if (name == "mode_isReadable" && arity == 1)
        return &modeIsReadable;
    if (name == "mode_isWritable" && arity == 1)
        return &modeIsWritable;
    if (name == "mode_isExecutable" && arity == 1)
        return &modeIsExecutable;

    // Random number generation
    if (name == "rand" && arity == 0)
        return &randNoArgs;
    if (name == "rand" && arity == 2)
        return &randRange;

    return std::nullopt;
}

} // namespace endo::builtins
