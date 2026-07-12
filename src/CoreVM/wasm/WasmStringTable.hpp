// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/wasm/WasmRuntimeABI.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CoreVM::wasm
{

/// Interns string constants into the module's data segment.
///
/// Each string is stored with the unified 8-byte cell header (kind, tag,
/// typeId, byte length) followed by its UTF-8 bytes, 8-byte aligned, starting
/// at layout::DataBase. Interning is content-deduplicating, so equal literals
/// share one cell. Both the code generator (user string literals) and the
/// runtime provider (diagnostic messages) intern through the same table.
class WasmStringTable
{
  public:
    /// Interns @p text and returns its pointer (address of the header base).
    uint32_t intern(std::string_view text);

    /// First address past the interned data (>= layout::DataBase).
    [[nodiscard]] uint32_t dataEnd() const noexcept
    {
        return layout::DataBase + static_cast<uint32_t>(_blob.size());
    }

    /// The raw data-segment contents, to be placed at layout::DataBase.
    [[nodiscard]] std::span<uint8_t const> blob() const noexcept { return _blob; }

    [[nodiscard]] bool empty() const noexcept { return _blob.empty(); }

  private:
    std::vector<uint8_t> _blob;
    std::unordered_map<std::string, uint32_t> _offsets;
};

} // namespace CoreVM::wasm
