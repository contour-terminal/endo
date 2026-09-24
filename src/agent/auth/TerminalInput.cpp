// SPDX-License-Identifier: Apache-2.0
#include "TerminalInput.hpp"

#include <core/net/EventLoop.hpp>
#include <core/net/IoBackend.hpp>
#include <core/tui/QuestionComponent.hpp>
#include <core/tui/Screen.hpp>
#include <core/tui/Terminal.hpp>
#include <core/tui/runtime/Modal.hpp>
#include <core/tui/runtime/TerminalInputSource.hpp>
#include <core/tui/runtime/TuiRuntime.hpp>

#include <cstdlib>
#include <print>
#include <string>
#include <vector>

namespace endo::agent
{

namespace
{
    /// @brief Runs a QuestionComponent to completion on its own coroutine runtime.
    ///
    /// This auth prompt runs outside the shell's main loop, so it owns a fresh
    /// Terminal, Screen, and TuiRuntime and drives the question with `runModal`.
    /// @param config The question configuration.
    /// @return The result of the question interaction.
    auto runQuestion(core::tui::QuestionConfig config) -> core::tui::QuestionResult
    {
        auto terminal = core::tui::Terminal {};
        if (auto result = terminal.initialize(); !result)
        {
            std::println(stderr, "Terminal initialization failed: {}", result.error());
            return {};
        }

        auto screen = core::tui::Screen(terminal, { .viewport = core::tui::Viewport::Inline });

        auto question = core::tui::QuestionComponent(std::move(config));

        auto const prefSize = question.preferredSize();
        auto const width = terminal.columns();
        auto const height = prefSize.height;
        auto const area = core::tui::Rect { .x = 0, .y = 0, .width = width, .height = height };

        question.setArea(area);
        screen.root().addChild(question, { .area = area });
        screen.setFocus(&question);

        // The runtime and its loop watch the terminal's handles, so both are gone before the
        // terminal shuts down and closes them.
        auto const result = [&] {
            auto const backend = core::net::makeDefaultBackend();
            auto loop = core::net::EventLoop { *backend };
            auto runtime = core::tui::runtime::TuiRuntime(loop, terminal);
            return runtime.blockOn(core::tui::runtime::runModal(&runtime, &question, &screen));
        }();

        screen.clearAndRelease();
        terminal.shutdown();

        return result.value_or(core::tui::QuestionResult {});
    }
} // namespace

auto askSingleSelect(std::string_view question, std::span<std::string_view const> options)
    -> std::optional<std::size_t>
{
    auto optionStrings = std::vector<std::string> {};
    optionStrings.reserve(options.size());
    for (auto const& opt: options)
        optionStrings.emplace_back(opt);

    auto result = runQuestion(core::tui::QuestionConfig {
        .questionText = std::string(question),
        .options = std::move(optionStrings),
        .multiSelect = false,
        .allowOther = false,
    });

    if (!result.confirmed)
        return std::nullopt;

    return result.selectedIndex;
}

auto askFreeText(std::string_view question, bool masked) -> std::optional<std::string>
{
    auto result = runQuestion(core::tui::QuestionConfig {
        .questionText = std::string(question),
        .options = {},
        .multiSelect = false,
        .allowOther = true,
        .masked = masked,
    });

    if (!result.confirmed)
        return std::nullopt;

    return std::move(result.answer);
}

auto openBrowser(std::string_view url) -> bool
{
    auto const urlStr = std::string(url);

#if defined(__APPLE__)
    auto const command = std::string("open '") + urlStr + "' 2>/dev/null";
#elif defined(_WIN32)
    auto const command = std::string("start \"\" '") + urlStr + "' 2>NUL";
#else
    auto const command = std::string("xdg-open '") + urlStr + "' 2>/dev/null";
#endif

    // NOLINTNEXTLINE(cert-env33-c) - intentional: launching user's default browser
    auto const exitCode = std::system(command.c_str());
    return exitCode == 0;
}

} // namespace endo::agent
