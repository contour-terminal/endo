// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
    #include <csignal>
#endif

#include "ContextSignalling.hpp"

using namespace endo::shellintegration;
using namespace std::string_view_literals;

namespace
{

constexpr auto TestId = "ab000000000000000000000000000001"sv;

/// Renders the end fields of an outcome as `key=value` pairs joined by `;`.
std::string renderEndFields(CommandOutcome const& outcome)
{
    auto result = std::string {};
    for (auto const& field: commandEndFields(outcome))
    {
        if (!result.empty())
            result += ';';
        result += field.key;
        result += '=';
        result += field.value;
    }
    return result;
}

/// Number of continuation bytes a UTF-8 lead byte announces, or nullopt for an invalid lead byte.
std::optional<int> continuationBytes(unsigned char lead)
{
    if ((lead & 0x80U) == 0)
        return 0;
    if ((lead & 0xE0U) == 0xC0U)
        return 1;
    if ((lead & 0xF0U) == 0xE0U)
        return 2;
    if ((lead & 0xF8U) == 0xF0U)
        return 3;
    return std::nullopt;
}

/// Checks that @p text is well-formed UTF-8 (no stray continuation or truncated sequence).
bool isValidUtf8(std::string_view text)
{
    auto pending = 0;
    for (auto const byte: text)
    {
        auto const ch = static_cast<unsigned char>(byte);
        if (pending > 0)
        {
            if ((ch & 0xC0U) != 0x80U)
                return false;
            --pending;
            continue;
        }
        auto const count = continuationBytes(ch);
        if (!count)
            return false;
        pending = *count;
    }
    return pending == 0;
}

} // namespace

TEST_CASE("ContextSignalling.start_shell")
{
    auto const fields = std::array {
        ContextField { .key = "type", .value = "shell" },
        ContextField { .key = "user", .value = "u" },
    };
    CHECK(contextStartSequence(TestId, fields)
          == "\033]3008;start=ab000000000000000000000000000001;type=shell;user=u\033\\"sv);
}

TEST_CASE("ContextSignalling.end_sequence")
{
    auto const fields = std::array {
        ContextField { .key = "exit", .value = "success" },
        ContextField { .key = "status", .value = "0" },
    };
    CHECK(contextEndSequence(TestId, fields)
          == "\033]3008;end=ab000000000000000000000000000001;exit=success;status=0\033\\"sv);
    CHECK(contextEndSequence(TestId, {}) == "\033]3008;end=ab000000000000000000000000000001\033\\"sv);
}

TEST_CASE("ContextSignalling.cwd_escaping_and_limit")
{
    SECTION("escaping")
    {
        auto const fields = std::array { ContextField { .key = "cwd", .value = "/a;b\\c\nd" } };
        CHECK(contextStartSequence(TestId, fields)
              == "\033]3008;start=ab000000000000000000000000000001;cwd=/a\\x3bb\\x5ccd\033\\"sv);
    }
    SECTION("over 255 encoded bytes is omitted")
    {
        auto const fields = std::array { ContextField { .key = "cwd", .value = std::string(300, 'x') } };
        auto const sequence = contextStartSequence(TestId, fields);
        CHECK(sequence.find("cwd=") == std::string::npos);
        CHECK(sequence == "\033]3008;start=ab000000000000000000000000000001\033\\"sv);
    }
    SECTION("the limit applies after escaping")
    {
        // 64 semicolons encode to 256 bytes.
        auto const fields = std::array { ContextField { .key = "cwd", .value = std::string(64, ';') } };
        CHECK(contextStartSequence(TestId, fields).find("cwd=") == std::string::npos);
    }
    SECTION("exactly 255 encoded bytes is kept")
    {
        auto const fields = std::array { ContextField { .key = "cwd", .value = std::string(255, 'x') } };
        CHECK(contextStartSequence(TestId, fields).find("cwd=" + std::string(255, 'x')) != std::string::npos);
    }
    SECTION("other oversized keys are omitted")
    {
        auto const fields = std::array { ContextField { .key = "user", .value = std::string(300, 'x') } };
        CHECK(contextStartSequence(TestId, fields).find("user=") == std::string::npos);
    }
}

TEST_CASE("ContextSignalling.cmdline_truncated_utf8")
{
    auto value = std::string {};
    for ([[maybe_unused]] auto const copy: std::views::iota(0, 300))
        value += "\xC3\xA9"sv; // U+00E9
    auto const fields = std::array { ContextField { .key = "cmdline", .value = value } };
    auto const sequence = contextStartSequence(TestId, fields);

    auto const prefix = "\033]3008;start=ab000000000000000000000000000001;cmdline="sv;
    REQUIRE(sequence.starts_with(prefix));
    REQUIRE(sequence.ends_with("\033\\"sv));
    auto const encoded =
        std::string_view(sequence).substr(prefix.size(), sequence.size() - prefix.size() - 2);
    CHECK(encoded.size() <= 255);
    CHECK(encoded.size() == 254); // 127 whole code points
    CHECK(isValidUtf8(encoded));
}

TEST_CASE("ContextSignalling.cmdline_truncation_counts_escapes")
{
    auto const fields = std::array { ContextField { .key = "cmdline", .value = std::string(100, ';') } };
    auto const sequence = contextStartSequence(TestId, fields);
    auto const prefix = "\033]3008;start=ab000000000000000000000000000001;cmdline="sv;
    auto const encoded =
        std::string_view(sequence).substr(prefix.size(), sequence.size() - prefix.size() - 2);
    CHECK(encoded.size() == 252); // 63 escapes of 4 bytes
}

TEST_CASE("ContextSignalling.exit_classification")
{
    CHECK(renderEndFields({ .status = 0 }) == "exit=success;status=0"sv);
    CHECK(renderEndFields({ .status = 1 }) == "exit=failure;status=1"sv);
    // No signal reported: a 139 status alone is just a failure.
    CHECK(renderEndFields({ .status = 139, .signal = std::nullopt }) == "exit=failure;status=139"sv);
#if !defined(_WIN32)
    CHECK(renderEndFields({ .status = 130, .signal = SIGINT })
          == "exit=interrupt;status=130;signal=SIGINT"sv);
    CHECK(renderEndFields({ .status = 139, .signal = SIGSEGV }) == "exit=crash;status=139;signal=SIGSEGV"sv);
    CHECK(renderEndFields({ .status = 134, .signal = SIGABRT }) == "exit=crash;status=134;signal=SIGABRT"sv);
    CHECK(renderEndFields({ .status = 143, .signal = SIGTERM })
          == "exit=failure;status=143;signal=SIGTERM"sv);
    CHECK(renderEndFields({ .status = 148, .signal = SIGTSTP, .stopped = true })
          == "exit=interrupt;status=148;signal=SIGTSTP"sv);
#endif
}

TEST_CASE("ContextSignalling.context_id")
{
    auto next = std::uint64_t { 0 };
    CHECK(newContextId([&] { return ++next; }) == "00000000000000010000000000000002"sv);
    CHECK(newContextId([] { return std::uint64_t { 0xDEADBEEFCAFEF00DU }; })
          == "deadbeefcafef00ddeadbeefcafef00d"sv);
}
