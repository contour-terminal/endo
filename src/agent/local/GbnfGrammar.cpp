// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <format>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <agent/local/GbnfGrammar.hpp>

namespace endo::agent::local
{

namespace
{

    /// Shared primitive rules appended once to every generated grammar.
    constexpr std::string_view SharedRules = R"(ws ::= [ \t\n]*
string ::= "\"" [^"]* "\""
number ::= "-"? [0-9]+ ("." [0-9]+)?
integer ::= "-"? [0-9]+
boolean ::= "true" | "false"
null ::= "null"
)";

    /// Recursively emits GBNF rules for a JSON Schema node.
    ///
    /// @param schema The JSON Schema node to convert.
    /// @param ruleName Name to assign to the emitted rule.
    /// @param rules Accumulator for all generated rule lines.
    void emitRules(nlohmann::json const& schema, std::string const& ruleName, std::vector<std::string>& rules)
    {
        // Handle enum types.
        if (schema.contains("enum"))
        {
            auto alternatives = std::string {};
            for (auto const& value: schema["enum"])
            {
                if (!alternatives.empty())
                    alternatives += " | ";
                alternatives += std::format("\"\\\"{}\\\"\"", value.get<std::string>());
            }
            rules.emplace_back(std::format("{} ::= {}", ruleName, alternatives));
            return;
        }

        auto const typeStr = schema.value("type", std::string {});

        if (typeStr == "string")
        {
            rules.emplace_back(std::format("{} ::= string", ruleName));
        }
        else if (typeStr == "number")
        {
            rules.emplace_back(std::format("{} ::= number", ruleName));
        }
        else if (typeStr == "integer")
        {
            rules.emplace_back(std::format("{} ::= integer", ruleName));
        }
        else if (typeStr == "boolean")
        {
            rules.emplace_back(std::format("{} ::= boolean", ruleName));
        }
        else if (typeStr == "null")
        {
            rules.emplace_back(std::format("{} ::= null", ruleName));
        }
        else if (typeStr == "array")
        {
            auto const itemsRuleName = ruleName + "-items";
            if (schema.contains("items"))
                emitRules(schema["items"], itemsRuleName, rules);
            else
                rules.emplace_back(std::format("{} ::= string", itemsRuleName));

            rules.emplace_back(std::format(
                "{} ::= \"[\" ws ({} (\",\" ws {})*)? \"]\"", ruleName, itemsRuleName, itemsRuleName));
        }
        else if (typeStr == "object")
        {
            if (!schema.contains("properties") || schema["properties"].empty())
            {
                rules.emplace_back(std::format("{} ::= \"{{\" ws \"}}\"", ruleName));
                return;
            }

            auto const& properties = schema["properties"];
            auto propertyParts = std::vector<std::string> {};

            for (auto const& [propName, propSchema]: properties.items())
            {
                auto const propRuleName = ruleName + "-" + propName;
                emitRules(propSchema, propRuleName, rules);
                propertyParts.emplace_back(std::format("\"\\\"{}\\\":\" ws {}", propName, propRuleName));
            }

            auto body = std::string {};
            for (auto const& [index, part]: std::views::enumerate(propertyParts))
            {
                if (index > 0)
                    body += " \",\" ws ";
                body += part;
            }

            rules.emplace_back(std::format("{} ::= \"{{\" ws {} ws \"}}\"", ruleName, body));
        }
        else
        {
            // Fallback: accept any string.
            rules.emplace_back(std::format("{} ::= string", ruleName));
        }
    }

} // namespace

auto jsonSchemaToGbnf(nlohmann::json const& schema, std::string_view rootRule) -> std::string
{
    auto rules = std::vector<std::string> {};
    emitRules(schema, std::string(rootRule), rules);

    auto output = std::string {};
    for (auto const& rule: rules)
        output += rule + "\n";
    output += SharedRules;

    return output;
}

auto generateToolCallGrammar(std::span<ToolDefinition const> tools) -> std::string
{
    auto rules = std::vector<std::string> {};
    auto toolJsonAlternatives = std::vector<std::string> {};

    for (auto const& [index, tool]: std::views::enumerate(tools))
    {
        auto const toolRuleName = std::format("tool{}-json", index);
        auto const argsRuleName = std::format("tool{}-args", index);

        // Emit argument rules from the tool's input schema.
        emitRules(tool.inputSchema, argsRuleName, rules);

        // Emit the tool-specific JSON rule: {"name": "toolName", "arguments": <args>}
        rules.emplace_back(
            std::format("{} ::= \"{{\\\"name\\\": \\\"{}\\\"\" \", \\\"arguments\\\": \" {} \"}}\"",
                        toolRuleName,
                        tool.name,
                        argsRuleName));

        toolJsonAlternatives.emplace_back(toolRuleName);
    }

    // Build the tool-json alternatives rule.
    auto toolJsonBody = std::string {};
    for (auto const& [index, alt]: std::views::enumerate(toolJsonAlternatives))
    {
        if (index > 0)
            toolJsonBody += " | ";
        toolJsonBody += alt;
    }

    // Top-level grammar rules.
    auto output = std::string {};
    output += "root ::= text | tool-call\n";
    output += "text ::= [^<]*\n";
    output += "tool-call ::= \"<tool_call>\\n\" tool-json \"\\n</tool_call>\"\n";
    output += std::format("tool-json ::= {}\n", toolJsonBody);

    for (auto const& rule: rules)
        output += rule + "\n";
    output += SharedRules;

    return output;
}

} // namespace endo::agent::local
