// SPDX-License-Identifier: Apache-2.0
#include "PromptComponent.hpp"

#include <endo-language/HoverProvider.hpp>

#include <tui/Canvas.hpp>
#include <tui/Screen.hpp>
#include <tui/Sixel.hpp>
#include <tui/Theme.hpp>
#include <tui/Unicode.hpp>
#include <tui/completer/Completer.hpp>

#include <algorithm>

#include "CommandResolver.hpp"
#include "Completer.hpp"
#include "Gradient.hpp"
#include "History.hpp"
#include "SourceOffsetUtils.hpp"
#include "SyntaxHighlighter.hpp"
#include "modules/BatteryModule.hpp"
#include "modules/ClockModule.hpp"
#include "modules/DurationModule.hpp"
#include "modules/ExitStatusModule.hpp"
#include "modules/FSharpModeModule.hpp"
#include "modules/GitModule.hpp"
#include "modules/HostnameModule.hpp"
#include "modules/IndicatorModule.hpp"
#include "modules/PathModule.hpp"
#include "modules/StructuredOutputModule.hpp"
#include "modules/ToolchainModule.hpp"

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

using tui::operator""_rgb;

namespace endo
{

// ============================================================================
// PromptTextDecorator implementation
// ============================================================================

auto PromptComponent::PromptTextDecorator::foreground(tui::TextPosition pos) const
    -> std::optional<tui::RgbColor>
{
    if (highlightMap && pos.graphemeIndex < highlightMap->size() && theme)
        return categoryColor((*highlightMap)[pos.graphemeIndex], *theme);
    return {};
}

auto PromptComponent::PromptTextDecorator::underline(tui::TextPosition pos) const
    -> std::optional<UnderlineDecoration>
{
    if (errorMap && pos.graphemeIndex < errorMap->size() && (*errorMap)[pos.graphemeIndex])
    {
        using tui::operator""_rgb;
        return UnderlineDecoration { .style = tui::UnderlineStyle::Curly, .color = 0xC0C000_rgb };
    }
    return {};
}

auto PromptComponent::PromptTextDecorator::background(int displayCol) const -> std::optional<tui::RgbColor>
{
    auto const idx = displayCol + bgOffset;
    if (bgColors && !bgColors->empty() && idx >= 0 && idx < static_cast<int>(bgColors->size()))
        return (*bgColors)[static_cast<std::size_t>(idx)];
    return flatBg;
}

// ============================================================================
// PromptComponent
// ============================================================================

PromptComponent::PromptComponent()
{
    _inputField.setPrompt(""); // We handle prompt rendering ourselves
    initializeModules();
}

void PromptComponent::initializeModules()
{
    auto add = [this](std::unique_ptr<PromptModule> mod) {
        auto const id = std::string(mod->id());
        _modules[id] = std::move(mod);
    };

    add(std::make_unique<PathModule>());
    add(std::make_unique<GitModule>());
    add(std::make_unique<ExitStatusModule>());
    add(std::make_unique<DurationModule>(_config.durationThresholdMs));
    add(std::make_unique<HostnameModule>());
    add(std::make_unique<ClockModule>());
    add(std::make_unique<BatteryModule>());
    add(std::make_unique<FSharpModeModule>());
    add(std::make_unique<StructuredOutputModule>());
    add(std::make_unique<ToolchainModule>());
    add(std::make_unique<IndicatorModule>(_config.indicator));
}

std::vector<PromptSegments> PromptComponent::evaluateModules(
    std::vector<std::string> const& moduleNames) const
{
    auto results = std::vector<PromptSegments> {};
    for (auto const& name: moduleNames)
    {
        auto it = _modules.find(name);
        if (it == _modules.end())
            continue;

        if (!it->second->shouldShow(_context))
            continue;

        auto segments = it->second->evaluate(_context);
        if (!segments.empty())
        {
            // Apply gradient to path module when configured
            if (_config.useGradientPath && name == "path")
            {
                std::string pathText;
                for (auto const& seg: segments)
                    pathText += seg.text;
                segments = gradient(_config.gradientStart, _config.gradientEnd, pathText);
                for (auto& seg: segments)
                    seg.style.bold = true;
            }
            results.push_back(std::move(segments));
        }
    }
    return results;
}

void PromptComponent::setPromptConfig(PromptConfig config)
{
    _config = std::move(config);
    _auroraFadeCacheWidth = 0; // Invalidate sixel fade cache

    // Update indicator module
    if (auto it = _modules.find("indicator"); it != _modules.end())
    {
        if (auto* ind = dynamic_cast<IndicatorModule*>(it->second.get()))
            ind->setIndicator(_config.indicator);
    }

    // Update duration module threshold
    if (auto it = _modules.find("duration"); it != _modules.end())
    {
        // Recreate with new threshold
        _modules["duration"] = std::make_unique<DurationModule>(_config.durationThresholdMs);
    }
}

void PromptComponent::setPromptContext(PromptContext context)
{
    // Invalidate sixel fade cache if cell pixel dimensions changed
    if (context.cellPixelWidth != _context.cellPixelWidth
        || context.cellPixelHeight != _context.cellPixelHeight)
        _auroraFadeCacheWidth = 0;

    _context = std::move(context);
    _moduleCacheValid = false; // Invalidate module cache on context change
}

void PromptComponent::render(tui::Canvas& canvas)
{
    auto const& theme = tui::currentTheme();
    auto const canvasWidth = canvas.width();
    auto const totalLines = _inputField.lineCount();
    auto const promptTextWidth = displayWidth(_promptStr);
    auto const totalPromptWidth = HorizontalMargin + leftBarWidth() + PaddingAfterBar + promptTextWidth;

    // Effective content width (excluding margins)
    auto const contentWidth = canvasWidth - 2 * HorizontalMargin;

    // Use theme-based colors
    auto const& pc = theme.promptColors;

    // Build aurora background color cache when configured
    auto const hasAurora = !_config.auroraBackground.empty();
    auto bgColors = std::vector<tui::RgbColor> {};
    if (hasAurora && contentWidth > 0)
    {
        bgColors.resize(static_cast<std::size_t>(contentWidth));
        for (int c = 0; c < contentWidth; ++c)
        {
            auto const t = static_cast<float>(c) / static_cast<float>(std::max(contentWidth - 1, 1));
            bgColors[static_cast<std::size_t>(c)] = multiStopGradient(_config.auroraBackground, t);
        }
    }
    /// @brief Returns the aurora background color at the given absolute column,
    /// or falls back to the flat theme background.
    auto const bgAt = [&](int col) -> tui::RgbColor {
        auto const idx = col - HorizontalMargin;
        if (!bgColors.empty() && idx >= 0 && idx < static_cast<int>(bgColors.size()))
            return bgColors[static_cast<std::size_t>(idx)];
        return pc.background;
    };

    // Create styles
    tui::Style bgStyle;
    bgStyle.bg = pc.background;

    tui::Style leftBarStyle;
    leftBarStyle.fg = pc.separator;
    leftBarStyle.bg = pc.background;

    tui::Style promptStyle;
    promptStyle.fg = pc.badgeText;
    promptStyle.bg = pc.background;

    tui::Style ghostStyle;
    ghostStyle.fg = pc.badgeText;
    ghostStyle.bg = pc.background;
    ghostStyle.dim = true;

    // Calculate padding, aurora, and chrome height
    auto const topPad = topPadding();
    auto const auroraHeight = auroraFadeHeight();
    auto const botPad = bottomPadding();
    auto const chrome = chromeHeight();
    auto const infoLineRow = topPad + auroraHeight;  // Row where info line starts.
    auto const inputStartRow = infoLineRow + chrome; // Row where input lines start.

    // Mark top padding rows for content height detection (NBSP at column 0)
    for (int i = 0; i < topPad; ++i)
        canvas.put(i, 0, "\xC2\xA0", {});

    // Render sixel aurora fade on its dedicated row (between padding and info line)
    if (auroraHeight > 0)
    {
        // Mark aurora row for content height detection
        canvas.put(topPad, 0, "\xC2\xA0", {});

        auto const cw = _context.cellPixelWidth;
        auto const ch = _context.cellPixelHeight;
        auto const termBg = theme.colors.background;
        if (_auroraFadeCacheWidth != contentWidth || _auroraFadeCacheCellW != cw
            || _auroraFadeCacheCellH != ch || _auroraFadeCacheBgColor.r != termBg.r
            || _auroraFadeCacheBgColor.g != termBg.g || _auroraFadeCacheBgColor.b != termBg.b)
        {
            _auroraFadeSixelCache = generateAuroraFadeSixel(cw, ch, contentWidth, termBg);
            _auroraFadeCacheCellW = cw;
            _auroraFadeCacheCellH = ch;
            _auroraFadeCacheWidth = contentWidth;
            _auroraFadeCacheBgColor = termBg;
        }
        if (!_auroraFadeSixelCache.empty())
            canvas.drawImage(topPad, HorizontalMargin, contentWidth, 1, _auroraFadeSixelCache);
    }

    // Render info line chrome above input
    if (chrome > 0)
    {
        // Use cached module results, re-evaluate only when cache is invalid or refresh deadline passed
        if (!_moduleCacheValid
            || (_nextModuleRefresh && std::chrono::steady_clock::now() >= *_nextModuleRefresh))
        {
            _cachedInfoModules = evaluateModules(_config.infoLineModules);
            _cachedRightModules = evaluateModules(_config.rightPromptModules);
            _nextModuleRefresh = computeModuleRefreshDeadline();
            _moduleCacheValid = true;
        }
        auto const& infoModules = _cachedInfoModules;
        auto const& rightModules = _cachedRightModules;

        // Info line background
        if (hasAurora)
        {
            for (int c = 0; c < contentWidth; ++c)
            {
                tui::Style cellStyle;
                cellStyle.bg = bgColors[static_cast<std::size_t>(c)];
                canvas.put(infoLineRow, HorizontalMargin + c, " ", cellStyle);
            }
        }
        else
        {
            canvas.fill(tui::Rect { HorizontalMargin, infoLineRow, contentWidth, 1 }, ' ', bgStyle);
        }

        auto col = HorizontalMargin;

        // Info line separator
        if (_config.separator == SeparatorStyle::Bar)
        {
            leftBarStyle.bg = bgAt(col);
            col += canvas.putString(infoLineRow, col, "\xe2\x96\x8e", leftBarStyle); // U+258E
            tui::Style spStyle;
            spStyle.bg = bgAt(col);
            canvas.put(infoLineRow, col, " ", spStyle);
            ++col;
        }
        else if (_config.separator == SeparatorStyle::Rounded)
        {
            tui::Style sepStyle;
            sepStyle.fg = pc.separator;
            sepStyle.bg = bgAt(col);
            col += canvas.putString(infoLineRow, col, "\xe2\x95\xad", sepStyle); // U+256D ╭
            sepStyle.bg = bgAt(col);
            col += canvas.putString(infoLineRow, col, "\xe2\x94\x80", sepStyle); // U+2500 ─
            tui::Style spStyle;
            spStyle.bg = bgAt(col);
            canvas.put(infoLineRow, col, " ", spStyle);
            ++col;
        }

        // Render info modules
        for (std::size_t i = 0; i < infoModules.size(); ++i)
        {
            if (i > 0)
            {
                if (_config.separator == SeparatorStyle::Rounded)
                {
                    // Dim │ pipe separator between module groups
                    tui::Style spStyle;
                    spStyle.bg = bgAt(col);
                    canvas.put(infoLineRow, col, " ", spStyle);
                    ++col;
                    tui::Style dimPipeStyle;
                    dimPipeStyle.fg = pc.separator;
                    dimPipeStyle.bg = bgAt(col);
                    dimPipeStyle.dim = true;
                    col += canvas.putString(infoLineRow, col, "\xe2\x94\x82", dimPipeStyle); // U+2502 │
                    spStyle.bg = bgAt(col);
                    canvas.put(infoLineRow, col, " ", spStyle);
                    ++col;
                }
                else
                {
                    tui::Style spStyle;
                    spStyle.bg = bgAt(col);
                    canvas.put(infoLineRow, col, " ", spStyle);
                    ++col;
                }
            }
            for (auto const& seg: infoModules[i])
            {
                auto segStyle = seg.style;
                segStyle.bg = bgAt(col);
                col += canvas.putString(infoLineRow, col, seg.text, segStyle);
            }
        }

        // Right-aligned modules on info line
        if (!rightModules.empty())
        {
            auto rightWidth = 0;
            for (auto const& mod: rightModules)
            {
                for (auto const& seg: mod)
                    rightWidth += displayWidth(seg.text);
                rightWidth += 1; // space between modules
            }
            if (rightWidth > 0)
                --rightWidth; // Remove trailing space

            auto rightCol = canvasWidth - HorizontalMargin - rightWidth;
            if (rightCol > col + 2) // Ensure at least 2 chars gap
            {
                for (std::size_t i = 0; i < rightModules.size(); ++i)
                {
                    if (i > 0)
                    {
                        tui::Style spStyle;
                        spStyle.bg = bgAt(rightCol);
                        canvas.put(infoLineRow, rightCol, " ", spStyle);
                        ++rightCol;
                    }
                    for (auto const& seg: rightModules[i])
                    {
                        auto segStyle = seg.style;
                        segStyle.bg = bgAt(rightCol);
                        rightCol += canvas.putString(infoLineRow, rightCol, seg.text, segStyle);
                    }
                }
            }
        }
    }

    // Compute syntax highlighting for the full input text (cached)
    auto const fullText = _inputField.text();
    if (fullText != _highlightCacheText)
    {
        _highlightCacheText = std::string(fullText);
        _highlightCacheMap = computeHighlightMap(fullText);
    }

    // Compute diagnostics and build per-grapheme error map
    updateDiagnostics();
    {
        // Build per-byte error flags first, then compress to per-grapheme
        auto byteErrors = std::vector<bool>(fullText.size(), false);
        if (!_diagnostics.empty())
        {
            auto const lineStarts = buildLineStartOffsets(fullText);
            for (auto const& diag: _diagnostics)
            {
                if (diag.severity != DiagnosticSeverity::Error
                    && diag.severity != DiagnosticSeverity::Warning)
                    continue;
                auto const startByte = positionToByteOffset(fullText, lineStarts, diag.range.start);
                auto const endByte = positionToByteOffset(fullText, lineStarts, diag.range.end);
                for (auto i = startByte; i < endByte && i < byteErrors.size(); ++i)
                    byteErrors[i] = true;
            }
        }
        // Compress to per-grapheme: check error flag at each cluster's first byte
        _errorMap.clear();
        _errorMap.reserve(fullText.size());
        auto errSegmenter = unicode::utf8_grapheme_segmenter(fullText);
        for (auto it = errSegmenter.begin(); it != errSegmenter.end(); ++it)
        {
            auto const byteOffset = static_cast<std::size_t>(it._clusterStart - fullText.data());
            _errorMap.push_back(byteOffset < byteErrors.size() && byteErrors[byteOffset]);
        }
    }

    // Render left chrome for each input line
    for (int lineIndex = 0; lineIndex < totalLines && (lineIndex + inputStartRow) < canvas.height();
         ++lineIndex)
    {
        auto const row = lineIndex + inputStartRow;

        // Fill content area with background (with margins)
        if (hasAurora)
        {
            for (int c = 0; c < contentWidth; ++c)
            {
                tui::Style cellStyle;
                cellStyle.bg = bgColors[static_cast<std::size_t>(c)];
                canvas.put(row, HorizontalMargin + c, " ", cellStyle);
            }
        }
        else
        {
            canvas.fill(tui::Rect { HorizontalMargin, row, contentWidth, 1 }, ' ', bgStyle);
        }

        // Draw separator on input lines
        if (_config.separator == SeparatorStyle::Bar)
        {
            leftBarStyle.bg = bgAt(HorizontalMargin);
            canvas.put(row, HorizontalMargin, "\xe2\x96\x8e", leftBarStyle); // U+258E
        }
        else if (_config.separator == SeparatorStyle::Rounded)
        {
            tui::Style sepStyle;
            sepStyle.fg = pc.separator;
            sepStyle.bg = bgAt(HorizontalMargin);
            if (lineIndex == 0)
            {
                canvas.putString(row, HorizontalMargin, "\xe2\x95\xb0", sepStyle); // U+2570 ╰
                sepStyle.bg = bgAt(HorizontalMargin + 1);
                canvas.putString(row, HorizontalMargin + 1, "\xe2\x94\x80", sepStyle); // U+2500 ─
            }
            else
                canvas.putString(row, HorizontalMargin, "\xe2\x94\x82", sepStyle); // U+2502 │
        }
        else if (_config.separator == SeparatorStyle::None)
        {
            tui::Style spStyle;
            spStyle.bg = bgAt(HorizontalMargin);
            canvas.put(row, HorizontalMargin, " ", spStyle);
        }

        // Padding after separator
        {
            auto const padCol = HorizontalMargin + leftBarWidth();
            tui::Style padStyle;
            padStyle.bg = bgAt(padCol);
            canvas.put(row, padCol, " ", padStyle);
        }
    }

    // Build continuation prompt string: spaces + middle dots (matching prompt width)
    auto continuationStr = std::string {};
    {
        auto const contSpaces = std::max(0, promptTextWidth - 2);
        continuationStr.reserve(static_cast<std::size_t>(contSpaces) + 4);
        for (int i = 0; i < contSpaces; ++i)
            continuationStr += ' ';
        continuationStr += "\xc2\xb7\xc2\xb7"; // ··
    }

    // Set up InputField with prompt, continuation, ghost text style, and decorator
    _inputField.setPrompt(_promptStr);
    _inputField.setContinuationPrompt(continuationStr);
    _inputField.setStyles(tui::InputFieldStyles {
        .text = promptStyle,
        .ghost = ghostStyle,
    });

    // Configure decorator for this frame
    auto const fieldOriginCol = HorizontalMargin + leftBarWidth() + PaddingAfterBar;
    _decorator.highlightMap = &_highlightCacheMap;
    _decorator.errorMap = &_errorMap;
    _decorator.bgColors = bgColors.empty() ? nullptr : &bgColors;
    _decorator.flatBg = pc.background;
    _decorator.bgOffset = fieldOriginCol - HorizontalMargin; // Map field col 0 to aurora col offset
    _decorator.theme = &theme;
    _inputField.setTextDecorator(&_decorator);

    // Render InputField into a subcanvas that starts after the left chrome
    auto const fieldArea = tui::Rect {
        fieldOriginCol,
        inputStartRow,
        canvasWidth - fieldOriginCol - HorizontalMargin,
        std::min(totalLines, canvas.height() - inputStartRow),
    };
    auto fieldCanvas = canvas.subcanvas(fieldArea);
    _inputField.render(fieldCanvas);

    // The cursor position is set by InputField::render() on the subcanvas,
    // which translates to the correct canvas-absolute position.

    // Render completion popup if visible
    if (_completionPopup.visible())
    {
        auto const cursorLine = _inputField.cursorLine();
        auto const cursorRow = cursorLine + inputStartRow;
        auto popupSize = _completionPopup.preferredSize();
        int availableBelow = canvas.height() - cursorRow - 1;
        int availableAbove = cursorRow;

        bool renderBelow = true;

        // In fullscreen/fixed mode, choose direction based on available space
        // In inline mode, always render below (Screen handles scrolling via preferredSize)
        if (auto* scr = screen(); scr && scr->viewport() != tui::Viewport::Inline)
        {
            // Prefer below, but use above if below has < 3 rows and above has more space
            if (availableBelow < 3 && availableAbove > availableBelow)
                renderBelow = false;
        }

        int popupRow = renderBelow ? (cursorRow + 1)
                                   : std::max(0, cursorRow - std::min(popupSize.height, availableAbove));
        int popupHeight = renderBelow ? std::min(popupSize.height, std::max(0, availableBelow))
                                      : std::min(popupSize.height, availableAbove);

        if (popupHeight >= 3) // Minimum: border (2) + 1 item
        {
            auto popupRect =
                tui::Rect { totalPromptWidth, // x (column) - where prompt ends
                            popupRow,         // y (row) - below or above cursor
                            std::min(popupSize.width, canvasWidth - totalPromptWidth - HorizontalMargin),
                            popupHeight };

            _completionPopup.setArea(popupRect);
            auto popupCanvas = canvas.subcanvas(popupRect);
            _completionPopup.render(popupCanvas);
        }
    }

    // Mark bottom padding rows for content height detection (NBSP at column 0)
    for (int i = 0; i < botPad; ++i)
        canvas.put(inputStartRow + totalLines + i, 0, "\xC2\xA0", {});

    _firstDisplay = false;
}

tui::EventResult PromptComponent::onEvent(tui::InputEvent const& event)
{
    auto action = processInput(event);
    if (action != Action::None)
    {
        flushDeferredUpdates();
        invalidate();
        return tui::EventResult::Handled;
    }
    return tui::EventResult::Ignored;
}

tui::Size PromptComponent::preferredSize() const
{
    auto const inputLineCount = _inputField.lineCount();
    auto const pw = this->promptWidth();

    // Calculate max line width
    int maxWidth = 0;
    for (int i = 0; i < inputLineCount; ++i)
    {
        auto const lineContent = _inputField.lineAt(i);
        maxWidth = std::max(maxWidth, pw + displayWidth(lineContent));
    }

    // Total height = top padding + aurora fade + chrome lines (info/box above) + input lines + bottom padding
    int totalHeight = topPadding() + auroraFadeHeight() + chromeHeight() + inputLineCount + bottomPadding();

    // If completion popup is visible, add space for it below the input
    if (_completionPopup.visible())
    {
        auto popupSize = _completionPopup.preferredSize();
        totalHeight += popupSize.height;
        maxWidth = std::max(maxWidth, pw + popupSize.width);
    }

    return { maxWidth, totalHeight };
}

void PromptComponent::setPrompt(std::string_view prompt)
{
    _promptStr = std::string(prompt);
}

int PromptComponent::promptWidth() const
{
    return leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr);
}

