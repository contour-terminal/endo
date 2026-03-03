// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file TraceTerminalRenderer.hpp
/// @brief Renders agent trace events as formatted ANSI terminal output.

#include <agent/tracing/TraceEvent.hpp>

namespace tui
{
class TerminalOutput;
}

namespace endo::agent
{

/// @brief Renders a trace event to a tui::TerminalOutput (interactive mode).
/// @param out The terminal output to write to.
/// @param event The trace event to render.
void renderTraceEvent(tui::TerminalOutput& out, TraceEvent const& event);

/// @brief Renders a trace event to stderr using raw ANSI sequences (headless mode).
/// @param event The trace event to render.
void renderTraceEventToStderr(TraceEvent const& event);

} // namespace endo::agent
