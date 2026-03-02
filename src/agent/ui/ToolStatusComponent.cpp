// SPDX-License-Identifier: Apache-2.0
#include "ToolStatusComponent.hpp"

#include <tui/Canvas.hpp>
#include <tui/Theme.hpp>

#include <algorithm>
#include <format>
#include <ranges>

namespace endo::agent
{

void ToolStatusComponent::toolStarted(ToolCall const& call)
{
    _entries.push_back(ToolEntry {
        .name = call.name,
        .argsSummary = formatArgsSummary(call.name, call.arguments),
        .startTime = std::chrono::steady_clock::now(),
        .completion = std::nullopt,
    });
    _spinner.reset();
}

void ToolStatusComponent::toolCompleted(ToolResultMessage const& result)
{
    // Find the most recent entry with matching name that hasn't completed yet.
    for (auto& _entrie: std::ranges::reverse_view(_entries))
    {
        if (_entrie.name == result.name && !_entrie.completion.has_value())
        {
            _entrie.completion = ToolCompletionInfo {
                .isError = result.isError,
                .outputSize = result.content.size(),
                .duration = result.duration,
            };
            return;
        }
    }
}

void ToolStatusComponent::clear()
{
    _entries.clear();
}

bool ToolStatusComponent::hasEntries() const noexcept
{
    return !_entries.empty();
}

bool ToolStatusComponent::hasActiveEntry() const noexcept
{
    return !_entries.empty() && !_entries.back().completion.has_value();
}

bool ToolStatusComponent::tickSpinner()
{
    if (!hasActiveEntry())
        return false;
    return _spinner.tick();
}

int ToolStatusComponent::spinnerTimeoutMs() const
{
    if (!hasActiveEntry())
        return -1;
    return static_cast<int>(_spinner.interval().count());
}

auto ToolStatusComponent::formatElapsed(std::chrono::milliseconds ms) -> std::string
{
    auto const totalSeconds = ms.count() / 1000;
    auto const tenths = (ms.count() % 1000) / 100;

    if (totalSeconds >= 60)
    {
        auto const minutes = totalSeconds / 60;
        auto const seconds = totalSeconds % 60;
        return std::format("{}m {}s", minutes, seconds);
    }

    return std::format("{}.{}s", totalSeconds, tenths);
}

auto ToolStatusComponent::formatSize(size_t bytes) -> std::string
{
    if (bytes < 1024)
        return std::format("{} B", bytes);

    auto const kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0)
        return std::format("{:.1f} KB", kb);

    auto const mb = kb / 1024.0;
    return std::format("{:.1f} MB", mb);
}

auto ToolStatusComponent::formatArgsSummary(std::string const& name, nlohmann::json const& arguments)
    -> std::string
{
    if (name == "shell_execute" || name == "endo_execute")
    {
        auto command = std::string {};
        if (arguments.contains("command") && arguments["command"].is_string())
            command = arguments["command"].get<std::string>();
        else if (arguments.contains("source") && arguments["source"].is_string())
            command = arguments["source"].get<std::string>();

        // Truncate to first line for multi-line commands.
        if (auto const nl = command.find('\n'); nl != std::string::npos)
            command = command.substr(0, nl) + "...";

        return "$ " + command;
    }

    if (arguments.is_null() || (arguments.is_object() && arguments.empty()))
        return {};

    // For read_file/glob/grep: show the path/pattern argument.
    if (name == "read_file" && arguments.contains("path") && arguments["path"].is_string())
        return arguments["path"].get<std::string>();

    if (name == "glob" && arguments.contains("pattern") && arguments["pattern"].is_string())
        return arguments["pattern"].get<std::string>();

    if (name == "grep" && arguments.contains("pattern") && arguments["pattern"].is_string())
    {
        auto result = arguments["pattern"].get<std::string>();
        if (arguments.contains("path") && arguments["path"].is_string())
            result += " " + arguments["path"].get<std::string>();
        return result;
    }

    if ((name == "write_file" || name == "edit_file") && arguments.contains("path")
        && arguments["path"].is_string())
        return arguments["path"].get<std::string>();

    // Generic: compact JSON with large fields replaced.
    auto truncated = arguments;
    for (auto const& key: { "content", "new_string", "old_string" })
    {
        if (truncated.contains(key) && truncated[key].is_string())
        {
            auto const len = truncated[key].get<std::string>().size();
            truncated[key] = std::format("<{} chars>", len);
        }
    }
    return truncated.dump(-1);
}