int PromptComponent::chromeHeight() const noexcept
{
    if (_config.layout == PromptLayoutKind::TwoLine || _config.layout == PromptLayoutKind::Powerline)
        return 1;
    if (_config.layout == PromptLayoutKind::Boxed)
        return 3;
    return 0;
}

int PromptComponent::auroraFadeHeight() const noexcept
{
    return (_config.enableSixelFade && !_config.auroraBackground.empty() && _context.cellPixelHeight > 0) ? 1
                                                                                                          : 0;
}

int PromptComponent::topPadding() const noexcept
{
    return _firstDisplay ? 0 : _config.promptSpacing;
}

int PromptComponent::bottomPadding() const noexcept
{
    return _config.promptSpacing;
}

int PromptComponent::displayWidth(std::string_view text)
{
    int width = 0;
    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    for (auto const& cluster: segmenter)
        width += tui::graphemeClusterWidth(cluster);
    return width;
}

PromptComponent::Action PromptComponent::processInput(tui::InputEvent const& event)
{
    // Hide tooltip when user starts typing
    if (std::holds_alternative<tui::KeyEvent>(event))
    {
        if (auto* scr = screen())
            scr->hideTooltip();
    }

    // Track if popup was visible before processing (for dynamic filtering)
    bool const popupWasVisible = _completionPopup.visible();
    bool popupDismissedByTyping = false;

    // Handle completion popup events first
    if (_completionPopup.visible())
    {
        // Intercept Tab for partial completion (longest common prefix)
        if (auto const* key = std::get_if<tui::KeyEvent>(&event);
            key && key->key == tui::KeyCode::Tab
            && tui::withoutLockKeys(key->modifiers) == tui::Modifier::None
            && _completionPopup.itemCount() > 1)
        {
            auto const commonPrefix = tui::Completer::findCommonPrefix(_completionPopup.items());
            if (!commonPrefix.empty())
            {
                auto const ctx = _completer->analyzeContext(_inputField.text(), _inputField.cursor());
                if (commonPrefix.size() > ctx.prefix.size())
                {
                    insertCompletion(commonPrefix);
                    _completionPopupDirty = true;
                    return Action::Changed;
                }
            }
        }

        auto completionResult = _completionPopup.processEvent(event);
        switch (completionResult)
        {
            case tui::CompletionAction::Changed: return Action::Changed;
            case tui::CompletionAction::Accepted:
                if (auto const* selected = _completionPopup.selectedItem())
                {
                    if (_historySearchMode)
                        _inputField.setText(selected->text); // Replace entire input with history entry
                    else
                        insertCompletion(selected->text);
                }
                dismissPopup();
                updateGhostText(); // Clear/update ghost text after completion
                return Action::Changed;
            case tui::CompletionAction::Dismissed:
                // Don't hide yet - let event pass through and potentially re-filter
                popupDismissedByTyping = true;
                break;
        }
    }

    // Inline history cycling: Up/Down cycles through history (prefix-matched when input is non-empty)
    if (!_completionPopup.visible())
    {
        if (auto const* key = std::get_if<tui::KeyEvent>(&event))
        {
            auto const inputText = std::string(_inputField.text());
            if (key->key == tui::KeyCode::Up || key->key == tui::KeyCode::Down)
            {
                if (key->key == tui::KeyCode::Up)
                {
                    if (!_historyCycleIndex.has_value())
                    {
                        // First Up press: compute candidates from completer, save original input
                        _historyCycleSavedInput = inputText;
                        _historyCandidates.clear();
                        if (_history)
                        {
                            auto matches = _history->search(inputText, 50);
                            for (auto const& entry: matches)
                                if (entry != inputText)
                                    _historyCandidates.push_back(std::string(entry));
                        }
                        if (!_historyCandidates.empty())
                            _historyCycleIndex = 0;
                    }
                    else if (*_historyCycleIndex + 1 < _historyCandidates.size())
                    {
                        ++(*_historyCycleIndex);
                    }
                    // else: at end, do nothing (don't wrap)
                }
                else // Down
                {
                    if (_historyCycleIndex.has_value())
                    {
                        if (*_historyCycleIndex > 0)
                            --(*_historyCycleIndex);
                        else
                        {
                            // Back to original input
                            _historyCycleIndex.reset();
                        }
                    }
                }

                // Apply the selected candidate or restore original
                if (_historyCycleIndex.has_value())
                    _inputField.setText(_historyCandidates[*_historyCycleIndex]);
                else
                    _inputField.setText(_historyCycleSavedInput);

                updateGhostText();
                return Action::Changed;
            }
        }
    }

    // Handle key events with special completion handling
    if (auto const* key = std::get_if<tui::KeyEvent>(&event))
    {
        // Tab triggers completion (double-Tab forces popup to show)
        if (key->key == tui::KeyCode::Tab && tui::withoutLockKeys(key->modifiers) == tui::Modifier::None)
        {
            auto const now = std::chrono::steady_clock::now();
            bool const isDoubleTab = (now - _lastTabTime) < DoubleTabThreshold;
            _lastTabTime = now;
            triggerCompletion(isDoubleTab);
            return Action::Changed;
        }

        // Ctrl+Space triggers completion (always shows popup)
        if (key->codepoint == ' ' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl))
        {
            triggerCompletion(true);
            return Action::Changed;
        }

        // Ctrl+L clears the screen
        if (key->codepoint == 'l' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl))
        {
            return Action::ClearScreen;
        }

        // Ctrl+R triggers history search (fuzzy popup over all history)
        if (key->codepoint == 'r' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl))
        {
            _historySearchMode = true;
            triggerHistorySearch();
            return Action::Changed;
        }

        // Right arrow or End at end of line accepts ghost text
        if (_inputField.hasGhostText() && _inputField.cursor() == _inputField.text().size())
        {
            if (key->key == tui::KeyCode::Right || key->key == tui::KeyCode::End
                || (key->codepoint == 'e' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl)))
            {
                _inputField.acceptGhostText();
                updateGhostText();
                return Action::Changed;
            }
        }

        // '#' on empty input enters agent mode
        if (key->codepoint == '#' && tui::withoutLockKeys(key->modifiers) == tui::Modifier::None
            && _inputField.text().empty())
            return Action::AgentMode;
    }

    // Process through InputField
    auto action = _inputField.processEvent(event);

    switch (action)
    {
        case tui::InputFieldAction::Submit:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            return Action::Submit;
        case tui::InputFieldAction::Abort:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            return Action::Abort;
        case tui::InputFieldAction::Eof:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            return Action::Eof;
        case tui::InputFieldAction::AgentMode:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            return Action::AgentMode;
        case tui::InputFieldAction::CycleAgentMode:
            // Not applicable in shell prompt context; ignore.
            break;
        case tui::InputFieldAction::Changed:
            resetHistoryCycling();
            _inputField.clearGhostText(); // Remove stale suggestion immediately
            _ghostTextDirty = true;
            _ghostTextPendingSince = std::chrono::steady_clock::now();
            // If popup was visible and dismissed by typing, re-filter instead of hiding
            if (popupWasVisible && popupDismissedByTyping)
                _completionPopupDirty = true;
            return Action::Changed;
        case tui::InputFieldAction::None:
            // If dismissed but text didn't change (e.g., Escape), hide popup
            if (popupDismissedByTyping)
            {
                dismissPopup();
                return Action::Changed; // Trigger re-render so popup disappears
            }
            break;
    }

    return Action::None;
}

