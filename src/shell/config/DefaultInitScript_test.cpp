// SPDX-License-Identifier: Apache-2.0
#include "DefaultInitScript.hpp"

#include <endo-language/builtins/PropertyDescriptors.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace
{
[[nodiscard]] bool contains(std::string const& haystack, std::string_view needle) noexcept
{
    return haystack.find(needle) != std::string::npos;
}
} // namespace

TEST_CASE("DefaultInitScript.generates_non_empty_script")
{
    auto const content = endo::generateDefaultInitEndo();
    REQUIRE_FALSE(content.empty());
    CHECK(contains(content, "~/.config/endo/init.endo"sv));
    CHECK(contains(content, "shell_prompt_preset"sv));
    CHECK(contains(content, "shell_prompt_indicator"sv));
}

TEST_CASE("DefaultInitScript.commented_out_by_default")
{
    // Every assignment line is commented out so the generated file is
    // behaviorally inert — the baked-in C++ defaults must remain authoritative.
    auto const content = endo::generateDefaultInitEndo();
    auto pos = std::size_t { 0 };
    while (pos < content.size())
    {
        auto const eol = content.find('\n', pos);
        auto const line = std::string_view(content).substr(pos, eol - pos);
        pos = eol == std::string::npos ? content.size() : eol + 1;

        // Allow blank lines and lines that start with `#`; that's all we emit.
        if (line.empty())
            continue;
        CHECK(line.front() == '#');
    }
}

TEST_CASE("DefaultInitScript.documents_every_prompt_property")
{
    // Every descriptor should contribute at least one line mentioning its name,
    // otherwise PropertyDescriptors got out of sync with the renderer.
    auto const content = endo::generateDefaultInitEndo();
    for (auto const& p: endo::promptPropertyDescriptors())
        CHECK(contains(content, p.name));
}

TEST_CASE("DefaultInitScript.enum_values_listed_as_comments")
{
    // For enum-valued properties we render a `# Values: ...` line listing
    // the permitted choices. Spot-check using `shell_prompt_preset`.
    auto const content = endo::generateDefaultInitEndo();
    CHECK(contains(content, "Values:"sv));
    CHECK(contains(content, "powerline"sv));
    CHECK(contains(content, "minimal-arrow"sv));
}

TEST_CASE("DefaultInitScript.key_bindings_reference_present")
{
    // The key-bindings block is reference-only — we just verify that a
    // representative default chord/action pair made it into the output.
    auto const content = endo::generateDefaultInitEndo();
    CHECK(contains(content, "Default key bindings"sv));
    CHECK(contains(content, "undo"sv));
    CHECK(contains(content, "copy"sv));
}

TEST_CASE("DefaultInitScript.pattern_matching_example_present")
{
    // Demonstrates F#-style pattern matching + command substitution as the
    // issue asks for — keeps the file useful as a teaching artifact.
    auto const content = endo::generateDefaultInitEndo();
    CHECK(contains(content, "match os with"sv));
    CHECK(contains(content, "uname -s"sv));
    CHECK(contains(content, "\"Linux\""sv));
    CHECK(contains(content, "\"Darwin\""sv));
}

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
TEST_CASE("DefaultInitScript.agent_section_present_when_agent_enabled")
{
    auto const content = endo::generateDefaultInitEndo();
    CHECK(contains(content, "agent_provider"sv));
}
#else
TEST_CASE("DefaultInitScript.agent_section_absent_when_agent_disabled")
{
    auto const content = endo::generateDefaultInitEndo();
    CHECK_FALSE(contains(content, "agent_provider"sv));
}
#endif
