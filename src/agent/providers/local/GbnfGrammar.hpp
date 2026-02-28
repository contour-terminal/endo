// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>
#include <string_view>

#include <agent/Types.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent::local
{

/// Generates a GBNF grammar that constrains model output to valid tool call JSON.
///
/// The grammar allows the model to output either:
/// - Plain text (no tool call)
/// - A single `<tool_call>{...}</tool_call>` block with valid JSON matching one of the tool schemas
///
/// @param tools Available tool definitions with their input schemas.
/// @return A GBNF grammar string suitable for llama.cpp grammar-constrained generation.
[[nodiscard]] auto generateToolCallGrammar(std::span<ToolDefinition const> tools) -> std::string;

/// Converts a JSON Schema object to GBNF grammar rules.
///
/// @param schema The JSON Schema to convert.
/// @param rootRule Name of the root rule (default: "root").
/// @return A GBNF grammar string.
[[nodiscard]] auto jsonSchemaToGbnf(nlohmann::json const& schema, std::string_view rootRule = "root")
    -> std::string;

} // namespace endo::agent::local
