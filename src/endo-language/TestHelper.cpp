// SPDX-License-Identifier: Apache-2.0
#include "TestHelper.hpp"

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <algorithm>
#include <bit>
#include <format>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "IRGenerator.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

namespace endo::test
{

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
        // Check if the value is a string pointer (e.g. from record field extraction in mapped lists)
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
                // Check if this is a ProcessInfo record (has "cpu" float field)
                bool const isProcessInfo = obj->type->id == CoreVM::BuiltinTypeId::ProcessInfo;

                // Record type: { field1 = val1; field2 = val2 }
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
                                auto const* str = reinterpret_cast<CoreVM::CoreString const*>(
                                    static_cast<uintptr_t>(slotVal));
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
            // Unknown object type — fallback
            return std::to_string(static_cast<int64_t>(rawVal));
        }
        return std::to_string(static_cast<int64_t>(rawVal));
    }
} // namespace

TestRuntime::TestRuntime()
{
    // Register minimal builtins for the parser to work
    runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&TestRuntime::dummyCallProc, this);

    runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&TestRuntime::dummyCallProcPiped, this);

    // Register print builtin (no newline)
    runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&TestRuntime::builtinPrint, this);

    // Register println builtin (with newline)
    runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&TestRuntime::builtinPrintln, this);

    // Register string_repeat builtin: "ha" * 3 → "hahaha"
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

    // Helper: converts a list TypedObject to string "[1; 2; 3]" (delegates to valueToString)
    auto listToString = [](CoreVM::TypedObject* obj, CoreVM::Runner* runner) -> std::string {
        return valueToString(reinterpret_cast<uintptr_t>(obj), runner);
    };

    // Register list_to_string builtin: converts list object to "[1; 2; 3]" string
    runtime.registerFunction("list_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind([listToString](CoreVM::Params& args) {
            auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            args.setResult(args.caller()->newString(listToString(obj, args.caller())));
        });

    // Register list_concat builtin: concatenates two lists
    runtime.registerFunction("list_concat")
        .param<CoreVM::CoreNumber>("left")
        .param<CoreVM::CoreNumber>("right")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* left = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            auto* right = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(2)));

            // If left is Nil, return right
            if (!left || left->tag == 0)
            {
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(right)));
                return;
            }

            // Collect left list elements
            std::vector<uint64_t> elements;
            auto* cur = left;
            while (cur && cur->tag == 1)
            {
                elements.push_back(cur->getSlot(0));
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }

            // Build result list from right-to-left, starting with right list as tail
            CoreVM::TypedObject* acc = right;
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
            {
                acc = args.caller()->makeConsCell(*it, acc);
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // Register list_head builtin: returns Option (Some head | None)
    runtime.registerFunction("list_head")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            if (!list || list->tag == 0)
            {
                // Nil → None
                auto* none = args.caller()->makeNoneOption();
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
            }
            else
            {
                // Cons → Some(head)
                auto* some = args.caller()->makeSomeOption(list->getSlot(0));
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
            }
        });

    // Register list_tail builtin: returns tail of list (or [] for empty)
    runtime.registerFunction("list_tail")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            if (!list || list->tag == 0)
            {
                // Nil → return new Nil
                auto* nil = args.caller()->makeNilList();
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(nil)));
            }
            else
            {
                // Cons → return tail pointer
                args.setResult(static_cast<CoreVM::CoreNumber>(list->getSlot(1)));
            }
        });

    // Register list_length builtin: returns number of elements
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

    // Register list_sort builtin: sorts list elements numerically (ascending)
    runtime.registerFunction("list_sort")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));

            // Collect elements
            std::vector<int64_t> elements;
            while (cur && cur->tag == 1)
            {
                elements.push_back(static_cast<int64_t>(cur->getSlot(0)));
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }

            // Sort ascending
            std::ranges::sort(elements);

            // Rebuild list right-to-left
            auto* acc = args.caller()->makeNilList(CoreVM::LiteralType::Number);
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
                acc =
                    args.caller()->makeConsCell(static_cast<uint64_t>(*it), acc, CoreVM::LiteralType::Number);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // Register list_distinct builtin: removes duplicate elements preserving first-seen order
    runtime.registerFunction("list_distinct")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));

            // Collect elements preserving first-seen order
            std::vector<int64_t> elements;
            std::unordered_set<int64_t> seen;
            while (cur && cur->tag == 1)
            {
                auto val = static_cast<int64_t>(cur->getSlot(0));
                if (seen.insert(val).second)
                    elements.push_back(val);
                cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
            }

            // Rebuild list right-to-left
            auto* acc = args.caller()->makeNilList(CoreVM::LiteralType::Number);
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
                acc =
                    args.caller()->makeConsCell(static_cast<uint64_t>(*it), acc, CoreVM::LiteralType::Number);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // Register list_sort_pairs builtin: sorts list of Tuple2(key, elem) by key, returns list of elements
    runtime.registerFunction("list_sort_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));

            // Collect (key, elem) pairs — input is in reversed order from IR loop
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
            auto* acc = args.caller()->makeNilList();
            for (auto it = pairs.rbegin(); it != pairs.rend(); ++it)
                acc = args.caller()->makeConsCell(it->second, acc);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(acc)));
        });

    // Register list_group_pairs builtin: groups list of Tuple2(key, elem) by key
    // Returns List<Tuple2<key, List<elem>>>
    runtime.registerFunction("list_group_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* cur = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));

            // Collect (key, elem) pairs — input is in reversed order from IR loop
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
                auto* innerAcc = args.caller()->makeNilList();
                for (auto eit = elems.rbegin(); eit != elems.rend(); ++eit)
                    innerAcc = args.caller()->makeConsCell(*eit, innerAcc);

                // Build Tuple2(key, innerList)
                auto* tuple = args.caller()->allocObject(CoreVM::BuiltinTypeId::Tuple2);
                tuple->setSlot(0, static_cast<uint64_t>(key));
                tuple->setSlot(1, reinterpret_cast<uintptr_t>(innerAcc));
                tuple->setSlot(2,
                               CoreVM::packTypeTag(CoreVM::LiteralType::Number, CoreVM::LiteralType::Object));

                // Cons tuple onto outer list
                outerAcc = args.caller()->makeConsCell(
                    reinterpret_cast<uintptr_t>(tuple), outerAcc, CoreVM::LiteralType::Object);
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(outerAcc)));
        });

    // Register list_isEmpty builtin: returns true if list is Nil
    runtime.registerFunction("list_isEmpty")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(args.getInt(1)));
            args.setResult(!list || list->tag == 0);
        });

    // Register list_nth builtin: returns Option (Some element | None) at given index
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

    // Register list_last builtin: returns Option (Some lastElement | None)
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

    // Register list_replicate builtin: creates a list of N copies of a value
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

    // Register list_char_range builtin: builds a list of single-character strings from ordinal range
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

    // Register object_to_string builtin: runtime dispatch for object printing
    runtime.registerFunction("object_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto rawVal = static_cast<uint64_t>(args.getInt(1));
            args.setResult(args.caller()->newString(valueToString(rawVal, args.caller())));
        });

    // Register display_result builtin: auto-display a value (for bare expression evaluation)
    runtime.registerFunction("display_result")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto rawVal = static_cast<uint64_t>(args.getInt(1));
            auto str = valueToString(rawVal, args.caller());
            capturedOutput += str;
            capturedOutput += '\n';
        });

    // Register structured_ps mock: returns deterministic test data (3 fixed processes)
    runtime.registerFunction("structured_ps")
        .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
        .bind([](CoreVM::Params& args) {
            auto* runner = args.caller();

            // Mock process data for deterministic testing
            struct MockProc
            {
                int64_t pid;
                int64_t ppid;
                char const* user;
                double cpu;
                int64_t mem;
                char const* command;
            };
            constexpr MockProc procs[] = {
                { 1, 0, "root", 0.1, 1024, "/sbin/init" },
                { 42, 1, "alice", 15.5, 4096, "firefox" },
                { 100, 1, "bob", 2.3, 2048, "vim" },
            };

            // Build cons-cell list right-to-left
            auto* list = runner->makeNilList(CoreVM::LiteralType::Object);

            for (int i = 2; i >= 0; --i)
            {
                auto const& p = procs[i];
                auto* record = runner->allocObject(CoreVM::BuiltinTypeId::ProcessInfo);
                record->setSlot(0, static_cast<uint64_t>(p.pid));
                record->setSlot(1, static_cast<uint64_t>(p.ppid));
                record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(p.user)));
                record->setSlot(3, std::bit_cast<uint64_t>(p.cpu));
                record->setSlot(4, static_cast<uint64_t>(p.mem));
                record->setSlot(5, reinterpret_cast<uintptr_t>(runner->newString(p.command)));

                list = runner->makeConsCell(
                    reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
            }

            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // Register structured_ls mock: returns 3 deterministic test files
    runtime.registerFunction("structured_ls")
        .param<CoreVM::CoreString>("path")
        .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
        .bind([](CoreVM::Params& args) {
            auto* runner = args.caller();

            // Mock file data for deterministic testing
            struct MockFile
            {
                char const* name;
                int64_t size;
                int64_t mode;
                int64_t mtime;
                bool isDir;
            };
            constexpr MockFile files[] = {
                { "docs", 4096, 0755, 1700000000, true },
                { "hello.txt", 42, 0644, 1700001000, false },
                { "script.sh", 256, 0755, 1700002000, false },
            };

            // Build cons-cell list right-to-left
            auto* list = runner->makeNilList(CoreVM::LiteralType::Object);

            for (int i = 2; i >= 0; --i)
            {
                auto const& f = files[i];
                auto* record = runner->allocObject(CoreVM::BuiltinTypeId::FileInfo);
                record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(f.name)));
                record->setSlot(1, static_cast<uint64_t>(f.size));
                record->setSlot(2, static_cast<uint64_t>(f.mode));
                record->setSlot(3, static_cast<uint64_t>(f.mtime));
                record->setSlot(4, static_cast<uint64_t>(f.isDir ? 1 : 0));

                list = runner->makeConsCell(
                    reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
            }

            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // Register structured_jobs mock: returns 3 deterministic jobs
    runtime.registerFunction("structured_jobs")
        .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
        .bind([](CoreVM::Params& args) {
            auto* runner = args.caller();

            // Mock job data for deterministic testing
            struct MockJob
            {
                int64_t id;
                char const* state;
                char const* command;
                int64_t pid;
            };
            constexpr MockJob jobs[] = {
                { 1, "Running", "sleep 100", 1234 },
                { 2, "Stopped", "vim", 5678 },
                { 3, "Done", "make build", 9012 },
            };

            // Build cons-cell list right-to-left
            auto* list = runner->makeNilList(CoreVM::LiteralType::Object);

            for (int i = 2; i >= 0; --i)
            {
                auto const& j = jobs[i];
                auto* record = runner->allocObject(CoreVM::BuiltinTypeId::JobInfo);
                record->setSlot(0, static_cast<uint64_t>(j.id));
                record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(j.state)));
                record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(j.command)));
                record->setSlot(3, static_cast<uint64_t>(j.pid));

                list = runner->makeConsCell(
                    reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
            }

            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // Register helper builtins for FileInfo mode/mtime formatting and testing
    runtime.registerFunction("format_datetime")
        .param<CoreVM::CoreNumber>("epoch")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto const epoch = static_cast<time_t>(args.getInt(1));
            struct tm tm {};
            gmtime_r(&epoch, &tm);
            auto result = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                                      tm.tm_year + 1900,
                                      tm.tm_mon + 1,
                                      tm.tm_mday,
                                      tm.tm_hour,
                                      tm.tm_min,
                                      tm.tm_sec);
            args.setResult(args.caller()->newString(result));
        });

    runtime.registerFunction("format_mode")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
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
        });

    runtime.registerFunction("mode_isReadable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto const mode = static_cast<int>(args.getInt(1));
            args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0444) != 0 ? 1 : 0));
        });

    runtime.registerFunction("mode_isWritable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto const mode = static_cast<int>(args.getInt(1));
            args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0222) != 0 ? 1 : 0));
        });

    runtime.registerFunction("mode_isExecutable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            auto const mode = static_cast<int>(args.getInt(1));
            args.setResult(static_cast<CoreVM::CoreNumber>((mode & 0111) != 0 ? 1 : 0));
        });

    // Register command substitution builtins (needed for structured pipeline fallback and & shell commands)
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
            // Trim trailing newline like real shell subst
            auto result = std::move(mockSubstBuffer);
            while (!result.empty() && result.back() == '\n')
                result.pop_back();
            args.setResult(std::move(result));
        });

    // Register shell command building builtins (needed for & shell commands)
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
            // Mock shell: simulate "echo" by writing args to output buffer
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
    runtime.registerFunction("getvar.exitstatus")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) { args.setResult(CoreVM::CoreNumber(0)); });

    // Register structured_docker_ps mock: returns 3 container records
    runtime.registerFunction("structured_docker_ps")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* runner = args.caller();
            struct MockContainer
            {
                char const* id;
                char const* image;
                char const* command;
                char const* created;
                char const* status;
                char const* ports;
                char const* names;
            };
            constexpr MockContainer containers[] = {
                { "abc123def",
                  "nginx:latest",
                  "/docker-entrypoint…",
                  "2024-01-15 10:00:00",
                  "Up 3 hours",
                  "80/tcp",
                  "web-server" },
                { "def456ghi",
                  "postgres:16",
                  "docker-entrypoint.s…",
                  "2024-01-14 08:00:00",
                  "Up 2 days",
                  "5432/tcp",
                  "db-main" },
                { "ghi789jkl",
                  "redis:7",
                  "docker-entrypoint.s…",
                  "2024-01-13 12:00:00",
                  "Exited (0) 1 hour ago",
                  "",
                  "cache" },
            };
            auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
            for (int i = 2; i >= 0; --i)
            {
                auto const& c = containers[i];
                auto* record = runner->allocObject(CoreVM::BuiltinTypeId::OutputDefBase);
                record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(c.id)));
                record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(c.image)));
                record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(c.command)));
                record->setSlot(3, reinterpret_cast<uintptr_t>(runner->newString(c.created)));
                record->setSlot(4, reinterpret_cast<uintptr_t>(runner->newString(c.status)));
                record->setSlot(5, reinterpret_cast<uintptr_t>(runner->newString(c.ports)));
                record->setSlot(6, reinterpret_cast<uintptr_t>(runner->newString(c.names)));
                list = runner->makeConsCell(
                    reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // Register structured_docker_images mock: returns 3 image records
    runtime.registerFunction("structured_docker_images")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* runner = args.caller();
            struct MockImage
            {
                char const* id;
                char const* repository;
                char const* tag;
                char const* created;
                char const* size;
            };
            constexpr MockImage images[] = {
                { "sha256:abc", "nginx", "latest", "2024-01-10", "187MB" },
                { "sha256:def", "postgres", "16", "2024-01-08", "412MB" },
                { "sha256:ghi", "redis", "7", "2024-01-05", "130MB" },
            };
            auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
            for (int i = 2; i >= 0; --i)
            {
                auto const& img = images[i];
                constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 1;
                auto* record = runner->allocObject(typeId);
                record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(img.id)));
                record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(img.repository)));
                record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(img.tag)));
                record->setSlot(3, reinterpret_cast<uintptr_t>(runner->newString(img.created)));
                record->setSlot(4, reinterpret_cast<uintptr_t>(runner->newString(img.size)));
                list = runner->makeConsCell(
                    reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // Register structured_git_log mock: returns 3 commit records
    runtime.registerFunction("structured_git_log")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* runner = args.caller();
            struct MockCommit
            {
                char const* sha;
                char const* author;
                char const* email;
                char const* date;
                char const* message;
            };
            constexpr MockCommit commits[] = {
                { "abc123", "Alice", "alice@example.com", "2024-01-15", "feat: add login" },
                { "def456", "Bob", "bob@example.com", "2024-01-14", "fix: null check" },
                { "ghi789", "Alice", "alice@example.com", "2024-01-13", "docs: update README" },
            };
            auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
            for (int i = 2; i >= 0; --i)
            {
                auto const& c = commits[i];
                constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 2;
                auto* record = runner->allocObject(typeId);
                record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(c.sha)));
                record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(c.author)));
                record->setSlot(2, reinterpret_cast<uintptr_t>(runner->newString(c.email)));
                record->setSlot(3, reinterpret_cast<uintptr_t>(runner->newString(c.date)));
                record->setSlot(4, reinterpret_cast<uintptr_t>(runner->newString(c.message)));
                list = runner->makeConsCell(
                    reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // Register structured_git_status mock: returns 3 status entries
    runtime.registerFunction("structured_git_status")
        .returnType(CoreVM::LiteralType::Number)
        .bind([](CoreVM::Params& args) {
            auto* runner = args.caller();
            struct MockStatusEntry
            {
                char const* status;
                char const* path;
            };
            constexpr MockStatusEntry entries[] = {
                { "M", "src/main.cpp" },
                { "??", "README.md" },
                { "A", ".gitignore" },
            };
            auto* list = runner->makeNilList(CoreVM::LiteralType::Object);
            for (int i = 2; i >= 0; --i)
            {
                auto const& e = entries[i];
                constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 3;
                auto* record = runner->allocObject(typeId);
                record->setSlot(0, reinterpret_cast<uintptr_t>(runner->newString(e.status)));
                record->setSlot(1, reinterpret_cast<uintptr_t>(runner->newString(e.path)));
                list = runner->makeConsCell(
                    reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
            }
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // Register env.has builtin (returns boolean: true if key exists in mock env)
    runtime.registerFunction("env.has")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([this](CoreVM::Params& args) {
            auto const& key = args.getString(1);
            args.setResult(mockEnv.contains(key));
        });

    // Register env.get builtin (returns string value, empty if not found)
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

    // --- String standard library builtins ---

    // string_trim: removes leading/trailing whitespace
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

    // string_toLower: converts string to lowercase
    runtime.registerFunction("string_toLower")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto str = std::string(args.getString(1));
            std::ranges::transform(str, str.begin(), ::tolower);
            args.setResult(args.caller()->newString(str));
        });

    // string_toUpper: converts string to uppercase
    runtime.registerFunction("string_toUpper")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) {
            auto str = std::string(args.getString(1));
            std::ranges::transform(str, str.begin(), ::toupper);
            args.setResult(args.caller()->newString(str));
        });

    // string_contains: checks if haystack contains needle
    runtime.registerFunction("string_contains")
        .param<CoreVM::CoreString>("haystack")
        .param<CoreVM::CoreString>("needle")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) {
            args.setResult(args.getString(1).find(args.getString(2)) != std::string_view::npos);
        });

    // string_startsWith: checks if text starts with prefix
    runtime.registerFunction("string_startsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("prefix")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) { args.setResult(args.getString(1).starts_with(args.getString(2))); });

    // string_endsWith: checks if text ends with suffix
    runtime.registerFunction("string_endsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("suffix")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) { args.setResult(args.getString(1).ends_with(args.getString(2))); });

    // string_replace: replaces all occurrences of old with new in text
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

    // string_split: splits text by delimiter into list<str>
    runtime.registerFunction("string_split")
        .param<CoreVM::CoreString>("delimiter")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Number) // Returns list object pointer
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

            // Build cons-cell list right-to-left
            auto* list = runner->makeNilList(CoreVM::LiteralType::String);
            for (auto it = parts.rbegin(); it != parts.rend(); ++it)
                list = runner->makeConsCell(
                    reinterpret_cast<uintptr_t>(runner->newString(*it)), list, CoreVM::LiteralType::String);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        });

    // string_join: joins list<str> with separator
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

    // Data source wrapper stubs (open-json, open-csv, from-json, from-csv)
    auto dummyHandler = [](CoreVM::Params& args) {
        // Return an empty list (Nil)
        auto* nil = args.caller()->makeNilList();
        args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(nil)));
    };
    runtime.registerFunction("open_json")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("open_csv")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("from_json")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("from_csv")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    // HTTP fetch builtin stubs (returns Result<string, string>)
    runtime.registerFunction("fetch")
        .param<CoreVM::CoreString>("url")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("fetch")
        .param<CoreVM::CoreString>("url")
        .param<CoreVM::CoreNumber>("headers")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    // rand builtin: returns a random positive integer > 0
    runtime.registerFunction("rand").returnType(CoreVM::LiteralType::Number).bind([](CoreVM::Params& args) {
        static thread_local std::mt19937_64 rng { std::random_device {}() };
        std::uniform_int_distribution<int64_t> dist(1, INT64_MAX);
        args.setResult(static_cast<CoreVM::CoreNumber>(dist(rng)));
    });

    // rand builtin (range): returns a random integer in [min, max]
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

