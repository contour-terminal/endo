// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/FileManager.hpp>
#include <endo-language/builtins/StdlibDescriptors.hpp>
#include <endo-language/builtins/TypeFormatters.hpp>

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <locale>
#include <random>
#include <ranges>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace endo::builtins
{

// ---------------------------------------------------------------------------
// Path operations
// ---------------------------------------------------------------------------

void pathTemporaryDirectory(CoreVM::Params& args)
{
    args.setResult(args.caller()->newString(std::filesystem::temp_directory_path().string()));
}

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
        case CoreVM::LiteralType::Number: return std::to_string(static_cast<int64_t>(rawVal));
        default: break;
    }
    // Void/Object — delegate to valueToString for recursive container handling
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
        // Use registered formatter if available
        if (obj->type->formatFn)
            return obj->type->formatFn(*obj, runner);
        // Fallback for types without custom formatter (user-defined types)
        if (obj->type->kind == CoreVM::TypeKind::Product)
            return formatProduct(*obj, runner);
        if (obj->type->kind == CoreVM::TypeKind::Sum)
            return formatSum(*obj, runner);
        return std::format("<{}@{:#x}>", obj->type->name, rawVal);
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
    for (auto& element: std::ranges::reverse_view(elements))
        acc = args.caller()->makeConsCell(element, acc, elemType);
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
    for (auto& element: std::ranges::reverse_view(elements))
        acc = args.caller()->makeConsCell(static_cast<uint64_t>(element), acc, CoreVM::LiteralType::Number);
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
    for (auto& element: std::ranges::reverse_view(elements))
        acc = args.caller()->makeConsCell(static_cast<uint64_t>(element), acc, CoreVM::LiteralType::Number);
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
    for (auto& pair: std::ranges::reverse_view(pairs))
        acc = args.caller()->makeConsCell(pair.second, acc, CoreVM::LiteralType::Void);
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
    for (long key: std::ranges::reverse_view(groupOrder))
    {
        auto const& elems = groups[key];
        // Build inner list right-to-left
        auto* innerAcc = args.caller()->makeNilList(CoreVM::LiteralType::Void);
        for (unsigned long elem: std::ranges::reverse_view(elems))
            innerAcc = args.caller()->makeConsCell(elem, innerAcc, CoreVM::LiteralType::Void);
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
    for (auto& value: std::ranges::reverse_view(values))
        acc = args.caller()->makeConsCell(static_cast<uint64_t>(value), acc, CoreVM::LiteralType::Number);
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
    for (auto& part: std::ranges::reverse_view(parts))
        list = runner->makeConsCell(
            reinterpret_cast<uintptr_t>(runner->newString(part)), list, CoreVM::LiteralType::String);
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

/// Extracts raw permission bits from either a raw integer or a FileMode object.
int extractModeBits(CoreVM::Params& args)
{
    auto const rawVal = static_cast<uint64_t>(args.getInt(1));
    if (args.caller()->isKnownObject(rawVal))
    {
        auto const* obj = reinterpret_cast<CoreVM::TypedObject const*>(static_cast<uintptr_t>(rawVal));
        if (obj->type->id == CoreVM::BuiltinTypeId::FileMode)
            return static_cast<int>(obj->getSlot(0));
    }
    return static_cast<int>(rawVal);
}

void formatMode(CoreVM::Params& args)
{
    auto const mode = extractModeBits(args);
    args.setResult(args.caller()->newString(formatFileModeToString(mode)));
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
    auto const mode = extractModeBits(args);
    args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0444) != 0 ? 1 : 0));
}

void modeIsWritable(CoreVM::Params& args)
{
    auto const mode = extractModeBits(args);
    args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0222) != 0 ? 1 : 0));
}

void modeIsExecutable(CoreVM::Params& args)
{
    auto const mode = extractModeBits(args);
    args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0111) != 0 ? 1 : 0));
}

// ---------------------------------------------------------------------------
// FileMode operations
// ---------------------------------------------------------------------------

std::string formatFileModeToString(int64_t mode)
{
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
    return result;
}

CoreVM::TypedObject* makeFileModeFromBits(CoreVM::Runner* runner, int64_t mode)
{
    auto* obj = runner->allocObject(CoreVM::BuiltinTypeId::FileMode);
    obj->setSlot(0, static_cast<uint64_t>(mode));
    return obj;
}

