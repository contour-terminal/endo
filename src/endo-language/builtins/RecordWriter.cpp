// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/RecordWriter.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <ranges>
#include <string>

namespace endo::builtins
{

RecordWriter::RecordWriter(CoreVM::Runner* runner, uint16_t typeId):
    _runner(runner), _record(runner->allocObject(typeId))
{
}

CoreVM::FieldInfo const* RecordWriter::claim(std::string_view field)
{
    auto const& fields = _record->type->fields;
    auto const it = std::ranges::find(fields, field, &CoreVM::FieldInfo::name);
    // A name the descriptor does not declare is a producer bug, not a runtime condition: it would
    // otherwise write nothing and leave the field reading back as unset.
    assert(it != fields.end() && "RecordWriter::set: no such field on this record type");
    if (it == fields.end())
        return nullptr;

    _written |= uint64_t { 1 } << static_cast<unsigned>(std::ranges::distance(fields.begin(), it));
    return &*it;
}

RecordWriter& RecordWriter::set(std::string_view field, std::string_view text)
{
    if (auto const* info = claim(field))
    {
        auto const* interned =
            text.empty() ? _runner->emptyString() : _runner->newString(std::string { text });
        _record->setSlot(info->offset, reinterpret_cast<uintptr_t>(interned));
    }
    return *this;
}

RecordWriter& RecordWriter::set(std::string_view field, CoreVM::TypedObject* object)
{
    if (auto const* info = claim(field))
        _record->setSlot(info->offset, reinterpret_cast<uintptr_t>(object));
    return *this;
}

RecordWriter& RecordWriter::setNumber(std::string_view field, int64_t value)
{
    if (auto const* info = claim(field))
        _record->setSlot(info->offset, static_cast<uint64_t>(value));
    return *this;
}

RecordWriter& RecordWriter::setFloat(std::string_view field, double value)
{
    if (auto const* info = claim(field))
        _record->setSlot(info->offset, std::bit_cast<uint64_t>(value));
    return *this;
}

RecordWriter& RecordWriter::setBoolean(std::string_view field, bool value)
{
    if (auto const* info = claim(field))
        _record->setSlot(info->offset, static_cast<uint64_t>(value ? 1 : 0));
    return *this;
}

CoreVM::TypedObject* RecordWriter::record()
{
    // Slots are zero-initialised, which is a valid value for a Number or a Boolean but not for a
    // String: a null CoreString misbehaves on read. Backfilling here means a producer that omits a
    // field it has nothing to say about — `find` has no symlink target — still yields a record every
    // consumer can read.
    auto const& fields = _record->type->fields;
    for (auto const [index, info]: std::views::enumerate(fields))
    {
        if ((_written & (uint64_t { 1 } << static_cast<unsigned>(index))) != 0)
            continue;
        if (info.type == CoreVM::LiteralType::String)
            _record->setSlot(info.offset, reinterpret_cast<uintptr_t>(_runner->emptyString()));
    }
    return _record;
}

} // namespace endo::builtins