void PromptComponent::updateGhostText()
{
    if (!_completer)
    {
        _inputField.clearGhostText();
        return;
    }

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Only show ghost text when cursor is at end of input
    if (cursor != text.size())
    {
        _inputField.clearGhostText();
        return;
    }

    // Check suggest cache — skip expensive completer call if text unchanged
    if (text == _suggestCacheText)
    {
        if (_suggestCacheResult)
            _inputField.setGhostText(*_suggestCacheResult);
        else
            _inputField.clearGhostText();
        return;
    }

    // Cache miss — call completer and store result
    auto suggestion = _completer->suggest(text, cursor);
    _suggestCacheText = std::string(text);
    _suggestCacheResult = suggestion;

    if (suggestion)
        _inputField.setGhostText(*suggestion);
    else
        _inputField.clearGhostText();
}

void PromptComponent::triggerCompletion(bool forceShowPopup)
{
    if (!_completer)
        return;

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Get completions
    auto completions = _completer->complete(text, cursor);

    if (completions.empty())
    {
        dismissPopup();
        return;
    }

    if (completions.size() == 1 && !forceShowPopup)
    {
        // Single match: insert directly without showing popup
        // (unless force-show was requested via double-Tab or Ctrl+Space)
        insertCompletion(completions[0].text);
        dismissPopup();
        updateGhostText(); // Clear/update ghost text after completion
        return;
    }

    // Multiple matches (or force-show): populate and show popup
    std::vector<tui::CompletionItem> popupItems;
    popupItems.reserve(completions.size());
    for (auto const& item: completions)
    {
        popupItems.push_back(tui::CompletionItem {
            .text = item.text,
            .displayText = item.displayText.empty() ? item.text : item.displayText,
            .description = item.description,
            .score = item.score,
        });
    }
    _completionPopup.show(std::move(popupItems));
}

