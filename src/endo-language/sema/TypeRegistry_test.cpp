// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/TypeRegistry.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace endo;

// ============================================================================
// Parity between the two registries that describe the same builtin records
// ============================================================================
//
// Builtin record layouts are declared twice: CoreVM's TypeRegistry drives runtime slot access and
// table rendering, and the frontend's TypeDefinitionRegistry drives type checking. Nothing ties
// them together, so a field added to one and not the other fails only at the point some program
// happens to touch it — which is how adding FileInfo::path first showed up, as an "IR generation
// failed" on a test that used the field. These tests turn that into a build failure instead.

TEST_CASE("TypeRegistry.parity.record_fields_match_corevm")
{
    auto runtime = CoreVM::TypeRegistry {};
    auto frontend = TypeDefinitionRegistry {};
    frontend.registerBuiltins();

    for (auto const& [name, frontendType]: frontend.records())
    {
        auto const* runtimeType = runtime.get(frontendType.typeId);
        INFO("record: " << name);
        REQUIRE(runtimeType != nullptr);
        CHECK(runtimeType->name == name);

        // Every field the frontend knows must exist at the same slot in the runtime layout,
        // otherwise field access compiles and then reads the wrong slot.
        for (auto const& field: frontendType.fields)
        {
            INFO("field: " << name << "." << field.name);
            auto const runtimeField = std::ranges::find_if(
                runtimeType->fields, [&](auto const& f) { return f.name == field.name; });
            REQUIRE(runtimeField != runtimeType->fields.end());
            CHECK(runtimeField->offset == field.offset);
            CHECK(runtimeField->type == field.type);
        }

        // And the converse: a field only the runtime knows is invisible to the type checker, so
        // programs cannot reach it at all.
        for (auto const& field: runtimeType->fields)
        {
            INFO("field: " << name << "." << field.name);
            CHECK(frontendType.fieldTypes.contains(field.name));
        }
    }
}

TEST_CASE("TypeRegistry.parity.slot_count_covers_every_field")
{
    // A field declared past slotCount would be written by a producer and then read out of bounds.
    auto runtime = CoreVM::TypeRegistry {};
    auto frontend = TypeDefinitionRegistry {};
    frontend.registerBuiltins();

    for (auto const& [name, frontendType]: frontend.records())
    {
        auto const* runtimeType = runtime.get(frontendType.typeId);
        REQUIRE(runtimeType != nullptr);
        for (auto const& field: runtimeType->fields)
        {
            INFO("field: " << name << "." << field.name << " of slotCount " << runtimeType->slotCount);
            CHECK(field.offset < runtimeType->slotCount);
        }
    }
}
