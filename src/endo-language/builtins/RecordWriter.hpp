// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file RecordWriter.hpp
/// @brief Writes builtin record slots by field name rather than by slot number.

#include <CoreVM/CoreVM.hpp>

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace endo::builtins
{

/// @brief Fills a freshly allocated builtin record, addressing fields by name.
///
/// Every producer of a builtin record used to spell its type's slot numbers itself — `ls`, `find`,
/// `ps`, `jobs`, `bind` and the test mock each carried their own copy of a layout declared in
/// CoreVM's TypeRegistry. Two things went wrong with that, both silently: a field could land in the
/// wrong slot after a reordering, and a field could be left unwritten, which is how `find` shipped
/// FileInfo records whose `target` slot read back as a null CoreString.
///
/// Offsets here come from the type's descriptor, and record() backfills every String field the
/// producer did not set with the runner's shared empty-string sentinel, so neither failure is
/// reachable. Name lookup is a short linear scan over the descriptor's fields — negligible next to
/// the allocation each record already costs.
///
/// Usage:
/// @code
/// auto* record = RecordWriter { &runner, CoreVM::BuiltinTypeId::JobInfo }
///                    .set("id", job.id)
///                    .set("state", job.state)
///                    .record();
/// @endcode
class RecordWriter
{
  public:
    /// @brief Allocates a record of @p typeId to be filled.
    /// @param runner Runner owning the object pool and string arena. Must outlive this writer.
    /// @param typeId The builtin record type to allocate, e.g. CoreVM::BuiltinTypeId::JobInfo.
    RecordWriter(CoreVM::Runner* runner, uint16_t typeId);

    /// @brief Sets a String field, interning @p text in the runner's arena.
    ///
    /// Empty text uses the shared empty-string sentinel rather than allocating.
    RecordWriter& set(std::string_view field, std::string_view text);

    /// @brief Sets an Object field to an already-allocated record.
    RecordWriter& set(std::string_view field, CoreVM::TypedObject* object);

    /// @brief Sets an integral (Number) field.
    template <std::integral T>
        requires(!std::same_as<std::remove_cv_t<T>, bool>)
    RecordWriter& set(std::string_view field, T value)
    {
        return setNumber(field, static_cast<int64_t>(value));
    }

    /// @brief Sets a Float field.
    template <std::floating_point T>
    RecordWriter& set(std::string_view field, T value)
    {
        return setFloat(field, static_cast<double>(value));
    }

    /// @brief Sets a Boolean field.
    ///
    /// Constrained rather than a plain `bool` parameter: an overload taking `bool` would win
    /// against std::string_view for a `char const*` argument, silently storing 1 for a name.
    template <std::same_as<bool> T>
    RecordWriter& set(std::string_view field, T value)
    {
        return setBoolean(field, value);
    }

    /// @brief Returns the finished record, backfilling unset String fields.
    /// @return The record. Owned by the runner's object pool, as every allocObject() result is.
    [[nodiscard]] CoreVM::TypedObject* record();

  private:
    RecordWriter& setNumber(std::string_view field, int64_t value);
    RecordWriter& setFloat(std::string_view field, double value);
    RecordWriter& setBoolean(std::string_view field, bool value);

    /// @brief Resolves @p field and marks it written; returns nullptr for an unknown name.
    CoreVM::FieldInfo const* claim(std::string_view field);

    /// Slot offsets this writer can track in _written. Builtin records declare far fewer slots;
    /// a field past this is simply always backfilled, which is the safe direction.
    static constexpr uint8_t SlotBits = 64;

    CoreVM::Runner* _runner;
    CoreVM::TypedObject* _record;
    uint64_t _written = 0; ///< Bit per written slot offset, so record() knows what to backfill.
};

} // namespace endo::builtins