void PromptComponent::updateCompletionPopup()
{
    if (!_completer)
    {
        dismissPopup();
        return;
    }

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Get filtered completions
    auto completions = _completer->complete(text, cursor);

    if (completions.empty())
    {
        dismissPopup(); // Auto-close on 0 matches
        return;
    }

    // Convert to popup items
    std::vector<tui::CompletionItem> popupItems;
    popupItems.reserve(completions.size());
    for (auto const& item: completions)
    {
        popupItems.push_back(tui::CompletionItem {
            .text = item.text,
            .displayText = item.displayText.empty() ? item.text : item.displayText,
            .description = item.description,
            .score = item.score,
        });
    }

    // Update with selection preservation
    _completionPopup.updateItems(std::move(popupItems));
}

void PromptComponent::dismissPopup()
{
    _completionPopup.hide();
    _historySearchMode = false;
}

void PromptComponent::triggerHistorySearch()
{
    if (!_history)
    {
        dismissPopup();
        return;
    }

    auto const inputText = std::string(_inputField.text());
    auto results = _history->searchFuzzy(inputText, 200);

    if (results.empty())
    {
        dismissPopup();
        return;
    }

    std::vector<tui::CompletionItem> items;
    items.reserve(results.size());
    for (auto const& result: results)
    {
        items.push_back(tui::CompletionItem {
            .text = std::string(result.entry),
            .displayText = std::string(result.entry),
            .description = {},
            .score = result.score,
            .matchPositions = result.positions,
        });
    }
    _completionPopup.show(std::move(items));
}

