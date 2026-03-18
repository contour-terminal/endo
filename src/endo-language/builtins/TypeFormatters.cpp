// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/TypeFormatters.hpp>

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <bit>
#include <cassert>
#include <format>
#include <string>

namespace endo::builtins
{

// ---------------------------------------------------------------------------
// Individual type formatters
// ---------------------------------------------------------------------------

std::string formatList(CoreVM::TypedObject const& obj, CoreVM::Runner* runner)
{
    // Read element type from type tag slot 2
    auto const elemType = static_cast<CoreVM::LiteralType>(obj.getSlot(2));
    std::string result = "[";
    bool first = true;
    auto const* cur = &obj;
    while (cur && cur->type->id == CoreVM::BuiltinTypeId::List && cur->tag == 1)
    {
        if (!first)
            result += "; ";
        first = false;
        result += slotValueToString(cur->getSlot(0), elemType, runner);
        cur = reinterpret_cast<CoreVM::TypedObject const*>(cur->getSlot(1));
    }
    result += "]";
    return result;
}

std::string formatTuple2(CoreVM::TypedObject const& obj, CoreVM::Runner* runner)
{
    auto const packed = obj.getSlot(2);
    auto const t0 = CoreVM::unpackTypeTag(packed, 0);
    auto const t1 = CoreVM::unpackTypeTag(packed, 1);
    return "(" + slotValueToString(obj.getSlot(0), t0, runner) + ", "
           + slotValueToString(obj.getSlot(1), t1, runner) + ")";
}

std::string formatTuple3(CoreVM::TypedObject const& obj, CoreVM::Runner* runner)
{
    auto const packed = obj.getSlot(3);
    auto const t0 = CoreVM::unpackTypeTag(packed, 0);
    auto const t1 = CoreVM::unpackTypeTag(packed, 1);
    auto const t2 = CoreVM::unpackTypeTag(packed, 2);
    return "(" + slotValueToString(obj.getSlot(0), t0, runner) + ", "
           + slotValueToString(obj.getSlot(1), t1, runner) + ", "
           + slotValueToString(obj.getSlot(2), t2, runner) + ")";
}

std::string formatOption(CoreVM::TypedObject const& obj, CoreVM::Runner* runner)
{
    if (obj.tag == 0)
        return "None";
    auto const innerType = static_cast<CoreVM::LiteralType>(obj.getSlot(1));
    return "Some " + slotValueToString(obj.getSlot(0), innerType, runner);
}

std::string formatResult(CoreVM::TypedObject const& obj, CoreVM::Runner* runner)
{
    auto const innerType = static_cast<CoreVM::LiteralType>(obj.getSlot(1));
    if (obj.tag == 0)
        return "Error " + slotValueToString(obj.getSlot(0), innerType, runner);
    return "Ok " + slotValueToString(obj.getSlot(0), innerType, runner);
}

std::string formatSeq(CoreVM::TypedObject const& obj, [[maybe_unused]] CoreVM::Runner* runner)
{
    if (obj.tag == 0)
        return "seq {}";
    // Don't force the lazy tail to avoid infinite evaluation
    return "seq { ... }";
}

std::string formatLazy(CoreVM::TypedObject const& /*obj*/, [[maybe_unused]] CoreVM::Runner* /*runner*/)
{
    return "lazy <unevaluated>";
}

std::string formatFileHandle(CoreVM::TypedObject const& obj, [[maybe_unused]] CoreVM::Runner* /*runner*/)
{
    auto const handle = static_cast<int64_t>(obj.getSlot(0));
    return std::format("FileHandle({})", handle);
}

std::string formatSize(CoreVM::TypedObject const& obj, [[maybe_unused]] CoreVM::Runner* /*runner*/)
{
    return formatSizeToString(static_cast<int64_t>(obj.getSlot(0)));
}

std::string formatTimeSpan(CoreVM::TypedObject const& obj, [[maybe_unused]] CoreVM::Runner* /*runner*/)
{
    return formatTimeSpanToString(static_cast<int64_t>(obj.getSlot(0)));
}

std::string formatFileMode(CoreVM::TypedObject const& obj, [[maybe_unused]] CoreVM::Runner* /*runner*/)
{
    return formatFileModeToString(static_cast<int64_t>(obj.getSlot(0)));
}

std::string formatMarkdown(CoreVM::TypedObject const& obj, [[maybe_unused]] CoreVM::Runner* /*runner*/)
{
    auto const* content = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj.getSlot(0)));
    return content ? std::string(*content) : "";
}

std::string formatCallable(CoreVM::TypedObject const& obj, [[maybe_unused]] CoreVM::Runner* /*runner*/)
{
    auto const funcId = static_cast<int64_t>(obj.getSlot(0));
    return std::format("<callable #{}>", funcId);
}

std::string formatDateTime(CoreVM::TypedObject const& obj, [[maybe_unused]] CoreVM::Runner* /*runner*/)
{
    auto const year = static_cast<int>(obj.getSlot(0));
    auto const month = static_cast<int>(obj.getSlot(1));
    auto const day = static_cast<int>(obj.getSlot(2));
    auto const hour = static_cast<int>(obj.getSlot(3));
    auto const minute = static_cast<int>(obj.getSlot(4));
    auto const second = static_cast<int>(obj.getSlot(5));
    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", year, month, day, hour, minute, second);
}

