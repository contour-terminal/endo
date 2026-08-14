// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include "PathModule.hpp"

using namespace endo;

namespace
{

/// @brief Renders a PathModule for the given cwd/home and returns the resulting text.
[[nodiscard]] std::string renderPath(std::string cwd, std::string homePath)
{
    auto ctx = PromptContext {};
    ctx.cwd = std::move(cwd);
    ctx.homePath = std::move(homePath);
    auto const segments = PathModule {}.evaluate(ctx);
    REQUIRE(segments.size() == 1);
    return segments.front().text;
}

} // namespace

TEST_CASE("PathModule.contracts_home_to_tilde")
{
    CHECK(renderPath("/home/alice/projects", "/home/alice") == "~/projects");
    CHECK(renderPath("/home/alice", "/home/alice") == "~");
}

TEST_CASE("PathModule.leaves_non_home_paths_unchanged")
{
    CHECK(renderPath("/usr/local/bin", "/home/alice") == "/usr/local/bin");
}

TEST_CASE("PathModule.does_not_contract_partial_component_match")
{
    // "/home/alice2" shares a textual prefix with home "/home/alice" but is a
    // different directory, so it must be left intact (not rendered as "~2").
    CHECK(renderPath("/home/alice2", "/home/alice") == "/home/alice2");
}

TEST_CASE("PathModule.no_home_path_leaves_cwd_unchanged")
{
    CHECK(renderPath("/home/alice", "") == "/home/alice");
}

#if defined(_WIN32)
TEST_CASE("PathModule.contracts_home_case_insensitively_on_windows")
{
    // On Windows the filesystem is case-insensitive, so cwd and home may differ in
    // case while naming the same directory; contraction must still apply.
    CHECK(renderPath("C:/Users/Alice/Documents", "c:/users/alice") == "~/Documents");
}
#endif

// ============================================================================
// OSC 8 hyperlink target
// ============================================================================

namespace
{

/// @brief Renders a PathModule and returns the single segment.
[[nodiscard]] PromptSegment renderSegment(std::string cwd, std::string homePath, bool hyperlinks)
{
    auto ctx = PromptContext {};
    ctx.cwd = std::move(cwd);
    ctx.homePath = std::move(homePath);
    ctx.hostname = "box";
    ctx.hyperlinks = hyperlinks;
    auto segments = PathModule {}.evaluate(ctx);
    REQUIRE(segments.size() == 1);
    return std::move(segments.front());
}

} // namespace

TEST_CASE("PathModule.hyperlink_targets_real_path_not_tilde")
{
    // The displayed text is contracted for brevity, but a link must address the actual
    // directory — "~/projects" is not a path any terminal can open.
    auto const segment = renderSegment("/home/alice/projects", "/home/alice", true);
    CHECK(segment.text == "~/projects");
    CHECK(segment.hyperlink == "file://box/home/alice/projects");
}

TEST_CASE("PathModule.hyperlink_percent_encodes_the_path")
{
    auto const segment = renderSegment("/tmp/my dir", "", true);
    CHECK(segment.hyperlink == "file://box/tmp/my%20dir");
}

TEST_CASE("PathModule.hyperlink_omitted_when_disabled")
{
    auto const segment = renderSegment("/home/alice/projects", "/home/alice", false);
    CHECK(segment.text == "~/projects");
    CHECK(segment.hyperlink.empty());
}

TEST_CASE("PathModule.hyperlink_uses_local_form_without_hostname")
{
    auto ctx = PromptContext {};
    ctx.cwd = "/tmp/x";
    ctx.hyperlinks = true;
    auto const segments = PathModule {}.evaluate(ctx);
    REQUIRE(segments.size() == 1);
    CHECK(segments.front().hyperlink == "file:///tmp/x");
}