void PromptComponent::updateHistorySearchPopup()
{
    if (!_history)
    {
        dismissPopup();
        return;
    }

    auto const inputText = std::string(_inputField.text());
    auto results = _history->searchFuzzy(inputText, 200);

    if (results.empty())
    {
        dismissPopup();
        return;
    }

    std::vector<tui::CompletionItem> items;
    items.reserve(results.size());
    for (auto const& result: results)
    {
        items.push_back(tui::CompletionItem {
            .text = std::string(result.entry),
            .displayText = std::string(result.entry),
            .description = {},
            .score = result.score,
            .matchPositions = result.positions,
        });
    }
    _completionPopup.updateItems(std::move(items));
}

void PromptComponent::flushDeferredUpdates()
{
    if (_ghostTextDirty)
    {
        // Only flush once debounce period has elapsed
        if (!_ghostTextPendingSince
            || (std::chrono::steady_clock::now() - *_ghostTextPendingSince) >= GhostTextDebounceMs)
        {
            _ghostTextDirty = false;
            _ghostTextPendingSince.reset();
            updateGhostText();
        }
        // else: debounce not expired — keep dirty flag for next flush
    }
    if (_completionPopupDirty)
    {
        _completionPopupDirty = false;
        if (_historySearchMode)
            updateHistorySearchPopup();
        else
            updateCompletionPopup();
    }
}

