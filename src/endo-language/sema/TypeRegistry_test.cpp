// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/TypeRegistry.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace endo;

// ============================================================================
// The frontend's builtin records, derived from CoreVM's descriptors
// ============================================================================
//
// Builtin record layouts used to be declared twice — CoreVM's TypeRegistry for runtime slot access
// and table rendering, the frontend's TypeDefinitionRegistry for type checking — with nothing tying
// them together, so a field added to one and not the other failed only where some program happened
// to touch it (which is how FileInfo::path first showed up, as an "IR generation failed"). The
// frontend now derives its records from CoreVM, so the layouts cannot disagree. What these tests
// guard is the derivation itself: that it covers the records the language exposes, resolves nested
// record types, and does not sweep in runtime-only shapes.

TEST_CASE("TypeRegistry.derived.records_match_corevm_layout")
{
    auto const& runtime = CoreVM::builtinTypes();
    auto frontend = TypeDefinitionRegistry {};
    frontend.registerBuiltins();

    REQUIRE_FALSE(frontend.records().empty());

    for (auto const& [name, frontendType]: frontend.records())
    {
        auto const* runtimeType = runtime.get(frontendType.typeId);
        INFO("record: " << name);
        REQUIRE(runtimeType != nullptr);
        CHECK(runtimeType->name == name);
        REQUIRE(runtimeType->fields.size() == frontendType.fields.size());

        for (auto const& field: frontendType.fields)
        {
            INFO("field: " << name << "." << field.name);
            auto const runtimeField = std::ranges::find_if(
                runtimeType->fields, [&](auto const& f) { return f.name == field.name; });
            REQUIRE(runtimeField != runtimeType->fields.end());
            CHECK(runtimeField->offset == field.offset);
            CHECK(runtimeField->type == field.type);
            // Reachable by the type checker, or field access compiles and reads the wrong slot.
            CHECK(frontendType.fieldTypes.contains(field.name));
            // A field declared past slotCount would be written by a producer, then read out of
            // bounds.
            CHECK(field.offset < runtimeType->slotCount);
        }
    }
}

TEST_CASE("TypeRegistry.derived.covers_the_records_the_language_exposes")
{
    // The set is a list in TypeRegistry.cpp, so a record dropped from it silently loses type
    // checking rather than failing to build. FileInfo carries the hidden fields the ls table and
    // the hyperlink target read, so it is the one worth naming explicitly.
    auto frontend = TypeDefinitionRegistry {};
    frontend.registerBuiltins();

    for (auto const* name: { "ProcessInfo",
                             "DateTime",
                             "Size",
                             "TimeSpan",
                             "FileMode",
                             "FileInfo",
                             "JobInfo",
                             "KeyBindingInfo" })
    {
        INFO("record: " << name);
        CHECK(frontend.lookupRecord(name) != nullptr);
    }

    auto const* fileInfo = frontend.lookupRecord("FileInfo");
    REQUIRE(fileInfo != nullptr);
    for (auto const* field: { "name", "size", "mode", "mtime", "isDir", "isSymlink", "target", "path" })
    {
        INFO("field: FileInfo." << field);
        CHECK(fileInfo->fieldTypes.contains(field));
    }
}

TEST_CASE("TypeRegistry.derived.nested_record_types_resolve")
{
    // An Object-typed field is only useful to the type checker once its nested record id is known:
    // without it, `(ls |> head).size.bytes` cannot be checked. The ids come from the descriptors'
    // nestedTypeName, so a descriptor that omits it loses field access on that field silently.
    auto frontend = TypeDefinitionRegistry {};
    frontend.registerBuiltins();

    auto const nestedId = [&](std::string const& record, std::string const& field) -> uint16_t {
        auto const* info = frontend.lookupRecord(record);
        REQUIRE(info != nullptr);
        auto const it = info->fieldObjectTypeIds.find(field);
        REQUIRE(it != info->fieldObjectTypeIds.end());
        return it->second;
    };

    CHECK(nestedId("FileInfo", "size") == CoreVM::BuiltinTypeId::Size);
    CHECK(nestedId("FileInfo", "mode") == CoreVM::BuiltinTypeId::FileMode);
    CHECK(nestedId("FileInfo", "mtime") == CoreVM::BuiltinTypeId::DateTime);
    CHECK(nestedId("ProcessInfo", "mem") == CoreVM::BuiltinTypeId::Size);
}

TEST_CASE("TypeRegistry.derived.excludes_runtime_only_shapes")
{
    // resolveRecordByFields() matches anonymous record literals against every registered record, so
    // deriving from "every Product type CoreVM knows" would let a literal resolve to Tuple2 or
    // Markdown. These are runtime shapes, not record types users write.
    auto frontend = TypeDefinitionRegistry {};
    frontend.registerBuiltins();

    for (auto const* name: { "Tuple2", "Tuple3", "Markdown", "Json", "Option", "Result", "List" })
    {
        INFO("type: " << name);
        CHECK(frontend.lookupRecord(name) == nullptr);
    }
}