// ---------------------------------------------------------------------------
// Generic formatters
// ---------------------------------------------------------------------------

std::string formatProduct(CoreVM::TypedObject const& obj, CoreVM::Runner* runner)
{
    std::string result = "{ ";
    for (size_t i = 0; i < obj.type->fields.size(); ++i)
    {
        if (i > 0)
            result += "; ";
        result += obj.type->fields[i].name;
        result += " = ";
        auto slotVal = obj.getSlot(static_cast<uint8_t>(i));

        if (obj.type->fields[i].type == CoreVM::LiteralType::Object)
        {
            // Nested object field — recursively render
            result += valueToString(slotVal, runner);
        }
        else
        {
            switch (obj.type->fields[i].type)
            {
                case CoreVM::LiteralType::String: {
                    auto const* str =
                        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(slotVal));
                    result += str ? *str : "(null)";
                    break;
                }
                case CoreVM::LiteralType::Boolean: result += slotVal ? "true" : "false"; break;
                case CoreVM::LiteralType::Float: {
                    auto const f = std::bit_cast<double>(slotVal);
                    result += std::format("{:.1f}", f);
                    break;
                }
                default: result += std::to_string(static_cast<int64_t>(slotVal)); break;
            }
        }
    }
    result += " }";
    return result;
}

std::string formatSum(CoreVM::TypedObject const& obj, CoreVM::Runner* runner)
{
    auto const* variantInfo = obj.type->getVariant(obj.tag);
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
            // Use actual field type when available, falling back to Void for heuristic dispatch
            auto const payloadType = (hasNamedFields && i < variantInfo->fields.size())
                                         ? variantInfo->fields[i].type
                                         : CoreVM::LiteralType::Void;
            result += slotValueToString(obj.getSlot(i), payloadType, runner, false);
        }
        if (hasNamedFields)
            result += ")";
    }
    return result;
}

std::string formatRef(CoreVM::TypedObject const& obj, CoreVM::Runner* runner)
{
    auto const innerType = static_cast<CoreVM::LiteralType>(obj.getSlot(1));
    return "ref " + slotValueToString(obj.getSlot(0), innerType, runner);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerBuiltinFormatters(CoreVM::TypeRegistry& registry)
{
    // Compile-time check: update this when adding new builtin type IDs
    static_assert(CoreVM::BuiltinTypeId::LastBuiltin == 22,
                  "New BuiltinTypeId added — update registerBuiltinFormatters() with a formatter");

    // Helper to set formatFn on a builtin type descriptor
    auto setFormatter = [&registry](uint16_t typeId, CoreVM::TypeFormatFn fn) {
        auto* type = registry.getMutable(typeId);
        if (type)
            type->formatFn = fn;
    };

    setFormatter(CoreVM::BuiltinTypeId::Option, formatOption);
    setFormatter(CoreVM::BuiltinTypeId::Result, formatResult);
    setFormatter(CoreVM::BuiltinTypeId::Tuple2, formatTuple2);
    setFormatter(CoreVM::BuiltinTypeId::Tuple3, formatTuple3);
    setFormatter(CoreVM::BuiltinTypeId::List, formatList);
    setFormatter(CoreVM::BuiltinTypeId::ProcessInfo, formatProduct);
    setFormatter(CoreVM::BuiltinTypeId::FileInfo, formatProduct);
    setFormatter(CoreVM::BuiltinTypeId::JobInfo, formatProduct);
    setFormatter(CoreVM::BuiltinTypeId::DateTime, formatDateTime);
    setFormatter(CoreVM::BuiltinTypeId::Size, formatSize);
    setFormatter(CoreVM::BuiltinTypeId::FileMode, formatFileMode);
    setFormatter(CoreVM::BuiltinTypeId::Markdown, formatMarkdown);
    setFormatter(CoreVM::BuiltinTypeId::TimeSpan, formatTimeSpan);
    setFormatter(CoreVM::BuiltinTypeId::Lazy, formatLazy);
    setFormatter(CoreVM::BuiltinTypeId::Seq, formatSeq);
    setFormatter(CoreVM::BuiltinTypeId::FileHandle, formatFileHandle);
    setFormatter(CoreVM::BuiltinTypeId::Callable, formatCallable);
    setFormatter(CoreVM::BuiltinTypeId::Path, formatProduct);
    setFormatter(CoreVM::BuiltinTypeId::KeyBindingInfo, formatProduct);
    setFormatter(CoreVM::BuiltinTypeId::Json, formatProduct);
    setFormatter(CoreVM::BuiltinTypeId::Process, formatProduct);
    setFormatter(CoreVM::BuiltinTypeId::Ref, formatRef);

    // Runtime assertion: all builtins 1..LastBuiltin must have a formatter
    for (uint16_t id = 1; id <= CoreVM::BuiltinTypeId::LastBuiltin; ++id)
    {
        auto const* type = registry.get(id);
        assert(!type || type->formatFn != nullptr);
    }
}

} // namespace endo::builtins
