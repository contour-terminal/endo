// SPDX-License-Identifier: Apache-2.0
#include "ShellIntegrationSequences.hpp"

#include <core/Base64.hpp>
#include <core/platform/FileUri.hpp>
#include <core/platform/PathUtils.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <ranges>

namespace endo::shellintegration
{

namespace
{
    using QuirkFlag = bool TerminalQuirks::*;

    /// Maps one environment variable to the quirk flag it switches on.
    struct QuirkRow
    {
        std::string_view environmentVariable;
        QuirkFlag flag;
    };

    constexpr auto QuirkTable = std::array {
        QuirkRow { .environmentVariable = "KONSOLE_VERSION", .flag = &TerminalQuirks::suppressPromptParts },
        QuirkRow { .environmentVariable = "TMUX", .flag = &TerminalQuirks::suppressRightPrompt },
        QuirkRow { .environmentVariable = "TMUX", .flag = &TerminalQuirks::insideTmux },
    };

    /// Spells one prompt zone and names the quirks that make the terminal drop it.
    struct ZoneRow
    {
        PromptZone zone;
        std::string_view sequence;
        std::array<QuirkFlag, 2> suppressedBy; // nullptr entries are unused
    };

    constexpr auto ZoneTable = std::array {
        ZoneRow { .zone = PromptZone::Start,
                  .sequence = "\033]133;A;click_events=1\033\\",
                  .suppressedBy = { nullptr, nullptr } },
        ZoneRow { .zone = PromptZone::Prompt,
                  .sequence = "\033]133;P;k=i\033\\",
                  .suppressedBy = { &TerminalQuirks::suppressPromptParts, nullptr } },
        ZoneRow { .zone = PromptZone::Continuation,
                  .sequence = "\033]133;P;k=s\033\\",
                  .suppressedBy = { &TerminalQuirks::suppressPromptParts, nullptr } },
        ZoneRow {
            .zone = PromptZone::RightPrompt,
            .sequence = "\033]133;P;k=r\033\\",
            .suppressedBy = { &TerminalQuirks::suppressPromptParts, &TerminalQuirks::suppressRightPrompt } },
        ZoneRow {
            .zone = PromptZone::Input, .sequence = "\033]133;B\033\\", .suppressedBy = { nullptr, nullptr } },
    };

    constexpr std::string_view St = "\033\\";

} // namespace

TerminalQuirks detectTerminalQuirks(core::Environment const& environment)
{
    auto quirks = TerminalQuirks {};
    for (auto const& row: QuirkTable)
        if (auto const value = environment.get(row.environmentVariable); value && !value->empty())
            quirks.*row.flag = true;
    return quirks;
}

std::string promptZoneSequence(PromptZone zone, TerminalQuirks const& quirks)
{
    auto const* const row = std::ranges::find(ZoneTable, zone, &ZoneRow::zone);
    if (row == ZoneTable.end())
        return {};
    auto const suppressed =
        std::ranges::any_of(row->suppressedBy, [&](QuirkFlag flag) { return flag && quirks.*flag; });
    return suppressed ? std::string {} : std::string { row->sequence };
}

std::string percentEncode(std::string_view bytes)
{
    auto result = std::string {};
    result.reserve(bytes.size());
    for (auto const byte: bytes)
    {
        auto const ch = static_cast<unsigned char>(byte);
        auto const isUnreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
                                  || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_'
                                  || ch == '~' || ch == '/';
        if (isUnreserved)
            result.push_back(byte);
        else
            result += std::format("%{:02X}", static_cast<unsigned>(ch));
    }
    return result;
}

std::string commandStartSequence(std::string_view commandLine)
{
    if (commandLine.empty())
        return std::format("\033]133;C{}", St);
    return std::format("\033]133;C;cmdline_url={}{}", percentEncode(commandLine), St);
}

std::string commandFinishedSequence(int status)
{
    return std::format("\033]133;D;{}{}", status, St);
}

std::string currentDirectorySequence(std::string_view path, std::string_view hostName)
{
    return std::format("\033]7;{}{}",
                       core::platform::fileUri(core::platform::normalizePath(std::string(path)), hostName),
                       St);
}

std::string userVarSequence(std::string_view name, std::string_view value)
{
    return std::format("\033]1337;SetUserVar={}={}{}", name, core::base64::encode(value), St);
}

std::string tmuxPassthrough(std::string_view sequence)
{
    auto result = std::string { "\033Ptmux;" };
    result.reserve(sequence.size() + 16);
    for (auto const ch: sequence)
    {
        if (ch == '\033')
            result.push_back('\033');
        result.push_back(ch);
    }
    result += St;
    return result;
}

} // namespace endo::shellintegration
