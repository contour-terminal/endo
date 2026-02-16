// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <algorithm>
#include <bit>
#include <format>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "IRGenerator.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include <emscripten/emscripten.h>

namespace
{

// Forward declaration for mutual recursion.
std::string valueToString(uint64_t rawVal, CoreVM::Runner* runner);

/// Converts a slot value to string using the known LiteralType from the type tag slot.
/// Strings inside containers are wrapped in double quotes.
std::string slotValueToString(uint64_t rawVal,
                              CoreVM::LiteralType type,
                              CoreVM::Runner* runner,
                              bool quoteStrings = true)
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
    return valueToString(rawVal, runner);
}

/// Recursively converts a runtime value (number, tuple, list, option, etc.) to a printable string.
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
            auto const packed = obj->getSlot(2);
            auto const t0 = CoreVM::unpackTypeTag(packed, 0);
            auto const t1 = CoreVM::unpackTypeTag(packed, 1);
            return "(" + slotValueToString(obj->getSlot(0), t0, runner) + ", "
                   + slotValueToString(obj->getSlot(1), t1, runner) + ")";
        }
        if (typeId == CoreVM::BuiltinTypeId::Tuple3)
        {
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
            std::string result = "{ ";
            for (size_t i = 0; i < obj->type->fields.size(); ++i)
            {
                if (i > 0)
                    result += "; ";
                result += obj->type->fields[i].name;
                result += " = ";
                auto slotVal = obj->getSlot(static_cast<uint8_t>(i));
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

/// Escapes a string for safe inclusion in a JSON string value.
std::string jsonEscape(std::string const& s)
{
    std::string result;
    result.reserve(s.size() + 16);
    for (auto c: s)
    {
        switch (c)
        {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    result += std::format("\\u{:04x}", static_cast<unsigned>(c));
                else
                    result += c;
                break;
        }
    }
    return result;
}

/// Playground runtime that wraps the Endo interpreter for browser use.
/// Modeled after TestRuntime but with REPL persistence and no POSIX dependencies.
class PlaygroundRuntime
{
  public:
    CoreVM::Runtime runtime;
    CoreVM::diagnostics::BufferedReport report;
    std::string capturedOutput;
    endo::FSharpPersistentState fsharpState;

    // Mock state for shell command simulation
    std::string mockCmdName;
    std::vector<std::string> mockCmdArgs;
    bool mockSubstActive = false;
    std::string mockSubstBuffer;
    std::unordered_map<std::string, std::string> mockEnv;

    PlaygroundRuntime()
    {
        // --- callproc callbacks (mock, just return 0) ---
        runtime.registerFunction("callproc")
            .param<std::vector<std::string>>("args")
            .returnType(CoreVM::LiteralType::Number)
            .bind([this](CoreVM::Params& params) {
                auto const& args = params.getStringArray(1);
                if (!args.empty() && args[0] == "echo")
                {
                    std::string output;
                    for (size_t i = 1; i < args.size(); ++i)
                    {
                        if (i > 1)
                            output += ' ';
                        output += args[i];
                    }
                    output += '\n';
                    if (mockSubstActive)
                        mockSubstBuffer += output;
                    else
                        capturedOutput += output;
                }
                params.setResult(CoreVM::CoreNumber(0));
            });

        runtime.registerFunction("callproc")
            .param<bool>("last_in_chain")
            .param<std::vector<std::string>>("args")
            .returnType(CoreVM::LiteralType::Number)
            .bind([this](CoreVM::Params& params) {
                auto const& args = params.getStringArray(2);
                if (!args.empty() && args[0] == "echo")
                {
                    std::string output;
                    for (size_t i = 1; i < args.size(); ++i)
                    {
                        if (i > 1)
                            output += ' ';
                        output += args[i];
                    }
                    output += '\n';
                    if (mockSubstActive)
                        mockSubstBuffer += output;
                    else
                        capturedOutput += output;
                }
                params.setResult(CoreVM::CoreNumber(0));
            });

        // --- print/println ---
        runtime.registerFunction("print")
            .param<CoreVM::CoreString>("text")
            .returnType(CoreVM::LiteralType::Void)
            .bind([this](CoreVM::Params& params) { capturedOutput += params.getString(1); });

        runtime.registerFunction("println")
            .param<CoreVM::CoreString>("text")
            .returnType(CoreVM::LiteralType::Void)
            .bind([this](CoreVM::Params& params) {
                capturedOutput += params.getString(1);
                capturedOutput += '\n';
            });

        // --- string_repeat ---
        runtime.registerFunction("string_repeat")
            .param<CoreVM::CoreString>("str")
            .param<CoreVM::CoreNumber>("count")
            .returnType(CoreVM::LiteralType::String)
            .bind([](CoreVM::Params& args) {
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
            });

        // --- list_to_string ---
        runtime.registerFunction("list_to_string")
            .param<CoreVM::CoreNumber>("obj")
            .returnType(CoreVM::LiteralType::String)
            .bind([](CoreVM::Params& args) {
                auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
                args.setResult(
                    args.caller()->newString(valueToString(reinterpret_cast<uintptr_t>(obj), args.caller())));
            });

        // --- list_concat ---
        runtime.registerFunction("list_concat")
            .param<CoreVM::CoreNumber>("left")
            .param<CoreVM::CoreNumber>("right")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
                CoreVM::TypedObject* acc = right;
                for (auto it = elements.rbegin(); it != elements.rend(); ++it)
                    acc = args.caller()->makeConsCell(*it, acc);
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
            });

        // --- list_head ---
        runtime.registerFunction("list_head")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
                auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
                if (!list || list->tag == 0)
                {
                    auto* none = args.caller()->makeNoneOption();
                    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
                }
                else
                {
                    auto* some = args.caller()->makeSomeOption(list->getSlot(0));
                    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
                }
            });

        // --- list_tail ---
        runtime.registerFunction("list_tail")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
            });

        // --- list_length ---
        runtime.registerFunction("list_length")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
                auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
                int64_t count = 0;
                while (cur && cur->tag == 1)
                {
                    ++count;
                    cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
                }
                args.setResult(static_cast<CoreVM::CoreNumber>(count));
            });

        // --- list_sort ---
        runtime.registerFunction("list_sort")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
                    acc = args.caller()->makeConsCell(
                        static_cast<uint64_t>(*it), acc, CoreVM::LiteralType::Number);
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
            });

        // --- list_distinct ---
        runtime.registerFunction("list_distinct")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
                    acc = args.caller()->makeConsCell(
                        static_cast<uint64_t>(*it), acc, CoreVM::LiteralType::Number);
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
            });

        // --- list_sort_pairs ---
        runtime.registerFunction("list_sort_pairs")
            .param<CoreVM::CoreNumber>("pairs")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
                std::ranges::reverse(pairs);
                std::ranges::stable_sort(pairs, {}, &std::pair<int64_t, uint64_t>::first);
                auto* acc = args.caller()->makeNilList();
                for (auto it = pairs.rbegin(); it != pairs.rend(); ++it)
                    acc = args.caller()->makeConsCell(it->second, acc);
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
            });

        // --- list_group_pairs ---
        runtime.registerFunction("list_group_pairs")
            .param<CoreVM::CoreNumber>("pairs")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
                std::ranges::reverse(pairs);
                std::vector<int64_t> groupOrder;
                std::unordered_map<int64_t, std::vector<uint64_t>> groups;
                for (auto const& [key, elem]: pairs)
                {
                    if (groups.find(key) == groups.end())
                        groupOrder.push_back(key);
                    groups[key].push_back(elem);
                }
                auto* outerAcc = args.caller()->makeNilList(CoreVM::LiteralType::Object);
                for (auto it = groupOrder.rbegin(); it != groupOrder.rend(); ++it)
                {
                    auto key = *it;
                    auto const& elems = groups[key];
                    auto* innerAcc = args.caller()->makeNilList();
                    for (auto eit = elems.rbegin(); eit != elems.rend(); ++eit)
                        innerAcc = args.caller()->makeConsCell(*eit, innerAcc);
                    auto* tuple = args.caller()->allocObject(CoreVM::BuiltinTypeId::Tuple2);
                    tuple->setSlot(0, static_cast<uint64_t>(key));
                    tuple->setSlot(1, reinterpret_cast<uintptr_t>(innerAcc));
                    tuple->setSlot(
                        2, CoreVM::packTypeTag(CoreVM::LiteralType::Number, CoreVM::LiteralType::Object));
                    outerAcc = args.caller()->makeConsCell(
                        reinterpret_cast<uintptr_t>(tuple), outerAcc, CoreVM::LiteralType::Object);
                }
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(outerAcc)));
            });

        // --- list_isEmpty ---
        runtime.registerFunction("list_isEmpty")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind([](CoreVM::Params& args) {
                auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
                args.setResult(!list || list->tag == 0);
            });

        // --- list_nth ---
        runtime.registerFunction("list_nth")
            .param<CoreVM::CoreNumber>("index")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
                    auto* some = args.caller()->makeSomeOption(cur->getSlot(0));
                    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
                }
                else
                {
                    auto* none = args.caller()->makeNoneOption();
                    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
                }
            });

        // --- list_last ---
        runtime.registerFunction("list_last")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
                    auto* some = args.caller()->makeSomeOption(cur->getSlot(0));
                    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
                }
            });

        // --- list_replicate ---
        runtime.registerFunction("list_replicate")
            .param<CoreVM::CoreNumber>("count")
            .param<CoreVM::CoreNumber>("value")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
                auto count = args.getInt(1);
                auto value = static_cast<uint64_t>(args.getInt(2));
                auto* acc = args.caller()->makeNilList();
                for (int64_t i = 0; i < count; ++i)
                    acc = args.caller()->makeConsCell(value, acc);
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
            });

        // --- list_char_range ---
        runtime.registerFunction("list_char_range")
            .param<CoreVM::CoreNumber>("startOrd")
            .param<CoreVM::CoreNumber>("endOrd")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
            });

        // --- object_to_string ---
        runtime.registerFunction("object_to_string")
            .param<CoreVM::CoreNumber>("obj")
            .returnType(CoreVM::LiteralType::String)
            .bind([](CoreVM::Params& args) {
                auto rawVal = static_cast<uint64_t>(args.getInt(1));
                args.setResult(args.caller()->newString(valueToString(rawVal, args.caller())));
            });

        // --- display_result ---
        runtime.registerFunction("display_result")
            .param<CoreVM::CoreNumber>("value")
            .returnType(CoreVM::LiteralType::Void)
            .bind([this](CoreVM::Params& args) {
                auto rawVal = static_cast<uint64_t>(args.getInt(1));
                auto str = valueToString(rawVal, args.caller());
                capturedOutput += str;
                capturedOutput += '\n';
            });

        // --- internal.subst_start / internal.subst_end ---
        runtime.registerFunction("internal.subst_start")
            .returnType(CoreVM::LiteralType::Void)
            .bind([this](CoreVM::Params&) {
                mockSubstActive = true;
                mockSubstBuffer.clear();
            });
        runtime.registerFunction("internal.subst_end")
            .returnType(CoreVM::LiteralType::String)
            .bind([this](CoreVM::Params& args) {
                mockSubstActive = false;
                auto result = std::move(mockSubstBuffer);
                while (!result.empty() && result.back() == '\n')
                    result.pop_back();
                args.setResult(std::move(result));
            });

        // --- internal.cmd_start / cmd_arg / cmd_exec / cmd_exec_piped ---
        runtime.registerFunction("internal.cmd_start")
            .param<CoreVM::CoreString>("cmd")
            .returnType(CoreVM::LiteralType::Void)
            .bind([this](CoreVM::Params& args) {
                mockCmdName = args.getString(1);
                mockCmdArgs.clear();
            });
        runtime.registerFunction("internal.cmd_arg")
            .param<CoreVM::CoreString>("arg")
            .returnType(CoreVM::LiteralType::Void)
            .bind([this](CoreVM::Params& args) { mockCmdArgs.emplace_back(args.getString(1)); });
        runtime.registerFunction("internal.cmd_exec")
            .returnType(CoreVM::LiteralType::Number)
            .bind([this](CoreVM::Params& args) {
                if (mockCmdName == "echo")
                {
                    std::string output;
                    for (size_t i = 0; i < mockCmdArgs.size(); ++i)
                    {
                        if (i > 0)
                            output += ' ';
                        output += mockCmdArgs[i];
                    }
                    output += '\n';
                    if (mockSubstActive)
                        mockSubstBuffer += output;
                    else
                        capturedOutput += output;
                }
                args.setResult(CoreVM::CoreNumber(0));
            });
        runtime.registerFunction("internal.cmd_exec_piped")
            .param<bool>("last_in_chain")
            .returnType(CoreVM::LiteralType::Number)
            .bind([this](CoreVM::Params& args) {
                if (mockCmdName == "echo" || mockCmdName == "/bin/echo")
                {
                    std::string output;
                    for (size_t i = 0; i < mockCmdArgs.size(); ++i)
                    {
                        if (i > 0)
                            output += ' ';
                        output += mockCmdArgs[i];
                    }
                    output += '\n';
                    if (mockSubstActive)
                        mockSubstBuffer += output;
                    else
                        capturedOutput += output;
                }
                args.setResult(CoreVM::CoreNumber(0));
            });

        // --- getvar.exitstatus ---
        runtime.registerFunction("getvar.exitstatus")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) { args.setResult(CoreVM::CoreNumber(0)); });

        // --- env.has / env.get ---
        runtime.registerFunction("env.has")
            .param<CoreVM::CoreString>("key")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind([this](CoreVM::Params& args) {
                auto const& key = args.getString(1);
                args.setResult(mockEnv.contains(key));
            });
        runtime.registerFunction("env.get")
            .param<CoreVM::CoreString>("key")
            .returnType(CoreVM::LiteralType::String)
            .bind([this](CoreVM::Params& args) {
                auto const& key = args.getString(1);
                if (auto const it = mockEnv.find(key); it != mockEnv.end())
                    args.setResult(args.caller()->newString(it->second));
                else
                    args.setResult(args.caller()->newString(""));
            });

        // --- export ---
        runtime.registerFunction("export")
            .param<CoreVM::CoreString>("name")
            .param<CoreVM::CoreString>("value")
            .returnType(CoreVM::LiteralType::Void)
            .bind([this](CoreVM::Params& args) {
                mockEnv[std::string(args.getString(1))] = std::string(args.getString(2));
            });
        runtime.registerFunction("export")
            .param<CoreVM::CoreString>("name")
            .returnType(CoreVM::LiteralType::Void)
            .bind([](CoreVM::Params&) {});

        // --- which_find ---
        runtime.registerFunction("which_find")
            .param<CoreVM::CoreString>("program")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
                // In playground, no programs are available
                auto* none = args.caller()->makeNoneOption();
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
            });

        // --- String standard library builtins ---
        runtime.registerFunction("string_trim")
            .param<CoreVM::CoreString>("text")
            .returnType(CoreVM::LiteralType::String)
            .bind([](CoreVM::Params& args) {
                auto str = std::string(args.getString(1));
                auto const start = str.find_first_not_of(" \t\n\r");
                if (start == std::string::npos)
                    args.setResult(args.caller()->newString(""));
                else
                {
                    auto const end = str.find_last_not_of(" \t\n\r");
                    args.setResult(args.caller()->newString(str.substr(start, end - start + 1)));
                }
            });

        runtime.registerFunction("string_toLower")
            .param<CoreVM::CoreString>("text")
            .returnType(CoreVM::LiteralType::String)
            .bind([](CoreVM::Params& args) {
                auto str = std::string(args.getString(1));
                std::ranges::transform(str, str.begin(), ::tolower);
                args.setResult(args.caller()->newString(str));
            });

        runtime.registerFunction("string_toUpper")
            .param<CoreVM::CoreString>("text")
            .returnType(CoreVM::LiteralType::String)
            .bind([](CoreVM::Params& args) {
                auto str = std::string(args.getString(1));
                std::ranges::transform(str, str.begin(), ::toupper);
                args.setResult(args.caller()->newString(str));
            });

        runtime.registerFunction("string_contains")
            .param<CoreVM::CoreString>("haystack")
            .param<CoreVM::CoreString>("needle")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind([](CoreVM::Params& args) {
                args.setResult(args.getString(1).find(args.getString(2)) != std::string_view::npos);
            });

        runtime.registerFunction("string_startsWith")
            .param<CoreVM::CoreString>("text")
            .param<CoreVM::CoreString>("prefix")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind([](CoreVM::Params& args) {
                args.setResult(args.getString(1).starts_with(args.getString(2)));
            });

        runtime.registerFunction("string_endsWith")
            .param<CoreVM::CoreString>("text")
            .param<CoreVM::CoreString>("suffix")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind(
                [](CoreVM::Params& args) { args.setResult(args.getString(1).ends_with(args.getString(2))); });

        runtime.registerFunction("string_replace")
            .param<CoreVM::CoreString>("old_str")
            .param<CoreVM::CoreString>("new_str")
            .param<CoreVM::CoreString>("text")
            .returnType(CoreVM::LiteralType::String)
            .bind([](CoreVM::Params& args) {
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
            });

        runtime.registerFunction("string_split")
            .param<CoreVM::CoreString>("delimiter")
            .param<CoreVM::CoreString>("text")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
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
                auto* list = runner->makeNilList(CoreVM::LiteralType::String);
                for (auto it = parts.rbegin(); it != parts.rend(); ++it)
                    list = runner->makeConsCell(reinterpret_cast<uintptr_t>(runner->newString(*it)),
                                                list,
                                                CoreVM::LiteralType::String);
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
            });

        runtime.registerFunction("string_join")
            .param<CoreVM::CoreString>("separator")
            .param<CoreVM::CoreNumber>("list")
            .returnType(CoreVM::LiteralType::String)
            .bind([](CoreVM::Params& args) {
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
            });

        // --- Data source stubs ---
        auto dummyCallback = [](CoreVM::Params& args) {
            auto* nil = args.caller()->makeNilList();
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(nil)));
        };
        runtime.registerFunction("open_json")
            .param<CoreVM::CoreString>("path")
            .param<CoreVM::CoreString>("schema_desc")
            .param<CoreVM::CoreNumber>("type_id")
            .returnType(CoreVM::LiteralType::Number)
            .bind(dummyCallback);
        runtime.registerFunction("open_csv")
            .param<CoreVM::CoreString>("path")
            .param<CoreVM::CoreString>("schema_desc")
            .param<CoreVM::CoreNumber>("type_id")
            .returnType(CoreVM::LiteralType::Number)
            .bind(dummyCallback);
        runtime.registerFunction("from_json")
            .param<CoreVM::CoreString>("source_cmd")
            .param<CoreVM::CoreString>("schema_desc")
            .param<CoreVM::CoreNumber>("type_id")
            .returnType(CoreVM::LiteralType::Number)
            .bind(dummyCallback);
        runtime.registerFunction("from_csv")
            .param<CoreVM::CoreString>("source_cmd")
            .param<CoreVM::CoreString>("schema_desc")
            .param<CoreVM::CoreNumber>("type_id")
            .returnType(CoreVM::LiteralType::Number)
            .bind(dummyCallback);

        // --- fetch stubs ---
        runtime.registerFunction("fetch")
            .param<CoreVM::CoreString>("url")
            .returnType(CoreVM::LiteralType::Number)
            .bind(dummyCallback);
        runtime.registerFunction("fetch")
            .param<CoreVM::CoreString>("url")
            .param<CoreVM::CoreNumber>("headers")
            .returnType(CoreVM::LiteralType::Number)
            .bind(dummyCallback);

        // --- rand ---
        runtime.registerFunction("rand")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
                static thread_local std::mt19937_64 rng { std::random_device {}() };
                std::uniform_int_distribution<int64_t> dist(1, INT64_MAX);
                args.setResult(static_cast<CoreVM::CoreNumber>(dist(rng)));
            });
        runtime.registerFunction("rand")
            .param<CoreVM::CoreNumber>("min")
            .param<CoreVM::CoreNumber>("max")
            .returnType(CoreVM::LiteralType::Number)
            .bind([](CoreVM::Params& args) {
                static thread_local std::mt19937_64 rng { std::random_device {}() };
                auto const minVal = args.getInt(1);
                auto const maxVal = args.getInt(2);
                std::uniform_int_distribution<int64_t> dist(minVal, maxVal);
                args.setResult(static_cast<CoreVM::CoreNumber>(dist(rng)));
            });
    }

    /// Returns the singleton instance of PlaygroundRuntime.
    static PlaygroundRuntime& instance()
    {
        static PlaygroundRuntime inst;
        return inst;
    }
};

} // anonymous namespace

