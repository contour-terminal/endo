// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/RecordWriter.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeRegistry.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <string_view>

using namespace endo::builtins;

namespace
{

/// @brief Owns a Runner, which is what record allocation and string interning need.
///
/// The program is a stub: the body never runs, but Runner dereferences its function for the stack
/// size, so a null one is not an option.
class RunnerFixture
{
  public:
    RunnerFixture():
        _program(makeHostProgram()),
        _runner(_program.findFunction("host"),
                nullptr,
                &_globals,
                CoreVM::RuntimeConfig::defaultConfig(),
                nullptr)
    {
    }

    [[nodiscard]] CoreVM::Runner* runner() noexcept { return &_runner; }

  private:
    [[nodiscard]] static CoreVM::Program makeHostProgram()
    {
        auto pool = CoreVM::ConstantPool {};
        pool.setFunction("host",
                         { CoreVM::makeInstruction(CoreVM::Opcode::ILOAD, 0),
                           CoreVM::makeInstruction(CoreVM::Opcode::URET) });
        return CoreVM::Program { std::move(pool) };
    }

    CoreVM::Runner::Globals _globals;
    CoreVM::Program _program;
    CoreVM::Runner _runner;
};

/// @brief Reads a String slot back as a view; the slot must not be null.
[[nodiscard]] std::string_view stringSlot(CoreVM::TypedObject const* record, std::string_view field)
{
    auto const* info = record->type->getFieldByName(field);
    REQUIRE(info != nullptr);
    auto const* text =
        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(record->getSlot(info->offset)));
    REQUIRE(text != nullptr);
    return *text;
}

} // namespace

TEST_CASE("RecordWriter.resolves_slots_by_name_not_by_call_order")
{
    // The point of the writer: a producer states field names, and the offsets come from the
    // descriptor. Setting them in an order unrelated to the layout must still land correctly.
    auto fixture = RunnerFixture {};

    auto writer = RecordWriter { fixture.runner(), CoreVM::BuiltinTypeId::JobInfo };
    auto* record =
        writer.set("pid", 4242).set("command", "sleep 1").set("id", 7).set("state", "Running").record();

    REQUIRE(record != nullptr);
    CHECK(record->getSlot(record->type->getFieldByName("id")->offset) == 7);
    CHECK(record->getSlot(record->type->getFieldByName("pid")->offset) == 4242);
    CHECK(stringSlot(record, "state") == "Running");
    CHECK(stringSlot(record, "command") == "sleep 1");
}

TEST_CASE("RecordWriter.backfills_string_fields_the_producer_left_unset")
{
    // The failure this closes: `find` reports no symlink information, so it never wrote FileInfo's
    // `target`, and the zero-initialised slot read back as a null CoreString. Every String field
    // must be readable whether or not the producer had anything to say about it.
    auto fixture = RunnerFixture {};

    auto writer = RecordWriter { fixture.runner(), CoreVM::BuiltinTypeId::FileInfo };
    auto* record = writer.set("name", "a.txt").record();

    auto const* descriptor = CoreVM::builtinTypes().get(CoreVM::BuiltinTypeId::FileInfo);
    REQUIRE(descriptor != nullptr);
    for (auto const& field: descriptor->fields)
    {
        if (field.type != CoreVM::LiteralType::String)
            continue;
        INFO("field: FileInfo." << field.name);
        CHECK(record->getSlot(field.offset) != 0);
    }
    CHECK(stringSlot(record, "name") == "a.txt");
    CHECK(stringSlot(record, "target").empty());
    CHECK(stringSlot(record, "path").empty());
}

TEST_CASE("RecordWriter.stores_a_string_literal_as_text_not_as_a_boolean")
{
    // A plain `bool` overload would win against string_view for a `char const*` argument, storing 1
    // where a name belongs. The bool setter is constrained so that cannot happen.
    auto fixture = RunnerFixture {};

    auto writer = RecordWriter { fixture.runner(), CoreVM::BuiltinTypeId::KeyBindingInfo };
    auto* record = writer.set("key", "Ctrl+A").set("action", "MoveToLineBegin").record();

    CHECK(stringSlot(record, "key") == "Ctrl+A");
    CHECK(stringSlot(record, "action") == "MoveToLineBegin");
}

TEST_CASE("RecordWriter.writes_numbers_booleans_and_floats_to_their_declared_types")
{
    auto fixture = RunnerFixture {};

    auto fileInfo = RecordWriter { fixture.runner(), CoreVM::BuiltinTypeId::FileInfo };
    auto* file = fileInfo.set("name", "d").set("isDir", true).set("isSymlink", false).record();
    CHECK(file->getSlot(file->type->getFieldByName("isDir")->offset) == 1);
    CHECK(file->getSlot(file->type->getFieldByName("isSymlink")->offset) == 0);

    auto process = RecordWriter { fixture.runner(), CoreVM::BuiltinTypeId::ProcessInfo };
    auto* proc = process.set("pid", 1).set("cpu", 12.5).set("user", "root").record();
    auto const cpuSlot = proc->getSlot(proc->type->getFieldByName("cpu")->offset);
    CHECK(std::bit_cast<double>(cpuSlot) == 12.5);
}