tui::Size ToolStatusComponent::preferredSize() const
{
    if (_entries.empty())
        return { .width = 0, .height = 0 };

    // Count visible entries: up to MaxVisibleCompleted completed + active entry.
    auto completedCount = size_t { 0 };
    auto activeCount = size_t { 0 };
    for (auto const& entry: _entries)
    {
        if (entry.completion.has_value())
            ++completedCount;
        else
            ++activeCount;
    }

    auto const visibleCompleted = std::min(completedCount, MaxVisibleCompleted);
    auto const visibleEntries = static_cast<int>(visibleCompleted + activeCount);
    return { .width = 0, .height = visibleEntries };
}

void ToolStatusComponent::render(tui::Canvas& canvas)
{
    if (_entries.empty())
        return;

    auto const& theme = canvas.theme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const toolNameStyle = tui::Style { .fg = theme.agentColors.leftBar, .bold = true };
    auto const argsStyle = tui::Style { .fg = theme.agentColors.statusText };
    auto const dimStyle = tui::Style { .fg = theme.agentColors.statusText, .dim = true };
    auto const successStyle = tui::Style { .fg = theme.colors.success };
    auto const errorStyle = tui::Style { .fg = theme.colors.error };
    auto const spinnerStyle = tui::Style { .fg = theme.agentColors.spinnerColor };

    // Collect visible entries: skip oldest completed entries beyond MaxVisibleCompleted.
    auto visibleEntries = std::vector<ToolEntry const*> {};
    auto completedCount = size_t { 0 };
    auto const totalCompleted = static_cast<size_t>(
        std::ranges::count_if(_entries, [](auto const& e) { return e.completion.has_value(); }));
    auto const skipCompleted =
        totalCompleted > MaxVisibleCompleted ? totalCompleted - MaxVisibleCompleted : 0;

    for (auto const& entry: _entries)
    {
        if (entry.completion.has_value())
        {
            ++completedCount;
            if (completedCount <= skipCompleted)
                continue;
        }
        visibleEntries.push_back(&entry);
    }

    auto row = 0;
    for (auto const* entry: visibleEntries)
    {
        if (row >= canvas.height())
            break;

        auto col = 0;

        // Left bar
        col = canvas.putString(row, col, "\u2502 ", barStyle);

        if (entry->completion.has_value())
        {
            // Completed: status icon
            auto const& info = *entry->completion;
            if (info.isError)
                col = canvas.putString(row, col, "\xe2\x9c\x97 ", errorStyle); // ✗
            else
                col = canvas.putString(row, col, "\xe2\x9c\x93 ", successStyle); // ✓

            // Tool name
            col = canvas.putString(row, col, entry->name, toolNameStyle);
            col = canvas.putString(row, col, " ", argsStyle);

            // Args summary
            if (!entry->argsSummary.empty())
                col = canvas.putString(row, col, entry->argsSummary, argsStyle);

            // Right-aligned: duration + size
            auto rightText = formatElapsed(info.duration);
            if (info.isError)
                rightText += "  error";
            else if (info.outputSize > 0)
                rightText += "  " + formatSize(info.outputSize);

            auto const rightCol = canvas.width() - static_cast<int>(rightText.size());
            if (rightCol > col)
                canvas.putString(row, rightCol, rightText, dimStyle);
        }
        else
        {
            // Active: spinner
            col = canvas.putString(row, col, _spinner.currentFrame(), spinnerStyle);
            col = canvas.putString(row, col, " ", argsStyle);

            // Tool name
            col = canvas.putString(row, col, entry->name, toolNameStyle);
            col = canvas.putString(row, col, " ", argsStyle);

            // Args summary
            if (!entry->argsSummary.empty())
                col = canvas.putString(row, col, entry->argsSummary, argsStyle);

            // Right-aligned: live elapsed time
            auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - entry->startTime);
            auto const elapsedText = formatElapsed(elapsed);
            auto const rightCol = canvas.width() - static_cast<int>(elapsedText.size());
            if (rightCol > col)
                canvas.putString(row, rightCol, elapsedText, dimStyle);
        }

        ++row;
    }
}

} // namespace endo::agent