void TestRuntime::dummyCallProc(CoreVM::Params&)
{
}

void TestRuntime::dummyCallProcPiped(CoreVM::Params&)
{
}

void TestRuntime::builtinPrint(CoreVM::Params& params)
{
    capturedOutput += params.getString(1);
}

void TestRuntime::builtinPrintln(CoreVM::Params& params)
{
    capturedOutput += params.getString(1);
    capturedOutput += '\n';
}

void TestRuntime::setMockEnvVar(std::string const& key, std::string const& value)
{
    mockEnv[key] = value;
}

void TestRuntime::clearMockEnvVars()
{
    mockEnv.clear();
}

void TestRuntime::clearErrors()
{
    report.clear();
}

void TestRuntime::clearOutput()
{
    capturedOutput.clear();
}

bool TestRuntime::hasErrors() const
{
    return report.containsFailures();
}

std::string const& TestRuntime::output() const
{
    return capturedOutput;
}

TestRuntime& TestRuntime::instance()
{
    static TestRuntime instance;
    return instance;
}

std::unique_ptr<ast::Statement> parse(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto result = parser.parse();

    if (testRuntime.hasErrors())
    {
        return nullptr;
    }

    return result;
}

std::unique_ptr<CoreVM::IRProgram> generateIR(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();

    if (!ast || testRuntime.hasErrors())
    {
        return nullptr;
    }

    auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime);

    if (!ir || testRuntime.hasErrors())
    {
        return nullptr;
    }

    return ir;
}

