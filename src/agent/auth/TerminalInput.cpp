// SPDX-License-Identifier: Apache-2.0
#include "TerminalInput.hpp"

#include <tui/QuestionComponent.hpp>
#include <tui/Screen.hpp>
#include <tui/Terminal.hpp>

#include <cstdlib>
#include <print>
#include <string>
#include <vector>

namespace endo::agent
{

namespace
{
    /// @brief Result from the TUI question event loop.
    struct QuestionResult
    {
        bool confirmed = false;
        std::size_t selectedIndex = 0;
        std::string answer;
    };

    /// @brief Runs a QuestionComponent in an inline Terminal+Screen event loop.
    /// @param config The question configuration.
    /// @return The result of the question interaction.
    auto runQuestion(tui::QuestionConfig config) -> QuestionResult
    {
        auto terminal = tui::Terminal {};
        if (auto result = terminal.initialize(); !result)
        {
            std::println(stderr, "Terminal initialization failed: {}", result.error());
            return {};
        }

        auto screen = tui::Screen(terminal, { .viewport = tui::Viewport::Inline });

        auto question = tui::QuestionComponent(std::move(config));

        auto const prefSize = question.preferredSize();
        auto const width = terminal.columns();
        auto const height = prefSize.height;
        auto const area = tui::Rect { .x = 0, .y = 0, .width = width, .height = height };

        question.setArea(area);
        screen.root().addChild(question, { .area = area });
        screen.setFocus(&question);

        auto result = QuestionResult {};
        auto running = true;
        while (running)
        {
            screen.draw();

            for (auto const& event: terminal.poll(-1))
            {
                auto const action = question.processInput(event);
                switch (action)
                {
                    case tui::QuestionAction::Confirmed:
                        result.confirmed = true;
                        result.selectedIndex = question.selectedIndex();
                        result.answer = question.answer();
                        running = false;
                        break;
                    case tui::QuestionAction::Cancelled: running = false; break;
                    case tui::QuestionAction::Changed: screen.invalidate(question); break;
                    case tui::QuestionAction::None: break;
                }
                if (!running)
                    break;
            }
        }

        screen.clearAndRelease();
        terminal.shutdown();

        return result;
    }
} // namespace

auto askSingleSelect(std::string_view question, std::span<std::string_view const> options)
    -> std::optional<std::size_t>
{
    auto optionStrings = std::vector<std::string> {};
    optionStrings.reserve(options.size());
    for (auto const& opt: options)
        optionStrings.emplace_back(opt);

    auto result = runQuestion(tui::QuestionConfig {
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
    auto result = runQuestion(tui::QuestionConfig {
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
