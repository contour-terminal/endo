// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <catch2/catch_test_macros.hpp>

#include "OutputParser.hpp"

using namespace endo;

namespace
{
/// Helper to create a minimal OutputVariant for testing.
OutputVariant makeJsonVariant(std::vector<OutputFieldSchema> schema, uint16_t typeId = 100)
{
    OutputVariant v;
    v.assignedTypeId = typeId;
    v.parser.type = ParserConfig::Type::Json;
    v.parser.format = ParserConfig::Format::Lines;
    v.schema = std::move(schema);
    return v;
}

OutputVariant makeFieldsVariant(std::vector<OutputFieldSchema> schema,
                                std::string separator,
                                std::optional<int> maxFields = {},
                                uint16_t typeId = 100)
{
    OutputVariant v;
    v.assignedTypeId = typeId;
    v.parser.type = ParserConfig::Type::Fields;
    v.parser.format = ParserConfig::Format::Lines;
    v.parser.fieldSeparator = std::move(separator);
    v.parser.maxFields = maxFields;
    v.schema = std::move(schema);
    return v;
}

/// Helper: counts elements in a cons-cell list.
size_t listLength(CoreVM::TypedObject* list)
{
    size_t count = 0;
    while (list && list->tag == 1)
    {
        ++count;
        list = reinterpret_cast<CoreVM::TypedObject*>(list->getSlot(1));
    }
    return count;
}

/// Helper: gets the Nth element from a cons-cell list.
CoreVM::TypedObject* listAt(CoreVM::TypedObject* list, size_t index)
{
    size_t i = 0;
    while (list && list->tag == 1)
    {
        if (i == index)
            return reinterpret_cast<CoreVM::TypedObject*>(list->getSlot(0));
        ++i;
        list = reinterpret_cast<CoreVM::TypedObject*>(list->getSlot(1));
    }
    return nullptr;
}

/// Helper: gets a string from a record slot.
std::string getStringSlot(CoreVM::TypedObject* record, uint8_t slot)
{
    const auto* str =
        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(record->getSlot(slot)));
    return str ? std::string(*str) : "";
}

/// Minimal runtime setup for OutputParser tests.
struct TestParserRuntime
{
    CoreVM::Runtime runtime;
    CoreVM::diagnostics::BufferedReport report;
    std::unique_ptr<CoreVM::IRProgram> irProgram;
    std::unique_ptr<CoreVM::Program> compiledProgram;

    /// Constructs a minimal program with custom product types registered.
    /// @param typeRegistrations Map of typeId → (name, slotCount) for types used by tests.
    explicit TestParserRuntime(
        std::vector<std::tuple<uint16_t, std::string, uint8_t>> const& typeRegistrations = {})
    {
        // Create a minimal program with a handler so we can create a Runner
        runtime.registerFunction("noop").returnType(CoreVM::LiteralType::Void).bind([](CoreVM::Params&) {});

        CoreVM::IRBuilder builder;
        builder.setProgram(std::make_unique<CoreVM::IRProgram>());

        // Register custom product types so TargetCodeGenerator adds them to the type registry
        for (auto const& [typeId, name, slotCount]: typeRegistrations)
        {
            CoreVM::IRProgram::CustomProductType customType;
            customType.name = name;
            customType.assignedId = typeId;
            for (uint8_t i = 0; i < slotCount; ++i)
                customType.fields.push_back({ .name = "field" + std::to_string(i),
                                              .offset = i,
                                              .type = CoreVM::LiteralType::String });
            builder.program()->addCustomProductType(std::move(customType));
        }

        auto* fn = builder.program()->createFunction("@test");
        builder.setFunction(fn);
        builder.setInsertPoint(fn->createBlock("entry"));
        builder.createRet(builder.get(static_cast<CoreVM::CoreNumber>(0)));

        irProgram = builder.takeProgram();

        CoreVM::TargetCodeGenerator codegen;
        compiledProgram = codegen.generate(irProgram.get());
        compiledProgram->link(&runtime, &report);
    }

    [[nodiscard]] CoreVM::Runner createRunner() const
    {
        auto* fn = compiledProgram->findFunction("@test");
        CoreVM::Runner::Globals globals;
        return CoreVM::Runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
    }
};
} // namespace

