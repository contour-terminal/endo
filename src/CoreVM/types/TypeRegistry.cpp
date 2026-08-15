// SPDX-License-Identifier: Apache-2.0
#include "TypeRegistry.hpp"

#include <algorithm>
#include <cassert>

namespace CoreVM
{

TypeRegistry::TypeRegistry()
{
    registerBuiltins();
}

void TypeRegistry::registerBuiltins()
{
    // Option<T>: None (tag=0, 0 payload slots) | Some (tag=1, 1 payload slot)
    auto optionType = std::make_unique<TypeDescriptor>();
    optionType->kind = TypeKind::Sum;
    optionType->id = BuiltinTypeId::Option;
    optionType->name = "Option";
    optionType->slotCount = 2; // 1 payload slot + 1 type tag slot
    optionType->variants = {
        { .name = "None", .payloadSlots = 0 }, // tag 0: no payload
        { .name = "Some", .payloadSlots = 1 }, // tag 1: 1 slot payload
    };
    optionType->moduleFunctions = {
        { .name = "map", .signature = "Option.map f opt -> option" },
        { .name = "bind", .signature = "Option.bind f opt -> option" },
        { .name = "defaultValue", .signature = "Option.defaultValue d opt -> value" },
    };
    // SlotTraceInfo: None has no slots; Some's slot 0 is dynamic (check type tag in slot 1)
    optionType->traceInfo.variantFixedSlots = { {}, {} };
    optionType->traceInfo.variantDynamicSlots = {
        {}, // None: no payload
        { SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 1, .tagPosition = 0 } }, // Some
    };
    addType(std::move(optionType));

