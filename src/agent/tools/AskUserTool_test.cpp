// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/tools/AskUserTool.hpp>

using namespace endo::agent;

TEST_CASE("AskUserTool.name", "[agent][tools]")
{
    auto const tool = AskUserTool([](auto const&) -> UserAnswer { return {}; });
    CHECK(tool.name() == "ask_user");
}

TEST_CASE("AskUserTool.definition_schema", "[agent][tools]")
{
    auto const tool = AskUserTool([](auto const&) -> UserAnswer { return {}; });
    auto const def = tool.definition();

    CHECK(def.name == "ask_user");
    CHECK_FALSE(def.description.empty());
    CHECK(def.inputSchema.contains("properties"));
    CHECK(def.inputSchema["properties"].contains("question"));
    CHECK(def.inputSchema["properties"].contains("options"));

    auto const& required = def.inputSchema["required"];
    REQUIRE(required.is_array());
    CHECK(required.size() == 1);
    CHECK(required[0] == "question");
}

TEST_CASE("AskUserTool.missing_question_error", "[agent][tools]")
{
    auto tool = AskUserTool([](auto const&) -> UserAnswer { return {}; });
    auto const result = tool.execute(nlohmann::json::object());

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Missing required parameter") != std::string::npos);
}

TEST_CASE("AskUserTool.empty_question_error", "[agent][tools]")
{
    auto tool = AskUserTool([](auto const&) -> UserAnswer { return {}; });
    auto const result = tool.execute(nlohmann::json { { "question", "" } });

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Missing required parameter") != std::string::npos);
}

TEST_CASE("AskUserTool.free_text_answer", "[agent][tools]")
{
    auto tool = AskUserTool([](UserQuestion const& q) -> UserAnswer {
        CHECK(q.text == "What is your name?");
        CHECK(q.options.empty());
        return { .answer = "Alice" };
    });

    auto const result = tool.execute(nlohmann::json { { "question", "What is your name?" } });
    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content == "Alice");
}

TEST_CASE("AskUserTool.options_answer", "[agent][tools]")
{
    auto tool = AskUserTool([](UserQuestion const& q) -> UserAnswer {
        CHECK(q.text == "Pick a color");
        REQUIRE(q.options.size() == 3);
        CHECK(q.options[0] == "Red");
        CHECK(q.options[1] == "Green");
        CHECK(q.options[2] == "Blue");
        return { .answer = "Green" };
    });

    auto const args = nlohmann::json {
        { "question", "Pick a color" },
        { "options", nlohmann::json::array({ "Red", "Green", "Blue" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content == "Green");
}

TEST_CASE("AskUserTool.cancellation_handling", "[agent][tools]")
{
    auto tool = AskUserTool([](auto const&) -> UserAnswer { return { .cancelled = true }; });

    auto const result = tool.execute(nlohmann::json { { "question", "Something?" } });
    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("cancelled") != std::string::npos);
}

TEST_CASE("AskUserTool.options_too_few_error", "[agent][tools]")
{
    auto tool = AskUserTool([](auto const&) -> UserAnswer { return {}; });

    auto const args = nlohmann::json {
        { "question", "Pick one" },
        { "options", nlohmann::json::array({ "Only" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("at least 2") != std::string::npos);
}

TEST_CASE("AskUserTool.options_too_many_error", "[agent][tools]")
{
    auto tool = AskUserTool([](auto const&) -> UserAnswer { return {}; });

    auto const args = nlohmann::json {
        { "question", "Pick" },
        { "options", nlohmann::json::array({ "A", "B", "C", "D", "E", "F", "G" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("at most 6") != std::string::npos);
}

TEST_CASE("AskUserTool.options_empty_string_error", "[agent][tools]")
{
    auto tool = AskUserTool([](auto const&) -> UserAnswer { return {}; });

    auto const args = nlohmann::json {
        { "question", "Pick" },
        { "options", nlohmann::json::array({ "Valid", "" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("non-empty strings") != std::string::npos);
}

TEST_CASE("AskUserTool.null_callback_error", "[agent][tools]")
{
    auto tool = AskUserTool(nullptr);
    auto const result = tool.execute(nlohmann::json { { "question", "Test?" } });

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("callback") != std::string::npos);
}
