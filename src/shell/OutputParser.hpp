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
};

} // namespace endo