void fileModeFromBits(CoreVM::Params& args)
{
    auto const bits = args.getInt(1);
    auto* obj = makeFileModeFromBits(args.caller(), bits);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void fileModeIsReadable(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const bits = static_cast<int64_t>(obj->getSlot(0));
    args.setResult(static_cast<CoreVM::CoreNumber>((bits & 0444) != 0 ? 1 : 0));
}

void fileModeIsWritable(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const bits = static_cast<int64_t>(obj->getSlot(0));
    args.setResult(static_cast<CoreVM::CoreNumber>((bits & 0222) != 0 ? 1 : 0));
}

void fileModeIsExecutable(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const bits = static_cast<int64_t>(obj->getSlot(0));
    args.setResult(static_cast<CoreVM::CoreNumber>((bits & 0111) != 0 ? 1 : 0));
}

void fileModeOwner(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const bits = static_cast<int64_t>(obj->getSlot(0));
    args.setResult(static_cast<CoreVM::CoreNumber>((bits >> 6) & 7));
}

void fileModeGroup(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const bits = static_cast<int64_t>(obj->getSlot(0));
    args.setResult(static_cast<CoreVM::CoreNumber>((bits >> 3) & 7));
}

void fileModeOther(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const bits = static_cast<int64_t>(obj->getSlot(0));
    args.setResult(static_cast<CoreVM::CoreNumber>(bits & 7));
}

// ---------------------------------------------------------------------------
// Size operations
// ---------------------------------------------------------------------------

std::string formatSizeToString(int64_t bytes)
{
    constexpr int64_t KB = 1024;
    constexpr int64_t MB = KB * 1024;
    constexpr int64_t GB = MB * 1024;
    constexpr int64_t TB = GB * 1024;
    if (bytes >= TB)
    {
        auto const whole = bytes / TB;
        auto const frac = (bytes % TB) * 10 / TB;
        return frac == 0 ? std::format("{} TB", whole) : std::format("{}.{} TB", whole, frac);
    }
    if (bytes >= GB)
    {
        auto const whole = bytes / GB;
        auto const frac = (bytes % GB) * 10 / GB;
        return frac == 0 ? std::format("{} GB", whole) : std::format("{}.{} GB", whole, frac);
    }
    if (bytes >= MB)
    {
        auto const whole = bytes / MB;
        auto const frac = (bytes % MB) * 10 / MB;
        return frac == 0 ? std::format("{} MB", whole) : std::format("{}.{} MB", whole, frac);
    }
    if (bytes >= KB)
    {
        auto const whole = bytes / KB;
        auto const frac = (bytes % KB) * 10 / KB;
        return frac == 0 ? std::format("{} KB", whole) : std::format("{}.{} KB", whole, frac);
    }
    return std::format("{} B", bytes);
}

CoreVM::TypedObject* makeSizeFromBytes(CoreVM::Runner* runner, int64_t bytes)
{
    auto* obj = runner->allocObject(CoreVM::BuiltinTypeId::Size);
    obj->setSlot(0, static_cast<uint64_t>(bytes));
    return obj;
}

void sizeFromBytes(CoreVM::Params& args)
{
    auto const bytes = args.getInt(1);
    auto* obj = makeSizeFromBytes(args.caller(), bytes);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void sizeFromKB(CoreVM::Params& args)
{
    auto const kb = args.getInt(1);
    auto* obj = makeSizeFromBytes(args.caller(), kb * 1024);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void sizeFromMB(CoreVM::Params& args)
{
    auto const mb = args.getInt(1);
    auto* obj = makeSizeFromBytes(args.caller(), mb * 1024 * 1024);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void sizeFromGB(CoreVM::Params& args)
{
    auto const gb = args.getInt(1);
    auto* obj = makeSizeFromBytes(args.caller(), gb * int64_t { 1024 } * 1024 * 1024);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void sizeFromTB(CoreVM::Params& args)
{
    auto const tb = args.getInt(1);
    auto* obj = makeSizeFromBytes(args.caller(), tb * int64_t { 1024 } * 1024 * 1024 * 1024);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

// ---------------------------------------------------------------------------
// DateTime operations
// ---------------------------------------------------------------------------

CoreVM::TypedObject* makeDateTimeFromEpoch(CoreVM::Runner* runner, int64_t epoch)
{
    auto const epochTime = static_cast<time_t>(epoch);
    struct tm tm {};
#ifdef _WIN32
    gmtime_s(&tm, &epochTime);
#else
    gmtime_r(&epochTime, &tm);
#endif

    auto* dt = runner->allocObject(CoreVM::BuiltinTypeId::DateTime);
    dt->setSlot(0, static_cast<uint64_t>(tm.tm_year) + 1900); // year
    dt->setSlot(1, static_cast<uint64_t>(tm.tm_mon) + 1);     // month (1-12)
    dt->setSlot(2, static_cast<uint64_t>(tm.tm_mday));        // day
    dt->setSlot(3, static_cast<uint64_t>(tm.tm_hour));        // hour
    dt->setSlot(4, static_cast<uint64_t>(tm.tm_min));         // minute
    dt->setSlot(5, static_cast<uint64_t>(tm.tm_sec));         // second
    dt->setSlot(6, static_cast<uint64_t>(epoch));             // epoch
    return dt;
}

void dateTimeNow(CoreVM::Params& args)
{
    auto const now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    auto* dt = makeDateTimeFromEpoch(args.caller(), static_cast<int64_t>(now));
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(dt)));
}

void dateTimeFromEpoch(CoreVM::Params& args)
{
    auto const epoch = args.getInt(1);
    auto* dt = makeDateTimeFromEpoch(args.caller(), epoch);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(dt)));
}

// ---------------------------------------------------------------------------
// TimeSpan operations
// ---------------------------------------------------------------------------

std::string formatTimeSpanToString(int64_t milliseconds)
{
    if (milliseconds == 0)
        return "0ms";

    auto const negative = milliseconds < 0;
    auto ms = negative ? -milliseconds : milliseconds;

    auto const days = ms / 86'400'000;
    ms %= 86'400'000;
    auto const hours = ms / 3'600'000;
    ms %= 3'600'000;
    auto const minutes = ms / 60'000;
    ms %= 60'000;
    auto const seconds = ms / 1'000;
    ms %= 1'000;

    std::string result;
    if (negative)
        result += "-";
    if (days > 0)
        result += std::format("{}d ", days);
    if (hours > 0)
        result += std::format("{}h ", hours);
    if (minutes > 0)
        result += std::format("{}m ", minutes);
    if (seconds > 0)
        result += std::format("{}s ", seconds);
    if (ms > 0)
        result += std::format("{}ms ", ms);

    // Remove trailing space
    if (!result.empty() && result.back() == ' ')
        result.pop_back();

    return result;
}

CoreVM::TypedObject* makeTimeSpanFromMs(CoreVM::Runner* runner, int64_t ms)
{
    auto* obj = runner->allocObject(CoreVM::BuiltinTypeId::TimeSpan);
    obj->setSlot(0, static_cast<uint64_t>(ms));
    return obj;
}

void timespanFromMs(CoreVM::Params& args)
{
    auto const ms = args.getInt(1);
    auto* obj = makeTimeSpanFromMs(args.caller(), ms);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void timespanFromSeconds(CoreVM::Params& args)
{
    auto const seconds = args.getInt(1);
    auto* obj = makeTimeSpanFromMs(args.caller(), seconds * 1'000);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void timespanFromMinutes(CoreVM::Params& args)
{
    auto const minutes = args.getInt(1);
    auto* obj = makeTimeSpanFromMs(args.caller(), minutes * 60'000);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void timespanFromHours(CoreVM::Params& args)
{
    auto const hours = args.getInt(1);
    auto* obj = makeTimeSpanFromMs(args.caller(), hours * 3'600'000);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void timespanFromDays(CoreVM::Params& args)
{
    auto const days = args.getInt(1);
    auto* obj = makeTimeSpanFromMs(args.caller(), days * 86'400'000);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void timespanSleep(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const ms = static_cast<int64_t>(obj->getSlot(0));
    if (ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    args.setResult(CoreVM::CoreNumber(0));
}

void formatTimeSpan(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const ms = static_cast<int64_t>(obj->getSlot(0));
    args.setResult(args.caller()->newString(formatTimeSpanToString(ms)));
}

// ---------------------------------------------------------------------------
// Markdown operations
// ---------------------------------------------------------------------------

CoreVM::TypedObject* makeMarkdown(CoreVM::Runner* runner, std::string const& content)
{
    auto* obj = runner->allocObject(CoreVM::BuiltinTypeId::Markdown);
    obj->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(content)));
    return obj;
}

void markdownCreate(CoreVM::Params& args)
{
    auto const& text = args.getString(1);
    auto* obj = makeMarkdown(args.caller(), std::string(text));
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(obj)));
}

void markdownToHtml(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const* content =
        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(0)));
    if (!content)
    {
        args.setResult(args.caller()->newString(""));
        return;
    }

    auto const& md = *content;
    std::string html;
    bool inCodeBlock = false;

    auto lines = std::vector<std::string>();
    size_t pos = 0;
    while (pos < md.size())
    {
        auto const nl = md.find('\n', pos);
        if (nl == std::string::npos)
        {
            lines.emplace_back(md.substr(pos));
            break;
        }
        lines.emplace_back(md.substr(pos, nl - pos));
        pos = nl + 1;
    }

    for (auto const& line: lines)
    {
        if (line.starts_with("```"))
        {
            if (inCodeBlock)
            {
                html += "</code></pre>\n";
                inCodeBlock = false;
            }
            else
            {
                html += "<pre><code>";
                inCodeBlock = true;
            }
            continue;
        }

        if (inCodeBlock)
        {
            html += line + "\n";
            continue;
        }

        if (line.starts_with("### "))
        {
            html += "<h3>" + line.substr(4) + "</h3>\n";
        }
        else if (line.starts_with("## "))
        {
            html += "<h2>" + line.substr(3) + "</h2>\n";
        }
        else if (line.starts_with("# "))
        {
            html += "<h1>" + line.substr(2) + "</h1>\n";
        }
        else if (line.starts_with("> "))
        {
            html += "<blockquote>" + line.substr(2) + "</blockquote>\n";
        }
        else if (line.starts_with("- ") || line.starts_with("* "))
        {
            html += "<ul><li>" + line.substr(2) + "</li></ul>\n";
        }
        else if (!line.empty())
        {
            // Inline formatting: **bold**, *italic*, `code`
            auto processed = std::string();
            for (size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '`')
                {
                    auto const end = line.find('`', i + 1);
                    if (end != std::string::npos)
                    {
                        processed += "<code>" + line.substr(i + 1, end - i - 1) + "</code>";
                        i = end;
                        continue;
                    }
                }
                if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*')
                {
                    auto const end = line.find("**", i + 2);
                    if (end != std::string::npos)
                    {
                        processed += "<strong>" + line.substr(i + 2, end - i - 2) + "</strong>";
                        i = end + 1;
                        continue;
                    }
                }
                if (line[i] == '*')
                {
                    auto const end = line.find('*', i + 1);
                    if (end != std::string::npos)
                    {
                        processed += "<em>" + line.substr(i + 1, end - i - 1) + "</em>";
                        i = end;
                        continue;
                    }
                }
                processed += line[i];
            }
            html += "<p>" + processed + "</p>\n";
        }
    }

    if (inCodeBlock)
        html += "</code></pre>\n";

    args.setResult(args.caller()->newString(html));
}

void markdownToText(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const* content =
        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(0)));
    if (!content)
    {
        args.setResult(args.caller()->newString(""));
        return;
    }

    auto const& md = *content;
    std::string text;
    bool inCodeBlock = false;

    size_t pos = 0;
    while (pos < md.size())
    {
        auto const nl = md.find('\n', pos);
        auto const lineEnd = (nl == std::string::npos) ? md.size() : nl;
        auto line = md.substr(pos, lineEnd - pos);
        pos = (nl == std::string::npos) ? md.size() : nl + 1;

        if (line.starts_with("```"))
        {
            inCodeBlock = !inCodeBlock;
            continue;
        }

        if (inCodeBlock)
        {
            if (!text.empty())
                text += '\n';
            text += line;
            continue;
        }

        // Strip heading markers
        while (line.starts_with("#"))
            line = line.substr(1);
        if (line.starts_with(" "))
            line = line.substr(1);

        // Strip blockquote markers
        if (line.starts_with("> "))
            line = line.substr(2);

        // Strip list markers
        if (line.starts_with("- ") || line.starts_with("* "))
            line = line.substr(2);

        // Strip inline formatting: **bold**, *italic*, `code`
        auto stripped = std::string();
        for (size_t i = 0; i < line.size(); ++i)
        {
            if (line[i] == '`')
            {
                auto const end = line.find('`', i + 1);
                if (end != std::string::npos)
                {
                    stripped += line.substr(i + 1, end - i - 1);
                    i = end;
                    continue;
                }
            }
            if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*')
            {
                auto const end = line.find("**", i + 2);
                if (end != std::string::npos)
                {
                    stripped += line.substr(i + 2, end - i - 2);
                    i = end + 1;
                    continue;
                }
            }
            if (line[i] == '*')
            {
                auto const end = line.find('*', i + 1);
                if (end != std::string::npos)
                {
                    stripped += line.substr(i + 1, end - i - 1);
                    i = end;
                    continue;
                }
            }
            stripped += line[i];
        }

        if (!text.empty() && !stripped.empty())
            text += '\n';
        text += stripped;
    }

    args.setResult(args.caller()->newString(text));
}

void markdownContent(CoreVM::Params& args)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
    auto const* content =
        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(0)));
    if (content)
        args.setResult(args.caller()->newString(std::string(*content)));
    else
        args.setResult(args.caller()->newString(""));
}