void PromptComponent::insertCompletion(std::string_view text)
{
    if (!_completer)
        return;

    auto const inputText = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Get the context to find what prefix to replace
    auto ctx = _completer->analyzeContext(inputText, cursor);

    // Calculate how much text to replace (the prefix being completed)
    auto const prefixLen = ctx.prefix.size();

    // Build new buffer: text before prefix + completion + text after cursor
    std::string newBuffer;
    newBuffer.reserve(inputText.size() - prefixLen + text.size());
    newBuffer.append(inputText.substr(0, cursor - prefixLen));
    newBuffer.append(text);
    newBuffer.append(inputText.substr(cursor));

    // Update the input field
    _inputField.setText(newBuffer);
}

void PromptComponent::onHoverConfirmed(int x, int y)
{
    auto* scr = screen();
    if (!scr)
        return;

    auto const bounds = screenBounds();

    // Convert screen coordinates to source position
    auto const sourcePos = screenToSourcePosition(x, y);

    // Priority 1: Check diagnostics at this position
    if (sourcePos)
    {
        if (auto diag = diagnosticAt(sourcePos->line, sourcePos->character))
        {
            auto tooltipText = diag->message;
            for (auto const& hint: diag->suggestions)
                tooltipText += "\nhint: " + hint;
            tui::Point tooltipPos { bounds.x + x, bounds.y + y + 1 };
            scr->showTooltip(tooltipText, tooltipPos, tui::TooltipContentType::PlainText);
            return;
        }
    }

    // Priority 2: Check language hover info (keywords, constructors, operators, builtins, bindings)
    if (sourcePos)
    {
        auto const text = std::string(_inputField.text());
        if (auto hover = endo::computeHover(text, *sourcePos))
        {
            tui::Point tooltipPos { bounds.x + x, bounds.y + y + 1 };
            scr->showTooltip(hover->markdownText, tooltipPos, tui::TooltipContentType::Markdown);
            return;
        }
    }

    // Priority 3: Fall through to existing command hover logic (first input line only)
    if (y == topPadding() + auroraFadeHeight() + chromeHeight() && _commandResolver)
    {
        auto const cmd = getCommandAtColumn(x);
        if (cmd)
        {
            auto const info = _commandResolver->resolve(*cmd);
            auto const [cmdStart, cmdEnd] = getCommandBounds();
            tui::Point tooltipPos { bounds.x + cmdStart, bounds.y + 1 };
            scr->showTooltip(info.tooltip, tooltipPos, tui::TooltipContentType::PlainText);
        }
    }
}

