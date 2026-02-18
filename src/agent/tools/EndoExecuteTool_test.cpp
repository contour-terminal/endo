// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/tools/EndoExecuteTool.hpp>

using namespace endo::agent;

namespace
{

auto makeMockCallback(EndoExecResult result) -> EndoExecuteCallback
{
    return
        [result = std::move(result)](std::string const& /*source*/, std::chrono::milliseconds /*timeout*/) {
            return result;
        };
}

} // namespace

TEST_CASE("EndoExecuteTool.normal_execution", "[agent][tools]")
{
    auto tool = EndoExecuteTool(makeMockCallback(EndoExecResult {
        .output = "3\n",
        .exitCode = 0,
        .timedOut = false,
    }));

    auto const args = nlohmann::json { { "source", "print (1 + 2)" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("Exit code: 0") != std::string::npos);
    CHECK(result->content.find("3") != std::string::npos);
}

TEST_CASE("EndoExecuteTool.error_exit_code", "[agent][tools]")
{
    auto tool = EndoExecuteTool(makeMockCallback(EndoExecResult {
        .output = "error: undefined variable\n",
        .exitCode = 1,
        .timedOut = false,
    }));

    auto const args = nlohmann::json { { "source", "print undefined_var" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("Exit code: 1") != std::string::npos);
}

TEST_CASE("EndoExecuteTool.timeout", "[agent][tools]")
{
    auto tool = EndoExecuteTool(makeMockCallback(EndoExecResult {
        .output = "partial output",
        .exitCode = -1,
        .timedOut = true,
    }));

    auto const args =
        nlohmann::json { { "source", "let rec loop x = loop x in loop 0" }, { "timeout_ms", 1000 } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("timed out") != std::string::npos);
}

TEST_CASE("EndoExecuteTool.output_truncation", "[agent][tools]")
{
    // Create output larger than 30KB
    auto largeOutput = std::string(40000, 'x');

    auto tool = EndoExecuteTool(makeMockCallback(EndoExecResult {
        .output = std::move(largeOutput),
        .exitCode = 0,
        .timedOut = false,
    }));

    auto const args = nlohmann::json { { "source", "print (string_replicate 40000 \"x\")" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("truncated") != std::string::npos);
}

TEST_CASE("EndoExecuteTool.missing_source", "[agent][tools]")
{
    auto tool = EndoExecuteTool(makeMockCallback(EndoExecResult {}));

    auto const result = tool.execute(nlohmann::json::object());

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("source") != std::string::npos);
}

TEST_CASE("EndoExecuteTool.captures_source_and_timeout", "[agent][tools]")
{
    auto capturedSource = std::string {};
    auto capturedTimeout = std::chrono::milliseconds {};

    auto tool = EndoExecuteTool([&](std::string const& source, std::chrono::milliseconds timeout) {
        capturedSource = source;
        capturedTimeout = timeout;
        return EndoExecResult { .output = "ok", .exitCode = 0, .timedOut = false };
    });

    auto const args = nlohmann::json { { "source", "print 42" }, { "timeout_ms", 5000 } };
    (void) tool.execute(args);

    CHECK(capturedSource == "print 42");
    CHECK(capturedTimeout == std::chrono::milliseconds(5000));
}

TEST_CASE("EndoExecuteTool.no_callback_configured", "[agent][tools]")
{
    auto tool = EndoExecuteTool(EndoExecuteCallback {});

    auto const args = nlohmann::json { { "source", "print 1" } };
    auto const result = tool.execute(args);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("callback") != std::string::npos);
}