    // Result<T,E>: Error (tag=0, 1 payload slot) | Ok (tag=1, 1 payload slot)
    auto resultType = std::make_unique<TypeDescriptor>();
    resultType->kind = TypeKind::Sum;
    resultType->id = BuiltinTypeId::Result;
    resultType->name = "Result";
    resultType->slotCount = 2; // 1 payload slot + 1 type tag slot
    resultType->variants = {
        { .name = "Error", .payloadSlots = 1 }, // tag 0: error payload
        { .name = "Ok", .payloadSlots = 1 },    // tag 1: success payload
    };
    // SlotTraceInfo: Error/Ok slot 0 is dynamic (check type tag in slot 1)
    resultType->traceInfo.variantFixedSlots = { {}, {} };
    resultType->traceInfo.variantDynamicSlots = {
        { SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 1, .tagPosition = 0 } }, // Error
        { SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 1, .tagPosition = 0 } }, // Ok
    };
    addType(std::move(resultType));

    // Tuple2: 2-element product type
    auto tuple2Type = std::make_unique<TypeDescriptor>();
    tuple2Type->kind = TypeKind::Product;
    tuple2Type->id = BuiltinTypeId::Tuple2;
    tuple2Type->name = "Tuple2";
    tuple2Type->fields = {
        { .name = "", .offset = 0 }, // slot 0 (unnamed positional)
        { .name = "", .offset = 1 }, // slot 1
    };
    tuple2Type->slotCount = 3; // 2 element slots + 1 packed type tag slot
    // SlotTraceInfo: slots 0,1 are dynamic (check packed type tag in slot 2)
    tuple2Type->traceInfo.dynamicSlots = {
        SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 2, .tagPosition = 0 },
        SlotTraceInfo::DynamicSlot { .slotIndex = 1, .typeTagSlot = 2, .tagPosition = 1 },
    };
    addType(std::move(tuple2Type));

    // Tuple3: 3-element product type
    auto tuple3Type = std::make_unique<TypeDescriptor>();
    tuple3Type->kind = TypeKind::Product;
    tuple3Type->id = BuiltinTypeId::Tuple3;
    tuple3Type->name = "Tuple3";
    tuple3Type->fields = {
        { .name = "", .offset = 0 }, // slot 0
        { .name = "", .offset = 1 }, // slot 1
        { .name = "", .offset = 2 }, // slot 2
    };
    tuple3Type->slotCount = 4; // 3 element slots + 1 packed type tag slot
    // SlotTraceInfo: slots 0,1,2 are dynamic (check packed type tag in slot 3)
    tuple3Type->traceInfo.dynamicSlots = {
        SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 3, .tagPosition = 0 },
        SlotTraceInfo::DynamicSlot { .slotIndex = 1, .typeTagSlot = 3, .tagPosition = 1 },
        SlotTraceInfo::DynamicSlot { .slotIndex = 2, .typeTagSlot = 3, .tagPosition = 2 },
    };
    addType(std::move(tuple3Type));

    // List: Nil (tag=0, 0 payload slots) | Cons (tag=1, 2 slots: head + tail)
    auto listType = std::make_unique<TypeDescriptor>();
    listType->kind = TypeKind::Sum;
    listType->id = BuiltinTypeId::List;
    listType->name = "List";
    listType->slotCount = 3; // 2 payload slots (head + tail) + 1 type tag slot
    listType->variants = {
        { .name = "Nil", .payloadSlots = 0 },  // tag 0: empty list
        { .name = "Cons", .payloadSlots = 2 }, // tag 1: head (slot 0) + tail (slot 1)
    };
    // SlotTraceInfo: Cons slot 1 (tail) is always an object; slot 0 (head) is dynamic
    listType->traceInfo.variantFixedSlots = { {}, { 1 } }; // Nil={}, Cons={slot 1}
    listType->traceInfo.variantDynamicSlots = {
        {},                                                                                    // Nil
        { SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 2, .tagPosition = 0 } }, // Cons head
    };
    addType(std::move(listType));

    // ProcessInfo: Product type with 6 fields for process information
    auto processInfoType = std::make_unique<TypeDescriptor>();
    processInfoType->kind = TypeKind::Product;
    processInfoType->id = BuiltinTypeId::ProcessInfo;
    processInfoType->name = "ProcessInfo";
    processInfoType->slotCount = 6;
    processInfoType->fields = {
        { .name = "pid", .offset = 0, .type = LiteralType::Number },
        { .name = "ppid", .offset = 1, .type = LiteralType::Number },
        { .name = "user", .offset = 2, .type = LiteralType::String },
        { .name = "cpu", .offset = 3, .type = LiteralType::Float },
        { .name = "mem", .offset = 4, .type = LiteralType::Object, .nestedTypeName = "Size" },
        { .name = "command", .offset = 5, .type = LiteralType::String },
    };
    processInfoType->languageRecord = true;
    processInfoType->producingCommand = "ps";
    // SlotTraceInfo: slot 4 (mem) is Object (Size)
    processInfoType->traceInfo.fixedObjectSlots = { 4 };
    addType(std::move(processInfoType));

    // DateTime: Product type with 7 fields for date/time representation
    auto dateTimeType = std::make_unique<TypeDescriptor>();
    dateTimeType->kind = TypeKind::Product;
    dateTimeType->id = BuiltinTypeId::DateTime;
    dateTimeType->name = "DateTime";
    dateTimeType->slotCount = 7;
    dateTimeType->fields = {
        { .name = "year", .offset = 0, .type = LiteralType::Number },
        { .name = "month", .offset = 1, .type = LiteralType::Number },
        { .name = "day", .offset = 2, .type = LiteralType::Number },
        { .name = "hour", .offset = 3, .type = LiteralType::Number },
        { .name = "minute", .offset = 4, .type = LiteralType::Number },
        { .name = "second", .offset = 5, .type = LiteralType::Number },
        { .name = "epoch", .offset = 6, .type = LiteralType::Number },
    };
    dateTimeType->languageRecord = true;
    dateTimeType->moduleFunctions = {
        { .name = "now", .signature = "DateTime.now -> DateTime (current UTC time)" },
        { .name = "fromEpoch", .signature = "DateTime.fromEpoch epoch -> DateTime" },
    };
    addType(std::move(dateTimeType));

    // FileInfo: Product type with 5 fields for file/directory information
    auto fileInfoType = std::make_unique<TypeDescriptor>();
    fileInfoType->kind = TypeKind::Product;
    fileInfoType->id = BuiltinTypeId::FileInfo;
    fileInfoType->name = "FileInfo";
    fileInfoType->slotCount = 8;
    fileInfoType->fields = {
        { .name = "name", .offset = 0, .type = LiteralType::String },
        { .name = "size", .offset = 1, .type = LiteralType::Object, .nestedTypeName = "Size" },
        { .name = "mode", .offset = 2, .type = LiteralType::Object, .nestedTypeName = "FileMode" },
        { .name = "mtime", .offset = 3, .type = LiteralType::Object, .nestedTypeName = "DateTime" },
        // isDir/isSymlink/target drive presentation (folder slash, symlink icon, "name ->
        // target" suffix) and are hidden from the default table, but remain field-accessible.
        { .name = "isDir", .offset = 4, .type = LiteralType::Boolean, .display = false },
        { .name = "isSymlink", .offset = 5, .type = LiteralType::Boolean, .display = false },
        { .name = "target", .offset = 6, .type = LiteralType::String, .display = false },
        // The entry's absolute path. `name` is only a basename, so anything that must address
        // the file rather than display it (OSC 8 hyperlinks, passing an entry to another
        // command) needs this. Hidden from the default table so `ls` output is unchanged.
        { .name = "path", .offset = 7, .type = LiteralType::String, .display = false },
    };
    fileInfoType->languageRecord = true;
    fileInfoType->producingCommand = "ls";
    // SlotTraceInfo: slots 1 (Size), 2 (FileMode), 3 (DateTime) are always objects.
    // Slots 0 (name), 6 (target) and 7 (path) are Strings: CoreString lives in the Runner's
    // separate string arena (not the GC object pool), so String slots are intentionally
    // NOT traced and must stay out of fixedObjectSlots.
    fileInfoType->traceInfo.fixedObjectSlots = { 1, 2, 3 };
    addType(std::move(fileInfoType));

    // JobInfo: Product type with 4 fields for background job information
    auto jobInfoType = std::make_unique<TypeDescriptor>();
    jobInfoType->kind = TypeKind::Product;
    jobInfoType->id = BuiltinTypeId::JobInfo;
    jobInfoType->name = "JobInfo";
    jobInfoType->slotCount = 4;
    jobInfoType->fields = {
        { .name = "id", .offset = 0, .type = LiteralType::Number },
        { .name = "state", .offset = 1, .type = LiteralType::String },
        { .name = "command", .offset = 2, .type = LiteralType::String },
        { .name = "pid", .offset = 3, .type = LiteralType::Number },
    };
    jobInfoType->languageRecord = true;
    jobInfoType->producingCommand = "jobs";
    addType(std::move(jobInfoType));

    // KeyBindingInfo: Product type with 2 fields for key binding information
    auto keyBindingInfoType = std::make_unique<TypeDescriptor>();
    keyBindingInfoType->kind = TypeKind::Product;
    keyBindingInfoType->id = BuiltinTypeId::KeyBindingInfo;
    keyBindingInfoType->name = "KeyBindingInfo";
    keyBindingInfoType->slotCount = 2;
    keyBindingInfoType->fields = {
        { .name = "key", .offset = 0, .type = LiteralType::String },
        { .name = "action", .offset = 1, .type = LiteralType::String },
    };
    keyBindingInfoType->languageRecord = true;
    keyBindingInfoType->producingCommand = "bind";
    addType(std::move(keyBindingInfoType));

    // Size: Product type with 1 field for byte count
    auto sizeType = std::make_unique<TypeDescriptor>();
    sizeType->kind = TypeKind::Product;
    sizeType->id = BuiltinTypeId::Size;
    sizeType->name = "Size";
    sizeType->slotCount = 1;
    sizeType->fields = {
        { .name = "bytes", .offset = 0, .type = LiteralType::Number },
    };
    sizeType->languageRecord = true;
    sizeType->moduleFunctions = {
        { .name = "fromBytes", .signature = "Size.fromBytes n -> Size" },
        { .name = "fromKB", .signature = "Size.fromKB n -> Size (n * 1024 bytes)" },
        { .name = "fromMB", .signature = "Size.fromMB n -> Size (n * 1024² bytes)" },
        { .name = "fromGB", .signature = "Size.fromGB n -> Size (n * 1024³ bytes)" },
        { .name = "fromTB", .signature = "Size.fromTB n -> Size (n * 1024⁴ bytes)" },
    };
    addType(std::move(sizeType));

    // FileMode: Product type with 1 field for raw Unix permission bits
    auto fileModeType = std::make_unique<TypeDescriptor>();
    fileModeType->kind = TypeKind::Product;
    fileModeType->id = BuiltinTypeId::FileMode;
    fileModeType->name = "FileMode";
    fileModeType->slotCount = 1;
    fileModeType->fields = {
        { .name = "bits", .offset = 0, .type = LiteralType::Number },
    };
    fileModeType->languageRecord = true;
    fileModeType->moduleFunctions = {
        { .name = "fromBits", .signature = "FileMode.fromBits n -> FileMode" },
    };
    addType(std::move(fileModeType));

    // Markdown: Product type with 1 field for raw markdown content string
    auto markdownType = std::make_unique<TypeDescriptor>();
    markdownType->kind = TypeKind::Product;
    markdownType->id = BuiltinTypeId::Markdown;
    markdownType->name = "Markdown";
    markdownType->slotCount = 1;
    markdownType->fields = {
        { .name = "content", .offset = 0, .type = LiteralType::String },
    };
    markdownType->moduleFunctions = {
        { .name = "render", .signature = "Markdown.render md -> unit (renders to terminal)" },
        { .name = "toHtml", .signature = "Markdown.toHtml md -> string (converts to HTML)" },
        { .name = "toText", .signature = "Markdown.toText md -> string (strips formatting)" },
    };
    addType(std::move(markdownType));

    // TimeSpan: Product type with 1 field for duration in milliseconds
    auto timeSpanType = std::make_unique<TypeDescriptor>();
    timeSpanType->kind = TypeKind::Product;
    timeSpanType->id = BuiltinTypeId::TimeSpan;
    timeSpanType->name = "TimeSpan";
    timeSpanType->slotCount = 1;
    timeSpanType->fields = {
        { .name = "milliseconds", .offset = 0, .type = LiteralType::Number },
    };
    timeSpanType->languageRecord = true;
    timeSpanType->moduleFunctions = {
        { .name = "fromMilliseconds", .signature = "TimeSpan.fromMilliseconds n -> TimeSpan" },
        { .name = "fromSeconds", .signature = "TimeSpan.fromSeconds n -> TimeSpan (n * 1000 ms)" },
        { .name = "fromMinutes", .signature = "TimeSpan.fromMinutes n -> TimeSpan (n * 60000 ms)" },
        { .name = "fromHours", .signature = "TimeSpan.fromHours n -> TimeSpan (n * 3600000 ms)" },
        { .name = "fromDays", .signature = "TimeSpan.fromDays n -> TimeSpan (n * 86400000 ms)" },
    };
    addType(std::move(timeSpanType));

    // Json: Stateless module providing JSON query functions (no instance fields)
    auto jsonType = std::make_unique<TypeDescriptor>();
    jsonType->kind = TypeKind::Product;
    jsonType->id = BuiltinTypeId::Json;
    jsonType->name = "Json";
    jsonType->slotCount = 0;
    jsonType->moduleFunctions = {
        { .name = "query", .signature = "Json.query path json -> list<string>" },
    };
    addType(std::move(jsonType));

    // Process: Stateless module providing process signal functions (no instance fields)
    auto processType = std::make_unique<TypeDescriptor>();
    processType->kind = TypeKind::Product;
    processType->id = BuiltinTypeId::Process;
    processType->name = "Process";
    processType->slotCount = 0;
    processType->moduleFunctions = {
        { .name = "kill", .signature = "Process.kill pid -> result<unit, str>" },
        { .name = "signal", .signature = "Process.signal signum pid -> result<unit, str>" },
    };
    addType(std::move(processType));

    // Lazy<T>: Unevaluated (tag=0, N+2 slots: funcId + cached + captures) | Evaluated (tag=1, cached value)
    // Note: slotCount is set to 2 as base (funcId + cached result); captures vary per lazy expression
    // and use per-expression type descriptors created during IRGenerator codegen.
    auto lazyType = std::make_unique<TypeDescriptor>();
    lazyType->kind = TypeKind::Sum;
    lazyType->id = BuiltinTypeId::Lazy;
    lazyType->name = "Lazy";
    lazyType->slotCount = 2; // base: slot 0 = funcId, slot 1 = cached result
    lazyType->variants = {
        { .name = "Unevaluated", .payloadSlots = 2 }, // tag 0: thunk (funcId + cached placeholder)
        { .name = "Evaluated", .payloadSlots = 1 },   // tag 1: cached result in slot 1
    };
    addType(std::move(lazyType));

    // Seq<T>: Empty (tag=0, 0 payload slots) | Cons (tag=1, 2 slots: head + lazyTail)
    auto seqType = std::make_unique<TypeDescriptor>();
    seqType->kind = TypeKind::Sum;
    seqType->id = BuiltinTypeId::Seq;
    seqType->name = "Seq";
    seqType->slotCount = 2; // head (slot 0) + lazy tail (slot 1)
    seqType->variants = {
        { .name = "Empty", .payloadSlots = 0 }, // tag 0: empty sequence
        { .name = "Cons", .payloadSlots = 2 },  // tag 1: head (slot 0) + lazy tail (slot 1)
    };
    // SlotTraceInfo: Cons slot 1 (lazy tail) is always an object
    seqType->traceInfo.variantFixedSlots = { {}, { 1 } }; // Empty={}, Cons={slot 1}
    seqType->traceInfo.variantDynamicSlots = { {}, {} };  // head type unknown at trace level
    addType(std::move(seqType));

    // FileHandle: Product type with 1 field for the handle index
    auto fileHandleType = std::make_unique<TypeDescriptor>();
    fileHandleType->kind = TypeKind::Product;
    fileHandleType->id = BuiltinTypeId::FileHandle;
    fileHandleType->name = "FileHandle";
    fileHandleType->slotCount = 1;
    fileHandleType->fields = {
        { .name = "handle", .offset = 0, .type = LiteralType::Number },
    };
    fileHandleType->disposeCallbackName = "file_close";
    fileHandleType->moduleFunctions = {
        { .name = "open", .signature = "File.open path mode -> result<FileHandle, str>" },
        { .name = "close", .signature = "File.close fd -> unit" },
        { .name = "readLine", .signature = "File.readLine fd -> option<str>" },
        { .name = "readAll", .signature = "File.readAll path -> result<str, str>" },
        { .name = "writeAll", .signature = "File.writeAll path content -> result<unit, str>" },
        { .name = "appendAll", .signature = "File.appendAll path content -> result<unit, str>" },
        { .name = "size", .signature = "File.size path -> result<int, str>" },
        { .name = "exists", .signature = "File.exists path -> bool" },
        { .name = "delete", .signature = "File.delete path -> result<unit, str>" },
        { .name = "lines", .signature = "File.lines fd -> seq<str>" },
    };
    addType(std::move(fileHandleType));

    // Callable: Product type for indirect function calls (funcId + captures)
    // Base type with 1 slot (funcId only, zero captures). Per-site types with
    // captures are registered dynamically via allocateCustomTypeId().
    auto callableType = std::make_unique<TypeDescriptor>();
    callableType->kind = TypeKind::Product;
    callableType->id = BuiltinTypeId::Callable;
    callableType->name = "Callable";
    callableType->slotCount = 1; // slot 0 = function ID (zero-capture base)
    callableType->fields = {
        { .name = "funcId", .offset = 0, .type = LiteralType::Number },
    };
    addType(std::move(callableType));

    // Path: Stateless module providing filesystem path utilities (no instance fields)
    auto pathType = std::make_unique<TypeDescriptor>();
    pathType->kind = TypeKind::Product;
    pathType->id = BuiltinTypeId::Path;
    pathType->name = "Path";
    pathType->slotCount = 0;
    pathType->moduleFunctions = {
        { .name = "temporary_directory", .signature = "Path.temporary_directory -> str" },
    };
    addType(std::move(pathType));

    // Ref<T>: Mutable reference cell — product type with 1 value slot + 1 type tag slot for GC.
    // Dereference via `.value` dot property, mutate via `r <- newval`.
    auto refType = std::make_unique<TypeDescriptor>();
    refType->kind = TypeKind::Product;
    refType->id = BuiltinTypeId::Ref;
    refType->name = "Ref";
    refType->slotCount = 2; // slot 0 = inner value, slot 1 = type tag (LiteralType)
    refType->fields = {
        { .name = "value", .offset = 0, .type = LiteralType::Void }, // user-facing dot property: r.value
        { .name = "",
          .offset = 1,
          .type = LiteralType::Number }, // type tag for GC and formatting (internal, hidden from completion)
    };
    refType->hasMutableSlots = true;
    refType->traceInfo.dynamicSlots = {
        SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 1, .tagPosition = 0 },
    };
    addType(std::move(refType));

    // CompletionEntry: Entry (tag=0) | Described (tag=1) | Detailed (tag=2)
    // Used by scripted completers to attach descriptions/detail to completion candidates.
    // Slots: 0=text, 1=description, 2=detail (all String, traced as fixed object slots).
    auto completionEntryType = std::make_unique<TypeDescriptor>();
    completionEntryType->kind = TypeKind::Sum;
    completionEntryType->id = BuiltinTypeId::CompletionEntry;
    completionEntryType->name = "Completion";
    completionEntryType->slotCount = 3; // 3 payload slots (no type tag needed — all String)
    completionEntryType->variants = {
        { .name = "Entry", .payloadSlots = 1 },     // tag 0: text only
        { .name = "Described", .payloadSlots = 2 }, // tag 1: text + description
        { .name = "Detailed", .payloadSlots = 3 },  // tag 2: text + description + detail
    };
    completionEntryType->moduleFunctions = {
        { .name = "register",
          .signature = "str -> str -> unit — register a completion function for a command" },
        { .name = "entry", .signature = "str -> Completion — plain text completion" },
        { .name = "described", .signature = "str -> str -> Completion — text with short description" },
        { .name = "detailed",
          .signature = "str -> str -> str -> Completion — text, description, and markdown detail" },
        { .name = "text", .signature = "Completion -> str — extract text from a completion entry" },
    };
    // SlotTraceInfo: all payload slots are string pointers (traced as fixed object slots).
    // Entry uses only slot 0, Described uses 0-1, Detailed uses 0-2.
    // Unused slots are zero-initialized and safe to trace (null is not a valid object).
    completionEntryType->traceInfo.variantFixedSlots = {
        { 0 },       // Entry: slot 0
        { 0, 1 },    // Described: slots 0-1
        { 0, 1, 2 }, // Detailed: slots 0-2
    };
    completionEntryType->traceInfo.variantDynamicSlots = { {}, {}, {} };
    addType(std::move(completionEntryType));

    // Update _nextId to be after the builtin type IDs
    _nextId = std::max(_nextId, static_cast<uint16_t>(BuiltinTypeId::LastBuiltin + 1));
}

