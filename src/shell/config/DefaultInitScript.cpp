// SPDX-License-Identifier: Apache-2.0
#include "DefaultInitScript.hpp"

#include <endo-language/builtins/PropertyDescriptors.hpp>

#include <tui/EditAction.hpp>
#include <tui/KeyBindings.hpp>

#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace endo
{

namespace
{

    /// Renders the (short) first sentence of a property's description.
    /// `PropertyDescriptor::description` is already terse, so we just trim it.
    [[nodiscard]] std::string_view briefDescription(PropertyDescriptor const& p) noexcept
    {
        return p.description;
    }

    /// Returns a type-appropriate placeholder literal used in the commented-out
    /// example assignment. For enum-valued string properties the first enum
    /// entry is used so users see a concrete example they can toggle.
    [[nodiscard]] std::string placeholderFor(PropertyDescriptor const& p)
    {
        using T = CoreVM::LiteralType;
        if (!p.enumValues.empty() && p.type == T::String)
            return std::format("\"{}\"", p.enumValues.front().value);
        switch (p.type)
        {
            case T::String: return "\"\"";
            case T::Number: return "0";
            case T::Boolean: return "false";
            default: return "\"\"";
        }
    }

    /// Writes one "# " comment block plus a commented-out assignment line for
    /// a single property. The assignment is commented so the file is
    /// behaviorally inert on first run — the baked-in C++ defaults win.
    void appendPropertyBlock(std::string& out, PropertyDescriptor const& p)
    {
        out += std::format("# {}\n", briefDescription(p));
        if (!p.enumValues.empty())
        {
            out += "# Values:";
            for (auto const& [i, e]: std::views::enumerate(p.enumValues))
                out += std::format("{} {}", i == 0 ? "" : " |", e.value);
            out += '\n';
        }
        if (p.readOnly)
            out += std::format("# (read-only) print {}\n", p.name);
        else
            out += std::format("# {} <- {}\n", p.name, placeholderFor(p));
        out += "#\n";
    }

    /// Emits a section header as an `.endo` comment banner.
    void appendSection(std::string& out, std::string_view title)
    {
        out += "# ---------------------------------------------------------------------------\n";
        out += std::format("# {}\n", title);
        out += "# ---------------------------------------------------------------------------\n";
    }

    /// Renders the header comment block shown at the top of the file.
    void appendHeader(std::string& out)
    {
        out += "# ~/.config/endo/init.endo\n";
        out += "#\n";
        out += "# This file was auto-generated on first run because no init.endo existed.\n";
        out += "# It documents every shell property you can configure. All assignments are\n";
        out += "# commented out by default — uncomment and edit the lines you care about.\n";
        out += "#\n";
        out += "# To regenerate this file from the current Endo defaults, delete it and\n";
        out += "# start a new interactive Endo session; a fresh copy will be written.\n";
        out += "# To skip loading this file on startup, pass `--no-profile` to endo.\n";
        out += "#\n";
        out += "# Full reference: https://endo-shell.dev/shell/configuration/\n";
    }

    /// Appends a reference section listing the default key bindings. Purely
    /// informational — there is currently no script-level API to rebind keys
    /// from within init.endo (see `bind` builtin for interactive use).
    void appendKeyBindingsReference(std::string& out)
    {
        appendSection(out, "Default key bindings (reference only)");
        out += "# Endo ships with these key bindings out of the box. There is no\n";
        out += "# `.endo` setter API for bindings yet; use the `bind` builtin at the\n";
        out += "# prompt to view, add, or remove bindings interactively. See:\n";
        out += "#   https://endo-shell.dev/shell/configuration/#key-bindings\n";
        out += "#\n";

        auto const defaults = tui::KeyBindings::defaults();
        for (auto const& [chord, action]: defaults.bindings())
            out += std::format("#   {:<20} -> {}\n", chord.toString(), tui::editActionToString(action));
    }

    /// Appends a short pattern-matching demo tailored to show off the
    /// language: capture `$(uname -s)` and branch on it. The block is
    /// commented out so it has no runtime effect unless the user enables it.
    void appendPatternMatchingExample(std::string& out)
    {
        appendSection(out, "Example: per-OS customization with pattern matching");
        out += "# Capture the OS name from the `uname` builtin and pick per-platform\n";
        out += "# settings. This demonstrates command substitution plus F#-style\n";
        out += "# pattern matching — uncomment to enable.\n";
        out += "#\n";
        out += "# let os = $(uname -s)\n";
        out += "# match os with\n";
        out += "# | \"Linux\"  -> shell_prompt_indicator <- \"🐧 \"\n";
        out += "# | \"Darwin\" -> shell_prompt_indicator <- \"🍎 \"\n";
        out += "# | _        -> shell_prompt_indicator <- \"|> \"\n";
    }

} // namespace

std::string generateDefaultInitEndo()
{
    std::string out;
    out.reserve(8 * 1024);

    appendHeader(out);

    appendSection(out, "Prompt & shell configuration");
    for (auto const& p: promptPropertyDescriptors())
        appendPropertyBlock(out, p);

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    appendSection(out, "AI agent configuration");
    out += "# These settings only take effect when Endo is built with agent support.\n";
    out += "#\n";
    for (auto const& p: agentPropertyDescriptors())
        appendPropertyBlock(out, p);
#endif

    appendKeyBindingsReference(out);
    appendPatternMatchingExample(out);

    return out;
}

} // namespace endo
