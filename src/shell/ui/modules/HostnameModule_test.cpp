// SPDX-License-Identifier: Apache-2.0
#include <shell/ui/PromptColorResolver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include "HostnameModule.hpp"

using namespace endo;

namespace
{

/// @brief Builds a PromptContext with the given identity for HostnameModule tests.
[[nodiscard]] PromptContext makeContext(std::string username, std::string hostname)
{
    auto ctx = PromptContext {};
    ctx.username = std::move(username);
    ctx.hostname = std::move(hostname);
    return ctx;
}

/// @brief Renders a HostnameModule and returns the concatenated segment text.
[[nodiscard]] std::string renderHostname(PromptContext const& ctx)
{
    auto text = std::string {};
    for (auto const& seg: HostnameModule {}.evaluate(ctx))
        text += seg.text;
    return text;
}

} // namespace

TEST_CASE("HostnameModule.renders_user_at_host")
{
    auto const ctx = makeContext("alice", "web-prod-01");
    CHECK(HostnameModule {}.shouldShow(ctx));
    CHECK(renderHostname(ctx) == "alice@web-prod-01");
}

TEST_CASE("HostnameModule.username_only_omits_at")
{
    auto const ctx = makeContext("alice", "");
    CHECK(HostnameModule {}.shouldShow(ctx));
    CHECK(renderHostname(ctx) == "alice");
}

TEST_CASE("HostnameModule.hostname_only_omits_at")
{
    auto const ctx = makeContext("", "web-prod-01");
    CHECK(HostnameModule {}.shouldShow(ctx));
    CHECK(renderHostname(ctx) == "web-prod-01");
}

TEST_CASE("HostnameModule.hidden_when_identity_unknown")
{
    auto const ctx = makeContext("", "");
    CHECK_FALSE(HostnameModule {}.shouldShow(ctx));
}

TEST_CASE("HostnameModule.colors_user_and_host_independently")
{
    auto colors = ResolvedPromptColors {};
    colors.username = ColorSpec { .colors = { tui::RgbColor { .r = 1, .g = 2, .b = 3 } } };
    colors.hostname = ColorSpec { .colors = { tui::RgbColor { .r = 4, .g = 5, .b = 6 } } };

    auto ctx = makeContext("alice", "web-prod-01");
    ctx.resolvedColors = &colors;

    auto const userColor = tui::Color { colors.username.solid() };
    auto const hostColor = tui::Color { colors.hostname.solid() };

    auto const segments = HostnameModule {}.evaluate(ctx);
    REQUIRE(segments.size() == 3);
    CHECK(segments[0].text == "alice");
    CHECK(segments[0].style.fg == userColor);
    CHECK(segments[1].text == "@");
    CHECK(segments[1].style.fg == hostColor);
    CHECK(segments[2].text == "web-prod-01");
    CHECK(segments[2].style.fg == hostColor);
}
