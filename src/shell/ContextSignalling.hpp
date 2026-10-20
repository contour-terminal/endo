// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endo::shellintegration
{

/// @brief One `key=value` field of an OSC 3008 context sequence.
struct ContextField
{
    std::string_view key; ///< Field name, e.g. `cwd`.
    std::string value;    ///< Raw, unescaped value.
};

/// @brief Builds the sequence opening a context (`OSC 3008 ; start=<id> ; key=value ; ... ST`).
///
/// Values have control characters stripped, `\` encoded as `\x5c` and `;` as `\x3b`. A value whose
/// encoding exceeds 255 bytes is dropped, except `cmdline`, which is truncated on a UTF-8 boundary.
/// @param id The context identifier.
/// @param fields The fields to attach.
/// @return The sequence.
[[nodiscard]] std::string contextStartSequence(std::string_view id, std::span<ContextField const> fields);

/// @brief Builds the sequence closing a context (`OSC 3008 ; end=<id> ; key=value ; ... ST`).
/// @param id The identifier of the context to close.
/// @param fields The fields to attach; encoded like those of contextStartSequence().
/// @return The sequence.
[[nodiscard]] std::string contextEndSequence(std::string_view id, std::span<ContextField const> fields);

/// @brief Creates a context identifier: 128 random bits as 32 lowercase hex digits.
/// @param random Source of random 64-bit values; it is called twice.
/// @return The identifier.
[[nodiscard]] std::string newContextId(std::function<std::uint64_t()> const& random);

/// @brief Outcome of the command whose status a line reports.
struct CommandOutcome
{
    int status = 0;            ///< Exit status as reported by `OSC 133 ; D`.
    std::optional<int> signal; ///< The signal that ended or stopped the command, if any.
    bool stopped = false;      ///< The command was stopped (job control), not finished.
};

/// @brief Classifies a command outcome into the fields of an OSC 3008 `end=` sequence.
/// @param outcome The outcome to classify.
/// @return `exit=`, `status=` and, where the platform names the signal, `signal=`.
[[nodiscard]] std::vector<ContextField> commandEndFields(CommandOutcome const& outcome);

} // namespace endo::shellintegration