bool generatesIRSuccessfully(std::string const& source)
{
    auto ir = generateIR(source);
    return ir != nullptr;
}

bool generatesIRWithError(std::string const& source, std::string_view expectedErrorSubstring)
{
    auto ir = generateIR(source);
    if (ir)
        return false; // Expected failure but IR generation succeeded

    auto const& messages = TestRuntime::instance().report.messages();
    for (auto const& msg: messages)
        if (msg.text.find(expectedErrorSubstring) != std::string::npos)
            return true;

    return false;
}

ast::Statement* getFirstStatement(ast::Statement* stmt)
{
    if (auto* compound = dynamic_cast<ast::CompoundStmt*>(stmt))
    {
        if (!compound->statements.empty())
        {
            return dynamic_cast<ast::Statement*>(compound->statements[0].get());
        }
    }
    return nullptr;
}

std::string parseAndPrintAST(std::string const& source)
{
    auto ast = parse(source);
    if (!ast)
    {
        throw ParseError("Parse failed for: \"" + source + "\"");
    }
    return ast::ASTPrinter::print(*ast);
}

ExecutionResult executeSource(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    // Generate IR
    auto ir = generateIR(source);
    if (!ir)
        return std::unexpected(TestError::IRGenerationFailed);

    // Generate target code
    CoreVM::TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(ir.get());
    if (!targetProgram)
        return std::unexpected(TestError::CodeGenerationFailed);

    // Link the program to the runtime (required for native function calls like print/println)
    if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
        return std::unexpected(TestError::LinkFailed);

    // Find the main handler
    CoreVM::Handler const* handler = targetProgram->findHandler("@main");
    if (!handler)
        return std::unexpected(TestError::HandlerNotFound);

    // Execute
    CoreVM::Runner::Globals globals;
    CoreVM::Runner runner(handler, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);

    // Runner::run() returns true if exit code was non-zero, false if it was 0
    bool exitNonZero = runner.run();
    int64_t exitCode = exitNonZero ? 1 : 0;

    return TestExecutionSuccess { .exitCode = exitCode, .output = testRuntime.output() };
}

