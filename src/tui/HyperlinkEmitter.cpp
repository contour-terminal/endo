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
    _regions.assign(regions.begin(), regions.end());
}

auto HyperlinkIndex::at(int row, int col) const noexcept -> HyperlinkRegion const*
{
    auto const covers = [row, col](HyperlinkRegion const& region) {
        return row >= region.cellArea.y && row < region.cellArea.bottom() && col >= region.cellArea.x
               && col < region.cellArea.right();
    };
    auto const it = std::ranges::find_if(_regions, covers);
    return it != _regions.end() ? &*it : nullptr;
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

FlushLinks::FlushLinks(TerminalOutput& out, Buffer const& currentBuffer, Buffer const* previousBuffer):
    current(currentBuffer),
    previous(previousBuffer != nullptr ? HyperlinkIndex { *previousBuffer } : HyperlinkIndex {}),
    emitter(out, current),
    any(!current.empty() || !previous.empty())
{
}

} // namespace tui