TEST_CASE("OutputParser.json_lines")
{
    TestParserRuntime rt({ { 100, "TestRecord", 2 } });
    auto runner = rt.createRunner();

    auto variant = makeJsonVariant({
        { .name = "name", .sourceKey = "Name", .type = CoreVM::LiteralType::String },
        { .name = "value", .sourceKey = "Value", .type = CoreVM::LiteralType::String },
    });

    const auto* const input = R"({"Name":"foo","Value":"bar"}
{"Name":"baz","Value":"qux"})";

    auto* result = OutputParser::parseJson(runner, input, variant);
    REQUIRE(result != nullptr);
    CHECK(listLength(result) == 2);

    auto* first = listAt(result, 0);
    REQUIRE(first != nullptr);
    CHECK(getStringSlot(first, 0) == "foo");
    CHECK(getStringSlot(first, 1) == "bar");

    auto* second = listAt(result, 1);
    REQUIRE(second != nullptr);
    CHECK(getStringSlot(second, 0) == "baz");
    CHECK(getStringSlot(second, 1) == "qux");
}

TEST_CASE("OutputParser.json_array")
{
    TestParserRuntime rt({ { 100, "TestRecord", 1 } });
    auto runner = rt.createRunner();

    auto variant = makeJsonVariant({
        { .name = "id", .sourceKey = "ID", .type = CoreVM::LiteralType::String },
    });
    variant.parser.format = ParserConfig::Format::Array;

    const auto* const input = R"([{"ID":"abc"},{"ID":"def"}])";

    auto* result = OutputParser::parseJson(runner, input, variant);
    REQUIRE(result != nullptr);
    CHECK(listLength(result) == 2);
}

TEST_CASE("OutputParser.json_empty_input")
{
    TestParserRuntime rt;
    auto runner = rt.createRunner();

    auto variant = makeJsonVariant({
        { .name = "name", .sourceKey = "Name", .type = CoreVM::LiteralType::String },
    });

    auto* result = OutputParser::parseJson(runner, "", variant);
    REQUIRE(result != nullptr);
    CHECK(listLength(result) == 0); // Empty list
    CHECK(result->tag == 0);        // Nil
}

TEST_CASE("OutputParser.json_malformed_lines_skipped")
{
    TestParserRuntime rt({ { 100, "TestRecord", 1 } });
    auto runner = rt.createRunner();

    auto variant = makeJsonVariant({
        { .name = "name", .sourceKey = "Name", .type = CoreVM::LiteralType::String },
    });

    const auto* const input = R"(not json
{"Name":"good"}
{bad json
{"Name":"also good"})";

    auto* result = OutputParser::parseJson(runner, input, variant);
    REQUIRE(result != nullptr);
    CHECK(listLength(result) == 2); // Only the 2 valid lines
}

TEST_CASE("OutputParser.json_missing_fields_default")
{
    TestParserRuntime rt({ { 100, "TestRecord", 2 } });
    auto runner = rt.createRunner();

    auto variant = makeJsonVariant({
        { .name = "name", .sourceKey = "Name", .type = CoreVM::LiteralType::String },
        { .name = "value", .sourceKey = "Value", .type = CoreVM::LiteralType::String },
    });

    const auto* const input = R"({"Name":"only_name"})";

    auto* result = OutputParser::parseJson(runner, input, variant);
    REQUIRE(result != nullptr);
    CHECK(listLength(result) == 1);

    auto* record = listAt(result, 0);
    REQUIRE(record != nullptr);
    CHECK(getStringSlot(record, 0) == "only_name");
    CHECK(getStringSlot(record, 1).empty()); // Default empty string
}

TEST_CASE("OutputParser.json_int_field_parsing")
{
    TestParserRuntime rt({ { 100, "TestRecord", 2 } });
    auto runner = rt.createRunner();

    auto variant = makeJsonVariant({
        { .name = "count", .sourceKey = "Count", .type = CoreVM::LiteralType::Number },
        { .name = "name", .sourceKey = "Name", .type = CoreVM::LiteralType::String },
    });

    const auto* const input = R"({"Count":42,"Name":"test"})";

    auto* result = OutputParser::parseJson(runner, input, variant);
    REQUIRE(result != nullptr);
    auto* record = listAt(result, 0);
    REQUIRE(record != nullptr);
    CHECK(static_cast<int64_t>(record->getSlot(0)) == 42);
    CHECK(getStringSlot(record, 1) == "test");
}

