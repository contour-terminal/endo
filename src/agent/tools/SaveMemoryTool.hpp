// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Callback invoked after a memory file is successfully saved.
using MemorySavedCallback = std::function<void()>;

/// Tool for saving agent memory files to persistent storage.
///
/// Writes content to ~/.config/endo/agent-memory/{filename}.md.
/// Creates the memory directory if it does not exist.
/// The saved memory is loaded into the system prompt on next agent session startup.
///
/// Input: { filename: string, content: string }
/// The filename should not include the .md extension (it is appended automatically).
class SaveMemoryTool final: public AgentTool
{
  public:
    /// @brief Constructs a save-memory tool.
    SaveMemoryTool() = default;

    /// @brief Constructs a save-memory tool with a post-save callback.
    /// @param onSaved Callback invoked after a successful save (e.g. to invalidate caches).
    explicit SaveMemoryTool(MemorySavedCallback onSaved);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

    /// @brief Returns the agent memory directory path (~/.config/endo/agent-memory).
    [[nodiscard]] static auto memoryDirectory() -> std::filesystem::path;

  private:
    MemorySavedCallback _onSaved;
};

} // namespace endo::agent