// ---------------------------------------------------------------------------
// JSON operations
// ---------------------------------------------------------------------------

namespace
{

    struct JsonPathSegment
    {
        enum class Kind : uint8_t
        {
            Key,
            Array,
        };
        Kind kind;
        std::string key;
    };

    std::vector<JsonPathSegment> parseJsonPath(std::string_view path)
    {
        std::vector<JsonPathSegment> segments;
        size_t i = 0;
        while (i < path.size())
        {
            if (path[i] == '.')
            {
                ++i;
                if (i >= path.size() || path[i] == '[')
                    continue; // skip lone dot before [] or at end
                auto const start = i;
                while (i < path.size() && path[i] != '.' && path[i] != '[')
                    ++i;
                segments.push_back(
                    { JsonPathSegment::Kind::Key, std::string(path.substr(start, i - start)) });
            }
            else if (path[i] == '[' && i + 1 < path.size() && path[i + 1] == ']')
            {
                segments.push_back({ JsonPathSegment::Kind::Array, {} });
                i += 2;
            }
            else
            {
                ++i; // skip unexpected characters
            }
        }
        return segments;
    }

    void walkJson(nlohmann::json const& node,
                  std::vector<JsonPathSegment> const& segments,
                  size_t index,
                  std::vector<std::string>& results)
    {
        if (index == segments.size())
        {
            if (node.is_string())
                results.push_back(node.get<std::string>());
            else if (node.is_number_integer())
                results.push_back(std::to_string(node.get<int64_t>()));
            else if (node.is_number_float())
                results.push_back(std::format("{}", node.get<double>()));
            else if (node.is_boolean())
                results.emplace_back(node.get<bool>() ? "true" : "false");
            else if (node.is_null())
                results.emplace_back("null");
            return;
        }

        auto const& seg = segments[index];
        if (seg.kind == JsonPathSegment::Kind::Key)
        {
            if (node.is_object() && node.contains(seg.key))
                walkJson(node[seg.key], segments, index + 1, results);
        }
        else if (seg.kind == JsonPathSegment::Kind::Array)
        {
            if (node.is_array())
            {
                for (auto const& elem: node)
                    walkJson(elem, segments, index + 1, results);
            }
        }
    }

} // namespace

