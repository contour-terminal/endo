// SPDX-License-Identifier: Apache-2.0
#include "OutputDefinitionRegistry.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <ranges>

namespace endo
{

namespace
{
    CoreVM::LiteralType parseFieldType(std::string const& typeStr)
    {
        if (typeStr == "int" || typeStr == "number")
            return CoreVM::LiteralType::Number;
        if (typeStr == "bool" || typeStr == "boolean")
            return CoreVM::LiteralType::Boolean;
        // Default to string
        return CoreVM::LiteralType::String;
    }

    /// Capitalizes the first letter and lowercases the rest.
    std::string capitalize(std::string_view s)
    {
        if (s.empty())
            return {};
        std::string result(s);
        result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
        return result;
    }
} // namespace

void OutputDefinitionRegistry::loadFromDirectory(std::filesystem::path const& dir, FileSystem const& fs)
{
    if (!fs.exists(dir) || !fs.isDirectory(dir))
        return;

    auto const entries = fs.listDirectory(dir);
    if (!entries)
        return;

    for (auto const& entry: *entries)
    {
        if (entry.isRegularFile && entry.path.string().ends_with(".endo-output.yml"))
            loadFromFile(entry.path, fs);
    }
}

bool OutputDefinitionRegistry::loadFromFile(std::filesystem::path const& path, FileSystem const& fs)
{
    try
    {
        auto const content = fs.readFile(path);
        if (!content)
            return false;

        auto const root = YAML::Load(*content);
        if (!root["command"])
            return false;

        OutputDefinition def;
        def.command = root["command"].as<std::string>();

        if (auto const variants = root["variants"]; variants && variants.IsSequence())
        {
            for (auto const& v: variants)
            {
                OutputVariant variant;
                variant.name = v["name"] ? v["name"].as<std::string>() : "";
                variant.priority = v["priority"] ? v["priority"].as<int>() : 0;

                if (v["command_to_run"])
                    variant.commandToRun = v["command_to_run"].as<std::string>();

                // Parse matches
                if (auto const matches = v["matches"]; matches && matches.IsSequence())
                {
                    for (auto const& match: matches)
                    {
                        std::vector<std::string> pattern;
                        if (match.IsSequence())
                            for (auto const& m: match)
                                pattern.push_back(m.as<std::string>());
                        variant.matches.push_back(std::move(pattern));
                    }
                }

                // Parse parser config
                if (auto const parser = v["parser"]; parser)
                {
                    if (parser["type"])
                    {
                        auto const typeStr = parser["type"].as<std::string>();
                        variant.parser.type =
                            typeStr == "fields" ? ParserConfig::Type::Fields : ParserConfig::Type::Json;
                    }
                    if (parser["format"])
                    {
                        auto const formatStr = parser["format"].as<std::string>();
                        variant.parser.format =
                            formatStr == "array" ? ParserConfig::Format::Array : ParserConfig::Format::Lines;
                    }
                    if (parser["field_separator"])
                    {
                        auto sep = parser["field_separator"].as<std::string>();
                        // Handle escape sequences
                        if (sep == "\\x00" || sep == "\\0")
                            sep = std::string(1, '\0');
                        variant.parser.fieldSeparator = std::move(sep);
                    }
                    if (parser["max_fields"])
                        variant.parser.maxFields = parser["max_fields"].as<int>();
                }

                // Parse schema
                if (auto const schema = v["schema"]; schema && schema.IsSequence())
                {
                    for (auto const& field: schema)
                    {
                        OutputFieldSchema fieldSchema;
                        fieldSchema.name = field["name"] ? field["name"].as<std::string>() : "";
                        fieldSchema.sourceKey =
                            field["source_key"] ? field["source_key"].as<std::string>() : "";
                        if (field["type"])
                            fieldSchema.type = parseFieldType(field["type"].as<std::string>());
                        variant.schema.push_back(std::move(fieldSchema));
                    }
                }

                // Derive record type name: capitalize(command) + capitalize(variant.name) + "Record"
                variant.recordTypeName = capitalize(def.command) + capitalize(variant.name) + "Record";

                // Derive fsharpName: command_variant (e.g., "docker_ps")
                variant.fsharpName = def.command + "_" + variant.name;

                def.variants.push_back(std::move(variant));
            }
        }

        _definitions.push_back(std::move(def));
        return true;
    }
    catch (YAML::Exception const&)
    {
        return false;
    }
}

OutputVariant const* OutputDefinitionRegistry::findMatch(std::string const& command,
                                                         std::vector<std::string> const& args) const
{
    OutputVariant const* bestMatch = nullptr;
    int bestPriority = -1;

    for (auto const& def: _definitions)
    {
        if (def.command != command)
            continue;

        for (auto const& variant: def.variants)
        {
            for (auto const& pattern: variant.matches)
            {
                if (matchesPattern(args, pattern) && variant.priority > bestPriority)
                {
                    bestMatch = &variant;
                    bestPriority = variant.priority;
                }
            }
        }
    }

    return bestMatch;
}

std::vector<OutputVariant const*> OutputDefinitionRegistry::allVariants() const
{
    std::vector<OutputVariant const*> result;
    for (auto const& def: _definitions)
        for (auto const& variant: def.variants)
            result.push_back(&variant);
    return result;
}

bool OutputDefinitionRegistry::matchesPattern(std::vector<std::string> const& args,
                                              std::vector<std::string> const& pattern)
{
    if (args.size() < pattern.size())
        return false;

    for (size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern[i] == "*")
            continue;
        if (args[i] != pattern[i])
            return false;
    }

    return true;
}

} // namespace endo