TEST_CASE("OutputParser.fields_space_separated_max2")
{
    TestParserRuntime rt({ { 100, "TestRecord", 2 } });
    auto runner = rt.createRunner();

    auto variant = makeFieldsVariant(
        {
            { .name = "status", .sourceKey = "", .type = CoreVM::LiteralType::String },
            { .name = "path", .sourceKey = "", .type = CoreVM::LiteralType::String },
        },
        " ",
        2);

    const auto* const input = "M src/main.cpp\n?? path with spaces\nA .gitignore\n";

    auto* result = OutputParser::parseFields(runner, input, variant);
    REQUIRE(result != nullptr);
    CHECK(listLength(result) == 3);

    auto* first = listAt(result, 0);
    REQUIRE(first != nullptr);
    CHECK(getStringSlot(first, 0) == "M");
    CHECK(getStringSlot(first, 1) == "src/main.cpp");

    auto* second = listAt(result, 1);
    REQUIRE(second != nullptr);
    CHECK(getStringSlot(second, 0) == "??");
    CHECK(getStringSlot(second, 1) == "path with spaces");
}

TEST_CASE("OutputParser.fields_empty_input")
{
    TestParserRuntime rt;
    auto runner = rt.createRunner();

    auto variant = makeFieldsVariant(
        {
            { .name = "a", .sourceKey = "", .type = CoreVM::LiteralType::String },
        },
        "\t");

    auto* result = OutputParser::parseFields(runner, "", variant);
    REQUIRE(result != nullptr);
    CHECK(listLength(result) == 0);
    CHECK(result->tag == 0); // Nil
}

// =============================================================================
// Schema descriptor parsing tests (Phase 6.1.3)
// =============================================================================

TEST_CASE("OutputParser.buildVariantFromDesc.two_fields")
{
    auto variant = OutputParser::buildVariantFromDesc("name:string,age:int", 200, ParserConfig::Type::Json);
    REQUIRE(variant.schema.size() == 2);
    CHECK(variant.schema[0].name == "name");
    CHECK(variant.schema[0].type == CoreVM::LiteralType::String);
    CHECK(variant.schema[1].name == "age");
    CHECK(variant.schema[1].type == CoreVM::LiteralType::Number);
    CHECK(variant.assignedTypeId == 200);
}

TEST_CASE("OutputParser.buildVariantFromDesc.bool_and_float")
{
    auto variant =
        OutputParser::buildVariantFromDesc("active:bool,score:float", 201, ParserConfig::Type::Fields);
    REQUIRE(variant.schema.size() == 2);
    CHECK(variant.schema[0].name == "active");
    CHECK(variant.schema[0].type == CoreVM::LiteralType::Boolean);
    CHECK(variant.schema[1].name == "score");
    CHECK(variant.schema[1].type == CoreVM::LiteralType::Float);
    CHECK(variant.parser.fieldSeparator == ",");
}

TEST_CASE("OutputParser.buildVariantFromDesc.single_field")
{
    auto variant = OutputParser::buildVariantFromDesc("id:int", 202, ParserConfig::Type::Json);
    REQUIRE(variant.schema.size() == 1);
    CHECK(variant.schema[0].name == "id");
    CHECK(variant.schema[0].type == CoreVM::LiteralType::Number);
}

TEST_CASE("OutputParser.detectCsvHeader.matches")
{
    std::vector<OutputFieldSchema> schema = {
        { .name = "name", .sourceKey = "", .type = CoreVM::LiteralType::String },
        { .name = "age", .sourceKey = "", .type = CoreVM::LiteralType::Number },
    };
    CHECK(OutputParser::detectCsvHeader("name,age", ",", schema));
}

TEST_CASE("OutputParser.detectCsvHeader.case_insensitive")
{
    std::vector<OutputFieldSchema> schema = {
        { .name = "name", .sourceKey = "", .type = CoreVM::LiteralType::String },
        { .name = "age", .sourceKey = "", .type = CoreVM::LiteralType::Number },
    };
    CHECK(OutputParser::detectCsvHeader("Name,Age", ",", schema));
}

TEST_CASE("OutputParser.detectCsvHeader.no_match_data")
{
    std::vector<OutputFieldSchema> schema = {
        { .name = "name", .sourceKey = "", .type = CoreVM::LiteralType::String },
        { .name = "age", .sourceKey = "", .type = CoreVM::LiteralType::Number },
    };
    CHECK_FALSE(OutputParser::detectCsvHeader("Alice,30", ",", schema));
}

TEST_CASE("OutputParser.detectCsvHeader.wrong_count")
{
    std::vector<OutputFieldSchema> schema = {
        { .name = "name", .sourceKey = "", .type = CoreVM::LiteralType::String },
        { .name = "age", .sourceKey = "", .type = CoreVM::LiteralType::Number },
    };
    CHECK_FALSE(OutputParser::detectCsvHeader("name,age,extra", ",", schema));
}
