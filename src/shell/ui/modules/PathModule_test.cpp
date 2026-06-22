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