/// Computes traceInfo for a product type from its FieldInfo vector.
static void computeProductTraceInfo(TypeDescriptor& type)
{
    for (size_t i = 0; i < type.fields.size(); ++i)
    {
        if (type.fields[i].type == LiteralType::Object)
            type.traceInfo.fixedObjectSlots.push_back(static_cast<uint8_t>(i));
    }
}

/// Computes traceInfo for a sum type from its VariantInfo vector.
static void computeSumTraceInfo(TypeDescriptor& type)
{
    type.traceInfo.variantFixedSlots.resize(type.variants.size());
    type.traceInfo.variantDynamicSlots.resize(type.variants.size());
    for (size_t v = 0; v < type.variants.size(); ++v)
    {
        for (size_t f = 0; f < type.variants[v].fields.size(); ++f)
        {
            if (type.variants[v].fields[f].type == LiteralType::Object)
                type.traceInfo.variantFixedSlots[v].push_back(static_cast<uint8_t>(f));
        }
    }
}

TypeDescriptor* TypeRegistry::registerSumType(std::string name, std::vector<VariantInfo> variants)
{
    auto type = std::make_unique<TypeDescriptor>();
    type->kind = TypeKind::Sum;
    type->id = _nextId++;
    type->name = std::move(name);
    type->variants = std::move(variants);

    // Calculate slot count as maximum of all variant payload sizes
    type->slotCount = 0;
    for (const auto& variant: type->variants)
    {
        type->slotCount = std::max(type->slotCount, static_cast<uint16_t>(variant.payloadSlots));
    }

    computeSumTraceInfo(*type);

    return addType(std::move(type));
}