extern "C"
{

    /// Evaluates Endo source code and returns JSON result.
    /// Returns a pointer to a static buffer with JSON:
    ///   {"status":"ok","output":"..."} or {"status":"error","errors":["..."]}
    EMSCRIPTEN_KEEPALIVE
    char const* endo_eval(char const* source)
    {
        static std::string resultBuffer;

        auto& pg = PlaygroundRuntime::instance();
        pg.report.clear();
        pg.capturedOutput.clear();

        // Parse
        endo::Parser parser(pg.runtime, pg.report, std::make_unique<endo::StringSource>(std::string(source)));

        // Provide known function names for the parser
        if (!pg.fsharpState.functions.empty() || !pg.fsharpState.valueBindings.empty())
        {
            std::unordered_set<std::string> names;
            for (auto const& [name, _]: pg.fsharpState.functions)
                names.insert(name);
            for (auto const& binding: pg.fsharpState.valueBindings)
                names.insert(binding.name);
            parser.setKnownFSharpFunctions(std::move(names));
        }

        auto ast = parser.parse();
        if (!ast || pg.report.containsFailures())
        {
            std::string errors = "[";
            bool first = true;
            for (auto const& msg: pg.report.messages())
            {
                if (!first)
                    errors += ",";
                first = false;
                errors += "\"" + jsonEscape(msg.text) + "\"";
            }
            errors += "]";
            resultBuffer = R"({"status":"error","errors":)" + errors + "}";
            return resultBuffer.c_str();
        }

        // Generate IR with persistent state
        auto ir = endo::IRGenerator::generate(*ast, pg.report, pg.runtime, &pg.fsharpState);
        if (!ir || pg.report.containsFailures())
        {
            std::string errors = "[";
            bool first = true;
            for (auto const& msg: pg.report.messages())
            {
                if (!first)
                    errors += ",";
                first = false;
                errors += "\"" + jsonEscape(msg.text) + "\"";
            }
            errors += "]";
            resultBuffer = R"({"status":"error","errors":)" + errors + "}";
            return resultBuffer.c_str();
        }

        // Retain the AST so persisted function body pointers remain valid
        pg.fsharpState.retainedASTs.push_back(std::move(ast));

        // Generate target code
        CoreVM::TargetCodeGenerator codegen;
        auto targetProgram = codegen.generate(ir.get());
        if (!targetProgram)
        {
            resultBuffer = R"({"status":"error","errors":["Code generation failed"]})";
            return resultBuffer.c_str();
        }

        // Link
        if (!targetProgram->link(&pg.runtime, &pg.report))
        {
            resultBuffer = R"({"status":"error","errors":["Link failed"]})";
            return resultBuffer.c_str();
        }

        // Find main function
        CoreVM::Function const* fn = targetProgram->findFunction("@main");
        if (!fn)
        {
            resultBuffer = R"({"status":"error","errors":["No main function found"]})";
            return resultBuffer.c_str();
        }

        // Execute
        CoreVM::Runner::Globals globals;
        CoreVM::Runner runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
        runner.run();

        // Save mutable binding values for persistence
        auto const& stack = runner.stack();
        for (size_t i = 0; i < pg.fsharpState.valueBindings.size() && i < stack.size(); ++i)
            if (pg.fsharpState.valueBindings[i].isMutable)
                pg.fsharpState.mutableSnapshots[pg.fsharpState.valueBindings[i].name] = stack[i];

        // Build success JSON
        resultBuffer = R"({"status":"ok","output":")" + jsonEscape(pg.capturedOutput) + "\"}";
        return resultBuffer.c_str();
    }

    /// Resets the REPL session state.
    EMSCRIPTEN_KEEPALIVE
    void endo_reset()
    {
        auto& pg = PlaygroundRuntime::instance();
        pg.fsharpState.functions.clear();
        pg.fsharpState.valueBindings.clear();
        pg.fsharpState.retainedASTs.clear();
        pg.fsharpState.mutableSnapshots.clear();
    }

    /// Returns the version string.
    EMSCRIPTEN_KEEPALIVE
    char const* endo_version()
    {
        return "0.1.0-playground";
    }

} // extern "C"
