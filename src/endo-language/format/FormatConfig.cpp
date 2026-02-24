// SPDX-License-Identifier: Apache-2.0
#include <endo-language/format/FormatConfig.hpp>

#include <fstream>
#include <sstream>

namespace endo::format
{

std::expected<FormatConfig, std::string> FormatConfig::load(std::filesystem::path const& path)
{
    std::ifstream file(path);
    if (!file)
        return std::unexpected(std::string("Cannot open config file: ") + path.string());

    FormatConfig config;
    std::string line;

    while (std::getline(file, line))
    {
        // Skip comments and empty lines
        auto const trimStart = line.find_first_not_of(" \t");
        if (trimStart == std::string::npos || line[trimStart] == '#')
            continue;

        auto const colonPos = line.find(':');
        if (colonPos == std::string::npos)
            continue;

        auto key = line.substr(trimStart, colonPos - trimStart);
        auto value = line.substr(colonPos + 1);

        // Trim whitespace from key and value
        auto const keyEnd = key.find_last_not_of(" \t");
        if (keyEnd != std::string::npos)
            key = key.substr(0, keyEnd + 1);

        auto const valStart = value.find_first_not_of(" \t");
        if (valStart != std::string::npos)
            value = value.substr(valStart);
        auto const valEnd = value.find_last_not_of(" \t\r\n");
        if (valEnd != std::string::npos)
            value = value.substr(0, valEnd + 1);

        if (key == "indentWidth" || key == "indent_width")
            config.indentWidth = static_cast<uint32_t>(std::stoul(value));
        else if (key == "useSpaces" || key == "use_spaces")
            config.useSpaces = (value == "true" || value == "yes" || value == "1");
        else if (key == "maxLineWidth" || key == "max_line_width")
            config.maxLineWidth = static_cast<uint32_t>(std::stoul(value));
        else if (key == "trailingNewline" || key == "trailing_newline")
            config.trailingNewline = (value == "true" || value == "yes" || value == "1");
        else if (key == "blankLinesBetweenTopLevel" || key == "blank_lines_between_top_level")
            config.blankLinesBetweenTopLevel = static_cast<uint32_t>(std::stoul(value));
    }

    return config;
}

std::optional<std::filesystem::path> FormatConfig::findConfigFile(std::filesystem::path const& startDir)
{
    auto dir = std::filesystem::absolute(startDir);

    while (true)
    {
        auto const candidate = dir / ".endo-format";
        if (std::filesystem::exists(candidate))
            return candidate;

        auto const parent = dir.parent_path();
        if (parent == dir)
            break; // Reached filesystem root
        dir = parent;
    }

    return std::nullopt;
}

std::string FormatConfig::indentString() const
{
    if (useSpaces)
        return std::string(indentWidth, ' ');
    return "\t";
}

} // namespace endo::format