void jsonQuery(CoreVM::Params& args)
{
    auto const path = std::string(args.getString(1));
    auto const json = std::string(args.getString(2));
    auto* runner = args.caller();

    std::vector<std::string> results;
    try
    {
        auto const segments = parseJsonPath(path);
        if (!segments.empty())
        {
            auto const doc = nlohmann::json::parse(json);
            walkJson(doc, segments, 0, results);
        }
    }
    catch (...) // NOLINT(bugprone-empty-catch)
    {
        // On parse error or any exception: return empty list
    }

    // Build cons-cell list right-to-left (same pattern as stringSplit)
    auto* list = runner->makeNilList(CoreVM::LiteralType::String);
    for (auto& result: std::ranges::reverse_view(results))
        list = runner->makeConsCell(
            reinterpret_cast<uintptr_t>(runner->newString(result)), list, CoreVM::LiteralType::String);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

void monotonicMs(CoreVM::Params& args)
{
    auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    args.setResult(static_cast<CoreVM::CoreNumber>(now));
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
// File I/O operations
// ---------------------------------------------------------------------------

namespace
{
    /// Global file manager instance for the File I/O module.
    endo::FileManager& globalFileManager()
    {
        static endo::FileManager instance;
        return instance;
    }
} // namespace

void fileOpen(CoreVM::Params& args)
{
    auto const& path = args.getString(1);
    auto const& mode = args.getString(2);
    auto& mgr = globalFileManager();

    if (auto handle = mgr.open(path, mode))
    {
        auto* obj = args.caller()->allocObject(CoreVM::BuiltinTypeId::FileHandle);
        obj->setSlot(0, static_cast<uint64_t>(*handle));
        auto* result =
            args.caller()->makeOkResult(reinterpret_cast<uintptr_t>(obj), CoreVM::LiteralType::Object);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
    }
    else
    {
        auto const errMsg = std::format("Failed to open file '{}' with mode '{}'", path, mode);
        auto* errStr = args.caller()->newString(errMsg);
        auto* result =
            args.caller()->makeErrorResult(reinterpret_cast<uintptr_t>(errStr), CoreVM::LiteralType::String);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
    }
}

void fileClose(CoreVM::Params& args)
{
    auto* handleObj = args.getObject(1);
    auto const handle = static_cast<int64_t>(handleObj->getSlot(0));
    globalFileManager().close(handle);
    args.setResult(CoreVM::CoreNumber(0));
}

void fileReadLine(CoreVM::Params& args)
{
    auto* handleObj = args.getObject(1);
    auto const handle = static_cast<int64_t>(handleObj->getSlot(0));

    if (auto line = globalFileManager().readLine(handle))
    {
        auto* str = args.caller()->newString(*line);
        auto* some =
            args.caller()->makeSomeOption(reinterpret_cast<uintptr_t>(str), CoreVM::LiteralType::String);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
    }
    else
    {
        auto* none = args.caller()->makeNoneOption();
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
    }
}

void fileReadAll(CoreVM::Params& args)
{
    auto const& path = args.getString(1);
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        auto const errMsg = std::format("Failed to open file '{}'", path);
        auto* errStr = args.caller()->newString(errMsg);
        auto* result =
            args.caller()->makeErrorResult(reinterpret_cast<uintptr_t>(errStr), CoreVM::LiteralType::String);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto* str = args.caller()->newString(content);
    auto* result = args.caller()->makeOkResult(reinterpret_cast<uintptr_t>(str), CoreVM::LiteralType::String);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
}

void fileWriteAll(CoreVM::Params& args)
{
    auto const& path = args.getString(1);
    auto const& content = args.getString(2);
    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open())
    {
        auto const errMsg = std::format("Failed to open file '{}' for writing", path);
        auto* errStr = args.caller()->newString(errMsg);
        auto* result =
            args.caller()->makeErrorResult(reinterpret_cast<uintptr_t>(errStr), CoreVM::LiteralType::String);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        return;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    auto* result = args.caller()->makeOkResult(0, CoreVM::LiteralType::Void);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
}

void fileAppendAll(CoreVM::Params& args)
{
    auto const& path = args.getString(1);
    auto const& content = args.getString(2);
    std::ofstream file(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!file.is_open())
    {
        auto const errMsg = std::format("Failed to open file '{}' for appending", path);
        auto* errStr = args.caller()->newString(errMsg);
        auto* result =
            args.caller()->makeErrorResult(reinterpret_cast<uintptr_t>(errStr), CoreVM::LiteralType::String);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        return;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    auto* result = args.caller()->makeOkResult(0, CoreVM::LiteralType::Void);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
}

void fileSize(CoreVM::Params& args)
{
    auto const& path = args.getString(1);
    std::error_code ec;
    auto const size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        auto const errMsg = std::format("Failed to get size of '{}': {}", path, ec.message());
        auto* errStr = args.caller()->newString(errMsg);
        auto* result =
            args.caller()->makeErrorResult(reinterpret_cast<uintptr_t>(errStr), CoreVM::LiteralType::String);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        return;
    }

    auto* result = args.caller()->makeOkResult(static_cast<uint64_t>(size), CoreVM::LiteralType::Number);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
}

void fileExists(CoreVM::Params& args)
{
    auto const& path = args.getString(1);
    auto const exists = std::filesystem::exists(path);
    args.setResult(CoreVM::CoreNumber(exists ? 1 : 0));
}

void fileDelete(CoreVM::Params& args)
{
    auto const& path = args.getString(1);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec)
    {
        auto const errMsg = std::format("Failed to delete '{}': {}", path, ec.message());
        auto* errStr = args.caller()->newString(errMsg);
        auto* result =
            args.caller()->makeErrorResult(reinterpret_cast<uintptr_t>(errStr), CoreVM::LiteralType::String);
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        return;
    }

    auto* result = args.caller()->makeOkResult(0, CoreVM::LiteralType::Void);
    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
}

// ---------------------------------------------------------------------------
// Shared implementation resolver — delegates to StdlibDescriptors table
// ---------------------------------------------------------------------------

std::optional<CoreVM::NativeCallback::Functor> resolveSharedImpl(std::string_view name, size_t arity)
{
    return resolveStdlibImpl(name, arity);
}

} // namespace endo::builtins