void PromptComponent::onHoverLeave()
{
    if (auto* scr = screen())
    {
        scr->hideTooltip();
    }
}

std::string PromptComponent::generateAuroraFadeSixel(int cellPixelWidth,
                                                     int cellPixelHeight,
                                                     int contentWidthCols,
                                                     tui::RgbColor bgColor) const
{
    auto const imgWidth = contentWidthCols * cellPixelWidth;
    auto const imgHeight = cellPixelHeight;

    if (imgWidth <= 0 || imgHeight <= 0)
        return {};

    // Generate RGBA pixels with alpha pre-multiplied against terminal background.
    // Sixel has no per-pixel alpha; without pre-multiplication the binary alpha threshold
    // (< 128 = transparent, >= 128 = opaque) creates a hard edge instead of a smooth fade.
    auto pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(imgWidth) * imgHeight * 4, 0);

    for (int y = 0; y < imgHeight; ++y)
    {
        // Vertical fade: 0 at top → 1 at bottom, with cubic ease-in for a perceptually
        // smooth transition. Linear ramps look abrupt because brightness perception is
        // non-linear; t³ keeps the top ~70% close to background and concentrates the
        // color ramp near the bottom where it meets the info line.
        auto const t = (imgHeight > 1) ? static_cast<float>(y) / static_cast<float>(imgHeight - 1) : 1.0f;
        auto const alpha = t * t * t;
        auto const a = static_cast<unsigned>(static_cast<std::uint8_t>(alpha * 255.0f));

        for (int x = 0; x < imgWidth; ++x)
        {
            auto const idx = (static_cast<std::size_t>(y) * imgWidth + x) * 4;

            // Horizontal gradient position
            auto const t = (imgWidth > 1) ? static_cast<float>(x) / static_cast<float>(imgWidth - 1) : 0.0f;
            auto const color = multiStopGradient(_config.auroraBackground, t);

            // Pre-multiply alpha: blend aurora color with terminal background
            pixels[idx + 0] = static_cast<std::uint8_t>((color.r * a + bgColor.r * (255 - a)) / 255);
            pixels[idx + 1] = static_cast<std::uint8_t>((color.g * a + bgColor.g * (255 - a)) / 255);
            pixels[idx + 2] = static_cast<std::uint8_t>((color.b * a + bgColor.b * (255 - a)) / 255);
            pixels[idx + 3] = 255; // Fully opaque — fade is baked into RGB
        }
    }

    // Encode to sixel
    auto const imageData = tui::ImageData {
        .pixels = std::span<const std::uint8_t>(pixels),
        .width = imgWidth,
        .height = imgHeight,
    };
    auto result = tui::encodeSixel(imageData, 64);
    return result.has_value() ? std::move(*result) : std::string {};
}

std::optional<std::string> PromptComponent::getCommandAtColumn(int screenColumn) const
{
    // Calculate the prompt prefix width
    auto const totalPromptWidth =
        HorizontalMargin + leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr);

    // Check if column is within command bounds
    auto const [cmdStart, cmdEnd] = getCommandBounds();
    if (screenColumn < cmdStart || screenColumn >= cmdEnd)
        return std::nullopt;

    // Extract the command from input text
    auto const text = _inputField.text();
    if (text.empty())
        return std::nullopt;

    // Skip leading whitespace to find command start
    auto pos = std::size_t { 0 };
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;

    if (pos >= text.size())
        return std::nullopt;

    // Find command end (first whitespace, pipe, semicolon, etc.)
    auto cmdEndPos = pos;
    while (cmdEndPos < text.size() && text[cmdEndPos] != ' ' && text[cmdEndPos] != '\t'
           && text[cmdEndPos] != '|' && text[cmdEndPos] != ';' && text[cmdEndPos] != '&'
           && text[cmdEndPos] != '\n' && text[cmdEndPos] != '(' && text[cmdEndPos] != ')')
    {
        ++cmdEndPos;
    }

    if (cmdEndPos <= pos)
        return std::nullopt;

    return std::string(text.substr(pos, cmdEndPos - pos));
}

void PromptComponent::setKnownFSharpNames(std::set<std::string> names)
{
    if (_knownFSharpNames == names)
        return;

    _knownFSharpNames = std::move(names);
    _diagnosticsContent.clear(); // Invalidate cache so diagnostics re-run with new names
}