std::string executeSourceAndGetOutput(std::string const& source)
{
    auto result = executeSource(source);
    if (!result.has_value())
        throw ExecutionError(result.error());
    return std::move(result->output);
}

bool executesSuccessfully(std::string const& source)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == 0;
}

bool executesWithExitCode(std::string const& source, int64_t expectedExitCode)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == expectedExitCode;
}

bool executesWithResult(std::string const& source, int64_t expectedExitCode, std::string_view expectedOutput)
{
    auto result = executeSource(source);
    return result.has_value() && result->exitCode == expectedExitCode && result->output == expectedOutput;
}

// =============================================================================
// Multi-prompt (REPL session) test helpers
// =============================================================================

ExecutionResult executeSession(std::vector<std::string> const& prompts)
{
    auto& testRuntime = TestRuntime::instance();
    FSharpPersistentState fsharpState;

    ExecutionResult lastResult = std::unexpected(TestError::ExecutionFailed);

    for (auto const& source: prompts)
    {
        testRuntime.clearErrors();
        testRuntime.clearOutput();

        // Parse
        Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
        if (!fsharpState.functions.empty() || !fsharpState.valueBindings.empty())
        {
            std::unordered_set<std::string> names;
            for (auto const& [name, _]: fsharpState.functions)
                names.insert(name);
            for (auto const& binding: fsharpState.valueBindings)
                names.insert(binding.name);
            parser.setKnownFSharpFunctions(std::move(names));
        }
        auto ast = parser.parse();
        if (!ast || testRuntime.hasErrors())
            return std::unexpected(TestError::ParseFailed);

        // Generate IR with persistent state
        auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime, &fsharpState);
        if (!ir || testRuntime.hasErrors())
            return std::unexpected(TestError::IRGenerationFailed);

        // Retain the AST so persisted function body pointers remain valid
        fsharpState.retainedASTs.push_back(std::move(ast));

        // Generate target code
        CoreVM::TargetCodeGenerator codegen;
        auto targetProgram = codegen.generate(ir.get());
        if (!targetProgram)
            return std::unexpected(TestError::CodeGenerationFailed);

        // Link
        if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
            return std::unexpected(TestError::LinkFailed);

        // Find main handler
        CoreVM::Handler const* handler = targetProgram->findHandler("@main");
        if (!handler)
            return std::unexpected(TestError::HandlerNotFound);

        // Execute
        CoreVM::Runner::Globals globals;
        CoreVM::Runner runner(handler, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
        bool exitNonZero = runner.run();
        int64_t exitCode = exitNonZero ? 1 : 0;

        // Save runtime values of mutable bindings for cross-prompt persistence.
        // Allocas are at the bottom of the stack (positions 0, 1, 2, ...),
        // matching the order of persisted value bindings.
        auto const& stack = runner.stack();
        for (size_t i = 0; i < fsharpState.valueBindings.size() && i < stack.size(); ++i)
            if (fsharpState.valueBindings[i].isMutable)
                fsharpState.mutableSnapshots[fsharpState.valueBindings[i].name] = stack[i];

        lastResult = TestExecutionSuccess { .exitCode = exitCode, .output = testRuntime.output() };
    }

    return lastResult;
}