TypeDescriptor* TypeRegistry::registerProductType(std::string name, std::vector<FieldInfo> fields)
{
    auto type = std::make_unique<TypeDescriptor>();
    type->kind = TypeKind::Product;
    type->id = _nextId++;
    type->name = std::move(name);
    type->fields = std::move(fields);

    // Slot count is the number of fields (each field is one slot)
    type->slotCount = static_cast<uint16_t>(type->fields.size());

    // Assign offsets if not already set
    for (size_t i = 0; i < type->fields.size(); ++i)
    {
        type->fields[i].offset = static_cast<uint8_t>(i);
    }

    computeProductTraceInfo(*type);

    return addType(std::move(type));
}

TypeDescriptor* TypeRegistry::registerProductType(std::unique_ptr<TypeDescriptor> type)
{
    // Ensure the type has the correct kind and consistent offsets
    assert(type->kind == TypeKind::Product);
    for (size_t i = 0; i < type->fields.size(); ++i)
        type->fields[i].offset = static_cast<uint8_t>(i);
    // Preserve slotCount if already set (e.g., named tuples need extra slots for type tags)
    if (type->slotCount == 0)
        type->slotCount = static_cast<uint16_t>(type->fields.size());

    // Compute traceInfo if not already populated
    if (type->traceInfo.fixedObjectSlots.empty() && type->traceInfo.dynamicSlots.empty())
        computeProductTraceInfo(*type);

    // Update _nextId to stay ahead of the assigned ID
    if (type->id >= _nextId)
        _nextId = type->id + 1;

    return addType(std::move(type));
}