void PromptComponent::updateDiagnostics()
{
    auto const text = std::string(_inputField.text());
    if (text == _diagnosticsContent)
    {
        // Text unchanged — check if debounce timer has fired
        if (_diagnosticsPendingSince)
        {
            auto const elapsed = std::chrono::steady_clock::now() - *_diagnosticsPendingSince;
            if (elapsed >= DiagnosticsDebounceMs)
            {
                _diagnosticsPendingSince.reset();
                _diagnostics = endo::collectDiagnostics(text, _knownFSharpNames);
            }
        }
        return;
    }

    // Text changed — clear stale diagnostics and start debounce timer
    _diagnosticsContent = text;
    _diagnostics.clear();
    _diagnosticsPendingSince = std::chrono::steady_clock::now();
}

int PromptComponent::diagnosticsTimeoutMs() const
{
    if (!_diagnosticsPendingSince)
        return -1;

    auto const elapsed = std::chrono::steady_clock::now() - *_diagnosticsPendingSince;
    auto const remaining = DiagnosticsDebounceMs - elapsed;
    if (remaining <= std::chrono::milliseconds::zero())
        return 0;

    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
}

int PromptComponent::ghostTextTimeoutMs() const
{
    if (!_ghostTextPendingSince)
        return -1;

    auto const elapsed = std::chrono::steady_clock::now() - *_ghostTextPendingSince;
    auto const remaining = GhostTextDebounceMs - elapsed;
    if (remaining <= std::chrono::milliseconds::zero())
        return 0;

    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
}

std::optional<endo::DiagnosticMessage> PromptComponent::diagnosticAt(int line, int character) const
{
    for (auto const& diag: _diagnostics)
    {
        auto const& r = diag.range;
        // Check if (line, character) is within this diagnostic's range
        if (line < r.start.line || line > r.end.line)
            continue;
        if (line == r.start.line && character < r.start.character)
            continue;
        if (line == r.end.line && character >= r.end.character)
            continue;
        return diag;
    }
    return std::nullopt;
}

std::optional<endo::SourcePosition> PromptComponent::screenToSourcePosition(int x, int y) const
{
    auto const totalPromptWidth =
        HorizontalMargin + leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr);

    // Screen x must be within the text area
    if (x < totalPromptWidth)
        return std::nullopt;

    // Convert screen y to input line index (subtract top padding + aurora + chrome offset)
    auto const inputLine = y - topPadding() - auroraFadeHeight() - chromeHeight();
    auto const totalLines = _inputField.lineCount();
    if (inputLine < 0 || inputLine >= totalLines)
        return std::nullopt;

    auto const lineContent = _inputField.lineAt(inputLine);
    if (lineContent.empty())
        return endo::SourcePosition { .line = inputLine, .character = 0 };

    // Walk grapheme clusters to convert display column to codepoint index
    auto const targetCol = x - totalPromptWidth;
    auto segmenter = unicode::utf8_grapheme_segmenter(lineContent);
    int displayCol = 0;
    int codepointIndex = 0;

    for (auto const& cluster: segmenter)
    {
        auto const w = tui::graphemeClusterWidth(cluster);
        if (displayCol + w > targetCol)
            break;
        displayCol += w;
        ++codepointIndex;
    }

    return endo::SourcePosition { .line = inputLine, .character = codepointIndex };
}

std::pair<int, int> PromptComponent::getCommandBounds() const
{
    auto const totalPromptWidth =
        HorizontalMargin + leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr);

    auto const text = _inputField.text();
    if (text.empty())
        return { totalPromptWidth, totalPromptWidth };

    // Skip leading whitespace
    auto pos = std::size_t { 0 };
    int leadingSpaceWidth = 0;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
    {
        leadingSpaceWidth += (text[pos] == '\t') ? 8 : 1; // Approximate tab width
        ++pos;
    }

    if (pos >= text.size())
        return { totalPromptWidth + leadingSpaceWidth, totalPromptWidth + leadingSpaceWidth };

    // Find command end and calculate display width
    auto cmdEndPos = pos;
    while (cmdEndPos < text.size() && text[cmdEndPos] != ' ' && text[cmdEndPos] != '\t'
           && text[cmdEndPos] != '|' && text[cmdEndPos] != ';' && text[cmdEndPos] != '&'
           && text[cmdEndPos] != '\n' && text[cmdEndPos] != '(' && text[cmdEndPos] != ')')
    {
        ++cmdEndPos;
    }

    auto const cmdText = text.substr(pos, cmdEndPos - pos);
    auto const cmdWidth = displayWidth(cmdText);

    int const cmdStart = totalPromptWidth + leadingSpaceWidth;
    int const cmdEnd = cmdStart + cmdWidth;

    return { cmdStart, cmdEnd };
}

void PromptComponent::resetHistoryCycling()
{
    _historyCycleIndex.reset();
    _historyCandidates.clear();
}

std::optional<std::chrono::steady_clock::time_point> PromptComponent::computeModuleRefreshDeadline() const
{
    auto minInterval = std::optional<std::chrono::milliseconds> {};

    auto const checkModules = [&](std::vector<std::string> const& moduleNames) {
        for (auto const& name: moduleNames)
        {
            auto const it = _modules.find(name);
            if (it == _modules.end())
                continue;
            if (!it->second->shouldShow(_context))
                continue;
            if (auto const interval = it->second->refreshInterval())
            {
                if (!minInterval || *interval < *minInterval)
                    minInterval = *interval;
            }
        }
    };

    checkModules(_config.infoLineModules);
    checkModules(_config.rightPromptModules);

    if (minInterval)
        return std::chrono::steady_clock::now() + *minInterval;

    return std::nullopt;
}

int PromptComponent::moduleRefreshTimeoutMs() const
{
    if (!_nextModuleRefresh)
        return -1;

    auto const now = std::chrono::steady_clock::now();
    if (now >= *_nextModuleRefresh)
        return 0;

    auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*_nextModuleRefresh - now);
    return std::max(100, static_cast<int>(remaining.count()));
}

} // namespace endo