std::string executeSessionAndGetOutput(std::vector<std::string> const& prompts)
{
    auto result = executeSession(prompts);
    if (!result.has_value())
        throw ExecutionError(result.error());
    return std::move(result->output);
}

bool sessionProducesOutput(std::vector<std::string> const& prompts, std::string_view expectedOutput)
{
    auto result = executeSession(prompts);
    return result.has_value() && result->output == expectedOutput;
}

// =============================================================================
// Structured pipeline test helpers
// =============================================================================

FSharpPersistentState createMockStructuredState()
{
    using CoreVM::LiteralType;

    FSharpPersistentState state;

    // DockerPsRecord (typeId = OutputDefBase = 100)
    {
        constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase;
        state.outputDefinitionTypes["DockerPsRecord"] = {
            .typeId = typeId,
            .fields = {
                { "id", 0, LiteralType::String },
                { "image", 1, LiteralType::String },
                { "command", 2, LiteralType::String },
                { "created", 3, LiteralType::String },
                { "status", 4, LiteralType::String },
                { "ports", 5, LiteralType::String },
                { "names", 6, LiteralType::String },
            },
        };
        state.structuredCommands[std::string("docker\0ps", 9)] = {
            .builtinCallbackName = "structured_docker_ps",
            .recordTypeId = typeId,
            .recordTypeName = "DockerPsRecord",
        };
        state.recordTypeFields["DockerPsRecord"] = {
            { "id", "string" },     { "image", "string" }, { "command", "string" }, { "created", "string" },
            { "status", "string" }, { "ports", "string" }, { "names", "string" },
        };
    }

    // DockerImagesRecord (typeId = OutputDefBase + 1 = 101)
    {
        constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 1;
        state.outputDefinitionTypes["DockerImagesRecord"] = {
            .typeId = typeId,
            .fields = {
                { "id", 0, LiteralType::String },
                { "repository", 1, LiteralType::String },
                { "tag", 2, LiteralType::String },
                { "created", 3, LiteralType::String },
                { "size", 4, LiteralType::String },
            },
        };
        state.structuredCommands[std::string("docker\0images", 13)] = {
            .builtinCallbackName = "structured_docker_images",
            .recordTypeId = typeId,
            .recordTypeName = "DockerImagesRecord",
        };
        state.recordTypeFields["DockerImagesRecord"] = {
            { "id", "string" },      { "repository", "string" }, { "tag", "string" },
            { "created", "string" }, { "size", "string" },
        };
    }

    // GitLogRecord (typeId = OutputDefBase + 2 = 102)
    {
        constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 2;
        state.outputDefinitionTypes["GitLogRecord"] = {
            .typeId = typeId,
            .fields = {
                { "sha", 0, LiteralType::String },
                { "author", 1, LiteralType::String },
                { "email", 2, LiteralType::String },
                { "date", 3, LiteralType::String },
                { "message", 4, LiteralType::String },
            },
        };
        state.structuredCommands[std::string("git\0log", 7)] = {
            .builtinCallbackName = "structured_git_log",
            .recordTypeId = typeId,
            .recordTypeName = "GitLogRecord",
        };
        state.recordTypeFields["GitLogRecord"] = {
            { "sha", "string" },  { "author", "string" },  { "email", "string" },
            { "date", "string" }, { "message", "string" },
        };
    }

    // GitStatusRecord (typeId = OutputDefBase + 3 = 103)
    {
        constexpr uint16_t typeId = CoreVM::BuiltinTypeId::OutputDefBase + 3;
        state.outputDefinitionTypes["GitStatusRecord"] = {
            .typeId = typeId,
            .fields = {
                { "status", 0, LiteralType::String },
                { "path", 1, LiteralType::String },
            },
        };
        state.structuredCommands[std::string("git\0status", 10)] = {
            .builtinCallbackName = "structured_git_status",
            .recordTypeId = typeId,
            .recordTypeName = "GitStatusRecord",
        };
        state.recordTypeFields["GitStatusRecord"] = {
            { "status", "string" },
            { "path", "string" },
        };
    }

    return state;
}

ExecutionResult executeSourceWithStructuredState(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();
    testRuntime.clearOutput();

    auto state = createMockStructuredState();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();
    if (!ast || testRuntime.hasErrors())
        return std::unexpected(TestError::ParseFailed);

    auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime, &state);
    if (!ir || testRuntime.hasErrors())
        return std::unexpected(TestError::IRGenerationFailed);

    CoreVM::TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(ir.get());
    if (!targetProgram)
        return std::unexpected(TestError::CodeGenerationFailed);

    if (!targetProgram->link(&testRuntime.runtime, &testRuntime.report))
        return std::unexpected(TestError::LinkFailed);

    CoreVM::Handler const* handler = targetProgram->findHandler("@main");
    if (!handler)
        return std::unexpected(TestError::HandlerNotFound);

    CoreVM::Runner::Globals globals;
    CoreVM::Runner runner(handler, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
    bool exitNonZero = runner.run();

    return TestExecutionSuccess { .exitCode = exitNonZero ? 1 : 0, .output = testRuntime.output() };
}

bool structuredExecutesWithOutput(std::string const& source, std::string_view expectedOutput)
{
    auto result = executeSourceWithStructuredState(source);
    return result.has_value() && result->output == expectedOutput;
}

} // namespace endo::test
