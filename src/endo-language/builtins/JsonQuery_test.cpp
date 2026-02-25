// SPDX-License-Identifier: Apache-2.0

#include <endo-language/TestHelper.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::test;

TEST_CASE("Json.query.basic_key")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json = "{\"name\": \"hello\"}"
        let r = Json.query ".name" json
        r |> each println
    )") == "hello\n");
}

TEST_CASE("Json.query.nested_key")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json = "{\"a\": {\"b\": \"val\"}}"
        let r = Json.query ".a.b" json
        r |> each println
    )") == "val\n");
}

TEST_CASE("Json.query.array_iteration")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json = "{\"items\":[{\"name\":\"alpha\"},{\"name\":\"beta\"}]}"
        let r = Json.query ".items[].name" json
        r |> each println
    )") == "alpha\nbeta\n");
}

TEST_CASE("Json.query.top_level_array")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json = "[{\"x\":\"1\"},{\"x\":\"2\"}]"
        let r = Json.query ".[].x" json
        r |> each println
    )") == "1\n2\n");
}

TEST_CASE("Json.query.numeric_values")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json = "{\"count\": 42}"
        let r = Json.query ".count" json
        r |> each println
    )") == "42\n");
}

TEST_CASE("Json.query.boolean_values")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json = "{\"flag\": true}"
        let r = Json.query ".flag" json
        r |> each println
    )") == "true\n");
}

TEST_CASE("Json.query.missing_key")
{
    auto output = executeSourceAndGetOutput(R"(
        let json = "{\"name\": \"hello\"}"
        let r = Json.query ".missing" json
        r |> each println
    )");
    CHECK(output.empty());
}

TEST_CASE("Json.query.invalid_json")
{
    auto output = executeSourceAndGetOutput(R"(
        let json = "not valid json {{"
        let r = Json.query ".name" json
        r |> each println
    )");
    CHECK(output.empty());
}

TEST_CASE("Json.query.empty_path")
{
    auto output = executeSourceAndGetOutput(R"(
        let json = "{\"name\": \"hello\"}"
        let r = Json.query "" json
        r |> each println
    )");
    CHECK(output.empty());
}

TEST_CASE("Json.query.pipeline_usage")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json = "{\"name\": \"hello\"}"
        json |> Json.query ".name" |> each println
    )") == "hello\n");
}

TEST_CASE("Json.query.cmake_presets_pattern")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json = "{\"configurePresets\":[{\"name\":\"clang-debug\"},{\"name\":\"clang-release\"}]}"
        let r = Json.query ".configurePresets[].name" json
        r |> each println
    )") == "clang-debug\nclang-release\n");
}

TEST_CASE("Json.query.list_append_pattern")
{
    CHECK(executeSourceAndGetOutput(R"(
        let json1 = "{\"presets\":[{\"name\":\"a\"},{\"name\":\"b\"}]}"
        let json2 = "{\"presets\":[{\"name\":\"c\"}]}"
        let results = Json.query ".presets[].name" json1 @ Json.query ".presets[].name" json2
        results |> each println
    )") == "a\nb\nc\n");
}