TypeDescriptor* TypeRegistry::registerSumType(std::unique_ptr<TypeDescriptor> type)
{
    assert(type->kind == TypeKind::Sum);

    // Calculate slotCount as max of variant payload sizes
    uint16_t maxSlots = 0;
    for (auto const& v: type->variants)
        maxSlots = std::max(maxSlots, static_cast<uint16_t>(v.payloadSlots));
    type->slotCount = maxSlots;

    // Compute traceInfo if not already populated
    if (type->traceInfo.variantFixedSlots.empty() && type->traceInfo.variantDynamicSlots.empty())
        computeSumTraceInfo(*type);

    // Update _nextId to stay ahead of the assigned ID
    if (type->id >= _nextId)
        _nextId = type->id + 1;

    return addType(std::move(type));
}

TypeDescriptor* TypeRegistry::registerFunctionType(std::string name, uint16_t captureCount)
{
    auto type = std::make_unique<TypeDescriptor>();
    type->kind = TypeKind::Function;
    type->id = _nextId++;
    type->name = std::move(name);
    type->captureCount = captureCount;
    type->slotCount = captureCount; // Each capture is one slot

    return addType(std::move(type));
}

const TypeDescriptor* TypeRegistry::get(uint16_t id) const
{
    // Types are stored with their ID as index (offset by 1 since ID 0 is reserved)
    // But we may have sparse IDs, so search linearly for now
    // TODO: Optimize with direct indexing if IDs are always sequential
    for (const auto& type: _types)
    {
        if (type->id == id)
            return type.get();
    }
    return nullptr;
}

TypeDescriptor* TypeRegistry::getMutable(uint16_t id)
{
    for (auto& type: _types)
    {
        if (type->id == id)
            return type.get();
    }
    return nullptr;
}

const TypeDescriptor* TypeRegistry::getByName(std::string_view name) const
{
    auto it = _nameToId.find(std::string(name));
    if (it != _nameToId.end())
        return get(it->second);
    return nullptr;
}

TypeRegistry const& builtinTypes()
{
    static TypeRegistry const registry;
    return registry;
}

TypeDescriptor* TypeRegistry::addType(std::unique_ptr<TypeDescriptor> type)
{
    assert(type != nullptr);
    assert(!type->name.empty());

    // Ensure ID is unique
    assert(get(type->id) == nullptr && "Type ID already registered");

    // Ensure name is unique
    assert(_nameToId.find(type->name) == _nameToId.end() && "Type name already registered");

    TypeDescriptor* ptr = type.get();
    _nameToId[type->name] = type->id;
    _types.push_back(std::move(type));
    return ptr;
}

} // namespace CoreVM
