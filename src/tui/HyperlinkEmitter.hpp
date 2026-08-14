// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file HyperlinkEmitter.hpp
/// @brief Emits OSC 8 framing around a buffer's hyperlink regions while its cells flush.

#include <tui/Buffer.hpp>

#include <string_view>
#include <vector>

namespace tui
{

class TerminalOutput;

/// @brief A per-row index over a buffer's hyperlink regions, for cell lookup during flush.
///
/// Flushing tests every cell, so the regions are bucketed by row once up front rather than
/// scanned linearly per cell. A buffer normally carries at most a handful of regions, so the
/// per-row bucket is tiny and a linear scan within it is the right trade.
class HyperlinkIndex
{
  public:
    /// @brief Builds an empty index that reports every cell as unlinked.
    HyperlinkIndex() = default;

    /// @brief Builds an index over @p buffer's regions.
    /// @param buffer The buffer being flushed. Only its regions are read, during construction.
    explicit HyperlinkIndex(Buffer const& buffer);

    /// @brief Returns the region covering (@p row, @p col), or nullptr when unlinked.
    [[nodiscard]] HyperlinkRegion const* at(int row, int col) const noexcept;

    /// @brief Returns the URI covering (@p row, @p col), or an empty view when unlinked.
    [[nodiscard]] std::string_view uriAt(int row, int col) const noexcept;

    /// @brief Whether this index holds no regions at all.
    [[nodiscard]] bool empty() const noexcept { return _regions.empty(); }

  private:
    /// Regions, copied by pointer into per-row buckets. Indexed by row.
    std::vector<std::vector<HyperlinkRegion const*>> _byRow;
    std::vector<HyperlinkRegion> _regions;
};

/// @brief Keeps the terminal's open OSC 8 link in sync with a buffer's regions during flush.
///
/// The link is opened lazily, immediately before the first cell written inside a region, and
/// closed before the first write outside it. Opening lazily is what makes this correct under
/// a cell-diffing renderer: cells the diff skips are never bracketed, so an unchanged link
/// costs nothing, and no cell can be attributed to a link it does not belong to.
///
/// Skipped cells keep the link the terminal recorded when they were last written, which is
/// exactly the link that would have been re-emitted. Detecting a region whose URI changed
/// while its cells did not is the caller's job — compare HyperlinkIndex::uriAt() against the
/// previous frame's index in the diff condition, since Cell equality cannot see an
/// out-of-band URI.
class HyperlinkEmitter
{
  public:
    /// @brief Constructs an emitter writing to @p out for the regions in @p index.
    /// @param out Sink receiving the OSC 8 framing. Must outlive this emitter.
    /// @param index Index over the buffer being flushed. Must outlive this emitter.
    HyperlinkEmitter(TerminalOutput& out, HyperlinkIndex const& index) noexcept;

    /// @brief Closes any still-open link, so framing can never leak past a flush.
    ~HyperlinkEmitter();

    HyperlinkEmitter(HyperlinkEmitter const&) = delete;
    HyperlinkEmitter(HyperlinkEmitter&&) = delete;
    auto operator=(HyperlinkEmitter const&) -> HyperlinkEmitter& = delete;
    auto operator=(HyperlinkEmitter&&) -> HyperlinkEmitter& = delete;

    /// @brief Synchronizes link state with the region covering (@p row, @p col).
    ///
    /// Call immediately before writing that cell's grapheme.
    void beforeWrite(int row, int col);

    /// @brief Closes any open link. Idempotent.
    ///
    /// Call at the end of a row and before any cursor movement or erase that should not be
    /// attributed to the link.
    void close();

  private:
    TerminalOutput* _out;
    HyperlinkIndex const* _index;
    HyperlinkRegion const* _open = nullptr;
};

} // namespace tui
