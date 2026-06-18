// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file DefaultInitScript.hpp
/// @brief Renders a fully-documented default `init.endo` template from the
///        single source-of-truth C++ descriptor tables.
///
/// Auto-created on first run by `Shell::loadInitScript()` when the user has
/// no `init.endo` yet. The generated file documents every configurable
/// shell property and ships commented-out example assignments so users can
/// uncomment and tweak them without hunting through docs. Because all
/// assignments are commented by default, regenerating the file is
/// behaviorally inert — the baked-in C++ defaults remain authoritative.

#include <string>

namespace endo
{

/// @brief Returns the rendered default `init.endo` file contents.
///
/// The output is derived from:
///   - `promptPropertyDescriptors()` / `agentPropertyDescriptors()` — property
///     names, types, descriptions, detailed docs, and enum values.
///   - `tui::KeyBindings::defaults()` — default key-binding reference block.
///
/// The rendered script is valid `.endo` syntax (comment-only by default) and
/// is expected to round-trip through `endo format --check`.
///
/// @return A UTF-8 string holding the complete file contents, including a
///         trailing newline.
[[nodiscard]] std::string generateDefaultInitEndo();

} // namespace endo
