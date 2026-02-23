// SPDX-License-Identifier: Apache-2.0
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include <agent/RunCommand.hpp>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace endo::agent
{

auto parseAgentRunArgs(std::span<char const* const> args) -> std::expected<AgentRunOptions, std::string>
{
    auto options = AgentRunOptions {};
    auto promptParts = std::vector<std::string> {};
    auto filePrompt = std::optional<std::string> {};

    for (size_t i = 0; i < args.size(); ++i)
    {
        auto const arg = std::string_view(args[i]);

        if (arg == "--json")
        {
            options.jsonOutput = true;
        }
        else if (arg == "--auto-approve")
        {
            options.autoApprove = true;
        }
        else if (arg == "--max-turns")
        {
            if (++i >= args.size())
                return std::unexpected("--max-turns requires a value"s);
            auto const value = std::string_view(args[i]);
            size_t parsed = 0;
            auto const [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (ec != std::errc {} || ptr != value.data() + value.size() || parsed == 0)
                return std::unexpected("--max-turns: invalid number '" + std::string(value) + "'");
            options.maxTurns = parsed;
        }
        else if (arg == "--provider")
        {
            if (++i >= args.size())
                return std::unexpected("--provider requires a value"s);
            options.provider = std::string(args[i]);
        }
        else if (arg == "--model")
        {
            if (++i >= args.size())
                return std::unexpected("--model requires a value"s);
            options.model = std::string(args[i]);
        }
        else if (arg == "--file" || arg == "-f")
        {
            if (++i >= args.size())
                return std::unexpected(std::string(arg) + " requires a file path");
            auto const filePath = std::filesystem::path(args[i]);
            if (!std::filesystem::exists(filePath))
                return std::unexpected("File not found: " + filePath.string());
            auto ifs = std::ifstream(filePath);
            if (!ifs)
                return std::unexpected("Cannot read file: " + filePath.string());
            filePrompt = std::string(std::istreambuf_iterator<char>(ifs), {});
        }
        else if (arg.starts_with("--"))
        {
            return std::unexpected("Unknown option: " + std::string(arg));
        }
        else
        {
            promptParts.emplace_back(arg);
        }
    }

    // Build prompt: file takes precedence, positional args are concatenated with spaces
    if (filePrompt.has_value())
    {
        options.prompt = std::move(*filePrompt);
    }
    else if (!promptParts.empty())
    {
        for (size_t i = 0; i < promptParts.size(); ++i)
        {
            if (i > 0)
                options.prompt += ' ';
            options.prompt += promptParts[i];
        }
    }
    else
    {
        return std::unexpected("No prompt provided. Usage: endo agent run \"<prompt>\" [OPTIONS]"s);
    }

    return options;
}

} // namespace endo::agent
