// SPDX-License-Identifier: Apache-2.0
#include <tui/HyperlinkEmitter.hpp>
#include <tui/TerminalOutput.hpp>

#include <algorithm>
#include <format>
#include <ranges>

namespace tui
{

HyperlinkIndex::HyperlinkIndex(Buffer const& buffer)
{
    auto const regions = buffer.hyperlinks();
    if (regions.empty())
        return;

    // Copy the regions so the index stays valid even if the buffer is cleared or re-rendered
    // before the flush completes; a frame carries only a handful of them.
    _regions.assign(regions.begin(), regions.end());

    auto maxRow = 0;
    for (auto const& region: _regions)
        maxRow = std::max(maxRow, region.cellArea.bottom());
    _byRow.resize(static_cast<std::size_t>(std::max(0, maxRow)));

    for (auto const& region: _regions)
    {
        auto const firstRow = std::max(0, region.cellArea.y);
        for (auto const row: std::views::iota(firstRow, std::max(firstRow, region.cellArea.bottom())))
            _byRow[static_cast<std::size_t>(row)].push_back(&region);
    }
}

auto HyperlinkIndex::at(int row, int col) const noexcept -> HyperlinkRegion const*
{
    if (row < 0 || std::cmp_greater_equal(row, _byRow.size()))
        return nullptr;

    for (auto const* region: _byRow[static_cast<std::size_t>(row)])
        if (col >= region->cellArea.x && col < region->cellArea.right())
            return region;

    return nullptr;
}

auto HyperlinkIndex::uriAt(int row, int col) const noexcept -> std::string_view
{
    auto const* region = at(row, col);
    return region != nullptr ? std::string_view { region->uri } : std::string_view {};
}

HyperlinkEmitter::HyperlinkEmitter(TerminalOutput& out, HyperlinkIndex const& index) noexcept:
    _out(&out), _index(&index)
{
}

HyperlinkEmitter::~HyperlinkEmitter()
{
    close();
}

void HyperlinkEmitter::beforeWrite(int row, int col)
{
    if (_index->empty() && _open == nullptr)
        return;

    auto const* region = _index->at(row, col);
    if (region == _open)
        return;

    close();

    if (region != nullptr)
    {
        _out->beginHyperlink(region->uri, std::format("endo-{:x}", region->id));
        _open = region;
    }
}

void HyperlinkEmitter::close()
{
    if (_open == nullptr)
        return;

    _out->endHyperlink();
    _open = nullptr;
}

} // namespace tui
