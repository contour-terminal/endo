// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/wasm/WasmStringTable.hpp>

namespace CoreVM::wasm
{

uint32_t WasmStringTable::intern(std::string_view text)
{
    if (auto const it = _offsets.find(text); it != _offsets.end())
        return it->second;

    auto const pointer = dataEnd();

    // Unified cell header: kind, tag, typeId (u16 LE), byte length (u32 LE).
    _blob.push_back(layout::KindString);
    _blob.push_back(0); // tag
    _blob.push_back(0); // typeId lo
    _blob.push_back(0); // typeId hi
    auto const length = static_cast<uint32_t>(text.size());
    for (auto const shift: { 0U, 8U, 16U, 24U })
        _blob.push_back(static_cast<uint8_t>(length >> shift));

    _blob.insert(_blob.end(), text.begin(), text.end());
    while (_blob.size() % 8 != 0)
        _blob.push_back(0);

    _offsets.emplace(std::string(text), pointer);
    return pointer;
}

} // namespace CoreVM::wasm
