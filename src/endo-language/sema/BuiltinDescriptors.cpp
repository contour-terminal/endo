// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/BuiltinDescriptors.hpp>

#include <CoreVM/types/TypeDescriptor.hpp>

#include <format>

namespace endo
{

namespace
{
    using LT = CoreVM::LiteralType;
    namespace BT = CoreVM::BuiltinTypeId;

    /// Helper to create a fixed-arity builtin descriptor.
    constexpr BuiltinCallDescriptor fixedArity(std::string_view name, size_t arity)
    {
        return { .name = name, .minArity = arity, .maxArity = arity };
    }

    /// Helper to create a variable-arity builtin descriptor.
    constexpr BuiltinCallDescriptor varArity(std::string_view name, size_t minArity, size_t maxArity)
    {
        return { .name = name, .minArity = minArity, .maxArity = maxArity };
    }
} // namespace

BuiltinDescriptorRegistry::BuiltinDescriptorRegistry()
{
    // --- IR instruction builtins (no native callback) ---
    registerCall(fixedArity("string_length", 1));
    registerCall(fixedArity("int_of_string", 1));
    registerCall(fixedArity("string_of_int", 1));
    registerCall(fixedArity("string", 1));
    registerCall(fixedArity("not", 1));

    // --- Environment/system builtins ---
    {
        auto desc = fixedArity("env", 1);
        desc.returnObjectTypeId = BT::Option;
        desc.returnInnerType = LT::String;
        registerCall(desc);
    }
    {
        auto desc = fixedArity("which", 1);
        desc.returnObjectTypeId = BT::Option;
        desc.returnInnerType = LT::String;
        registerCall(desc);
    }

    // --- List builtins ---
    {
        auto desc = fixedArity("head", 1);
        desc.returnObjectTypeId = BT::Option;
        desc.propagatesListElementAsInnerObj = true;
        registerCall(desc);
    }
    {
        auto desc = fixedArity("tail", 1);
        desc.returnObjectTypeId = BT::List;
        desc.propagatesListElementType = true;
        registerCall(desc);
    }
    registerCall(fixedArity("length", 1));
    registerCall(fixedArity("isEmpty", 1));
    {
        auto desc = fixedArity("nth", 2);
        desc.returnObjectTypeId = BT::Option;
        desc.propagatesListElementAsInnerObj = true;
        registerCall(desc);
    }
    {
        auto desc = fixedArity("last", 1);
        desc.returnObjectTypeId = BT::Option;
        desc.propagatesListElementAsInnerObj = true;
        registerCall(desc);
    }
    {
        auto desc = fixedArity("replicate", 2);
        desc.returnObjectTypeId = BT::List;
        registerCall(desc);
    }

    // --- String builtins ---
    registerCall(fixedArity("trim", 1));
    registerCall(fixedArity("toLower", 1));
    registerCall(fixedArity("toUpper", 1));
    registerCall(fixedArity("contains", 2));
    registerCall(fixedArity("startsWith", 2));
    registerCall(fixedArity("endsWith", 2));
    registerCall(fixedArity("replace", 3));
    {
        auto desc = fixedArity("split", 2);
        desc.returnObjectTypeId = BT::List;
        desc.returnListElemType = LT::String;
        registerCall(desc);
    }
    registerCall(fixedArity("join", 2));
    registerCall(fixedArity("toText", 1));

    // --- Structured data builtins ---
    {
        auto desc = fixedArity("ps", 0);
        desc.returnObjectTypeId = BT::List;
        desc.returnListElementTypeId = BT::ProcessInfo;
        registerCall(desc);
    }
    {
        auto desc = fixedArity("jobs", 0);
        desc.returnObjectTypeId = BT::List;
        desc.returnListElementTypeId = BT::JobInfo;
        registerCall(desc);
    }
    {
        auto desc = varArity("ls", 0, 1);
        desc.returnObjectTypeId = BT::List;
        desc.returnListElementTypeId = BT::FileInfo;
        registerCall(desc);
    }
    {
        auto desc = fixedArity("bind", 0);
        desc.returnObjectTypeId = BT::List;
        desc.returnListElementTypeId = BT::KeyBindingInfo;
        registerCall(desc);
    }

    // --- Formatting builtins ---
    registerCall(fixedArity("formatDateTime", 1));
    registerCall(fixedArity("formatTimeSpan", 1));
    registerCall(fixedArity("sleep", 1));
    registerCall(fixedArity("formatMode", 1));
    registerCall(varArity("formatNumber", 1, 2));
    registerCall(fixedArity("isReadable", 1));
    registerCall(fixedArity("isWritable", 1));
    registerCall(fixedArity("isExecutable", 1));

    // --- Network/IO builtins ---
    {
        auto desc = varArity("fetch", 1, 2);
        desc.returnObjectTypeId = BT::Result;
        desc.returnInnerType = LT::String;
        registerCall(desc);
    }
    {
        auto desc = fixedArity("markdown", 1);
        desc.returnObjectTypeId = BT::Markdown;
        registerCall(desc);
    }

    // --- Misc builtins ---
    registerCall(varArity("rand", 0, 2));
    registerCall(fixedArity("time", 1));
    registerCall(fixedArity("register_completer", 2));

    // --- Property descriptors ---

    // String properties
    registerProperty({ .typeId = 0, .fieldName = "length" }); // typeId 0 = String (special)

    // List properties
    {
        BuiltinPropertyDescriptor desc { .typeId = BT::List, .fieldName = "length" };
        registerProperty(desc);
    }
    {
        BuiltinPropertyDescriptor desc { .typeId = BT::List, .fieldName = "head" };
        desc.returnObjectTypeId = BT::Option;
        desc.propagatesListElementAsInnerObj = true;
        registerProperty(desc);
    }
    {
        BuiltinPropertyDescriptor desc { .typeId = BT::List, .fieldName = "tail" };
        desc.returnObjectTypeId = BT::List;
        desc.propagatesListElementType = true;
        registerProperty(desc);
    }
    {
        BuiltinPropertyDescriptor desc { .typeId = BT::List, .fieldName = "isEmpty" };
        registerProperty(desc);
    }
    {
        BuiltinPropertyDescriptor desc { .typeId = BT::List, .fieldName = "last" };
        desc.returnObjectTypeId = BT::Option;
        desc.propagatesListElementAsInnerObj = true;
        registerProperty(desc);
    }
    {
        BuiltinPropertyDescriptor desc { .typeId = BT::List, .fieldName = "reverse" };
        desc.returnObjectTypeId = BT::List;
        desc.propagatesListElementType = true;
        registerProperty(desc);
    }

    // Option properties
    registerProperty({ .typeId = BT::Option, .fieldName = "isSome" });
    registerProperty({ .typeId = BT::Option, .fieldName = "isNone" });

    // Ref properties
    registerProperty({ .typeId = BT::Ref, .fieldName = "value" });

    // Result properties
    registerProperty({ .typeId = BT::Result, .fieldName = "isOk" });
    registerProperty({ .typeId = BT::Result, .fieldName = "isError" });

    // Tuple properties
    registerProperty({ .typeId = BT::Tuple2, .fieldName = "fst" });
    registerProperty({ .typeId = BT::Tuple2, .fieldName = "snd" });
    registerProperty({ .typeId = BT::Tuple3, .fieldName = "fst" });
    registerProperty({ .typeId = BT::Tuple3, .fieldName = "snd" });
    registerProperty({ .typeId = BT::Tuple3, .fieldName = "third" });

    // FileMode properties
    registerProperty({ .typeId = BT::FileMode, .fieldName = "isReadable" });
    registerProperty({ .typeId = BT::FileMode, .fieldName = "isWritable" });
    registerProperty({ .typeId = BT::FileMode, .fieldName = "isExecutable" });
}

void BuiltinDescriptorRegistry::registerCall(BuiltinCallDescriptor desc)
{
    _calls.emplace(desc.name, desc);
}

void BuiltinDescriptorRegistry::registerProperty(BuiltinPropertyDescriptor desc)
{
    auto key = std::format("{}:{}", desc.typeId, desc.fieldName);
    _properties.emplace(std::move(key), desc);
}

BuiltinCallDescriptor const* BuiltinDescriptorRegistry::lookupCall(std::string_view name) const
{
    if (auto it = _calls.find(name); it != _calls.end())
        return &it->second;
    return nullptr;
}

BuiltinPropertyDescriptor const* BuiltinDescriptorRegistry::lookupProperty(uint16_t typeId,
                                                                           std::string_view field) const
{
    auto key = std::format("{}:{}", typeId, field);
    if (auto it = _properties.find(key); it != _properties.end())
        return &it->second;
    return nullptr;
}

} // namespace endo
