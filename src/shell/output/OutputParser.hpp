// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <string_view>

#include "OutputDefinition.hpp"

namespace endo
{

/// Parses command output text into a list of TypedObject records.
///
/// Supports JSON (NDJSON lines or JSON array) and delimited-fields formats.
/// The output is a cons-cell list following the same pattern as PsCommand::execute().
class OutputParser
{
  public:
    /// Parses JSON output (NDJSON lines or JSON array) into a list of typed records.
    /// @param runner  The VM runner (for allocating objects and strings)
    /// @param text    The raw command output text
    /// @param variant The output variant definition (schema, parser config)
    /// @return Pointer to the head of the cons-cell list (Nil if empty)
    static CoreVM::TypedObject* parseJson(CoreVM::Runner& runner,
                                          std::string_view text,
                                          OutputVariant const& variant);

    /// Parses delimited-field output into a list of typed records.
    /// @param runner  The VM runner (for allocating objects and strings)
    /// @param text    The raw command output text
    /// @param variant The output variant definition (schema, parser config)
    /// @return Pointer to the head of the cons-cell list (Nil if empty)
    static CoreVM::TypedObject* parseFields(CoreVM::Runner& runner,
                                            std::string_view text,
                                            OutputVariant const& variant);

    /// Parses a compact schema descriptor string into an OutputVariant.
    /// Format: "name:string,age:int,active:bool" (generated at compile time, consumed at runtime).
    /// @param schemaDesc The schema descriptor string
    /// @param typeId The type ID to assign to the variant
    /// @param parserType The parser type (Json or Fields)
    /// @return OutputVariant ready for use with parseJson()/parseFields()
    static OutputVariant buildVariantFromDesc(std::string_view schemaDesc,
                                              uint16_t typeId,
                                              ParserConfig::Type parserType);

    /// Auto-detects CSV header row by comparing first line fields against schema field names.
    /// @param firstLine The first line of the CSV data
    /// @param separator The field separator
    /// @param schema The expected field schema
    /// @return true if first row appears to be a header (should be skipped)
    static bool detectCsvHeader(std::string_view firstLine,
                                std::string_view separator,
                                std::vector<OutputFieldSchema> const& schema);
};

} // namespace endo
