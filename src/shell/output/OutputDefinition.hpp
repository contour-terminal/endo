// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace endo
{

/// Schema for a single field in an output definition record.
struct OutputFieldSchema
{
    std::string name;                                       ///< Endo field name (e.g., "status")
    std::string sourceKey;                                  ///< JSON key or field index (e.g., "Status")
    CoreVM::LiteralType type = CoreVM::LiteralType::String; ///< Field type (default: String)
};

/// Configuration for how to parse command output.
struct ParserConfig
{
    enum class Type // NOLINT(performance-enum-size)
    {
        Json,   ///< JSON-based parsing (NDJSON lines or JSON array)
        Fields, ///< Delimited fields (e.g., NUL-separated, space-separated)
    };

    enum class Format // NOLINT(performance-enum-size)
    {
        Lines, ///< One record per line (NDJSON or delimited)
        Array, ///< Single JSON array
    };

    Type type = Type::Json;
    Format format = Format::Lines;
    std::string fieldSeparator;   ///< Separator for Fields parser
    std::optional<int> maxFields; ///< Max fields to split into (for Fields parser)
};

/// A single variant of an output definition (e.g., "docker ps" vs "docker images").
struct OutputVariant
{
    std::string name;                              ///< e.g., "ps-json"
    std::vector<std::vector<std::string>> matches; ///< Arg patterns: [["ps"], ["ps", "-a"]]
    int priority = 0;                              ///< Higher priority wins when multiple match
    std::optional<std::string> commandToRun;       ///< Override command template
    ParserConfig parser;                           ///< Parser configuration
    std::vector<OutputFieldSchema> schema;         ///< Record schema
    std::string recordTypeName;                    ///< e.g., "DockerPsRecord"
    std::string fsharpName;                        ///< e.g., "docker_ps" (derived)
    uint16_t assignedTypeId = 0;                   ///< Filled during registration
};

/// An output definition for a base command (e.g., "docker").
struct OutputDefinition
{
    std::string command;                 ///< Base command: "docker"
    std::vector<OutputVariant> variants; ///< All variants for this command
};

} // namespace endo
