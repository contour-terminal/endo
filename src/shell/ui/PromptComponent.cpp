// SPDX-License-Identifier: Apache-2.0
#include "PromptComponent.hpp"
#include <shell/completion/Completer.hpp>
#include <shell/completion/FileCompleter.hpp>
#include <shell/history/History.hpp>
#include <shell/history/RequiredPaths.hpp>
#include <shell/ui/SourceOffsetUtils.hpp>
#include <shell/ui/SyntaxHighlighter.hpp>
#include <shell/util/CommandResolver.hpp>

#include <endo-language/ide/HoverProvider.hpp>

#include <tui/Canvas.hpp>
#include <tui/GhostTextHelper.hpp>
#include <tui/Screen.hpp>
#include <tui/Sixel.hpp>
#include <tui/Theme.hpp>
#include <tui/TimerUtils.hpp>
#include <tui/Unicode.hpp>
#include <tui/completer/Completer.hpp>

#include <algorithm>
#include <utility>

#include "Gradient.hpp"
#include "PromptColorResolver.hpp"
#include "modules/BatteryModule.hpp"
#include "modules/ClockModule.hpp"
#include "modules/DurationModule.hpp"
#include "modules/ExitStatusModule.hpp"
#include "modules/FSharpModeModule.hpp"
#include "modules/GitModule.hpp"
#include "modules/HostnameModule.hpp"
#include "modules/IndicatorModule.hpp"
#include "modules/PathModule.hpp"
#include "modules/ShellLevelModule.hpp"
#include "modules/StructuredOutputModule.hpp"
#include "modules/ToolchainModule.hpp"
#include <platform/ProjectFileTree.hpp>

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
    if (bgColors && !bgColors->empty() && idx >= 0 && std::cmp_less(idx, bgColors->size()))
        return (*bgColors)[static_cast<std::size_t>(idx)];
    if (transparentBg)
        return std::nullopt; // Let terminal default show through
    return flatBg;
}

// ============================================================================
// PromptComponent
// ============================================================================

PromptComponent::PromptComponent()
{
    _inputField.setPrompt(""); // We handle prompt rendering ourselves
    initializeModules();

    // Add fuzzy file finder as child component for proper tree-based rendering.
    // Starts hidden; shown on Ctrl+G. High z-index ensures overlay rendering.
    addChild(_fuzzyFileFinder, tui::LayoutParams { .visible = false, .zIndex = 10 });
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
    add(std::make_unique<ShellLevelModule>());
    add(std::make_unique<StructuredOutputModule>());
    add(std::make_unique<ToolchainModule>());
    add(std::make_unique<IndicatorModule>(_config.indicator));
}

void PromptComponent::updateModuleCache(std::vector<std::string> const& moduleNames,
                                        ModuleSensitivity changedFlags,
                                        bool timerExpired)
{
    for (auto const& name: moduleNames)
    {
        auto modIt = _modules.find(name);
        if (modIt == _modules.end())
            continue;

        auto& mod = *modIt->second;
        auto& entry = _moduleCache[name];

        auto const needsReeval = !entry.evaluated
                                 || (mod.sensitivity() & changedFlags) != ModuleSensitivity::None
                                 || (timerExpired && mod.refreshInterval().has_value());

        if (needsReeval)
        {
            entry.visible = mod.shouldShow(_context);
            entry.segments = entry.visible ? mod.evaluate(_context) : PromptSegments {};
            entry.evaluated = true;
        }
    }
}

std::vector<PromptSegments> PromptComponent::buildModuleVector(
    std::vector<std::string> const& moduleNames) const
{
    // Map module names to their resolved ColorSpec for gradient detection
    auto const colorSpecForModule = [this](std::string const& name) -> ColorSpec const* {
        if (!_context.resolvedColors)
            return nullptr;
        auto const& rc = *_context.resolvedColors;
        if (name == "path")
            return &rc.path;
        if (name == "exit_status")
            return &rc.exitCode;
        if (name == "duration")
            return &rc.duration;
        if (name == "hostname")
            return &rc.hostname;
        if (name == "clock")
            return &rc.clock;
        if (name == "shell_level" || name == "battery")
            return &rc.badge;
        // git, fsharp_mode, structured_output, toolchain: no single color mapping
        return nullptr;
    };

    auto results = std::vector<PromptSegments> {};
    for (auto const& name: moduleNames)
    {
        auto it = _moduleCache.find(name);
        if (it == _moduleCache.end() || !it->second.visible || it->second.segments.empty())
            continue;

        // Apply gradient when the resolved ColorSpec has multiple stops
        auto const* spec = colorSpecForModule(name);
        if (spec && spec->isGradient())
        {
            // Combine all segment text for gradient application
            auto combinedText = std::string {};
            for (auto const& seg: it->second.segments)
                combinedText += seg.text;
            auto gradientSegments = gradient(spec->colors, combinedText);
            // Transfer style attributes (bold, etc.) from original segments
            for (auto& seg: gradientSegments)
                seg.style.bold = true;
            results.push_back(std::move(gradientSegments));
        }
        else
        {
            results.push_back(it->second.segments);
        }
    }
    return results;
}

void PromptComponent::setPromptConfig(PromptConfig config)
{
    // Dropping the entire cache is the simplest correct response to a config update:
    // if the user changed any dynamic-resolver assignment (e.g. `shell_prompt_indicator <- newFn`
    // or reassigned a color function), cached outputs from the previous function are stale.
    // The cost is one re-evaluation per field on the next render.
    _dynamicCallbackCache.clear();

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

    // Track granular change flags for per-module selective re-evaluation.
    if (context.cwd != _context.cwd)
        _pendingChanges |= ModuleSensitivity::CwdChange;
    if (context.lastExitCode != _context.lastExitCode)
        _pendingChanges |= ModuleSensitivity::ExitCode;
    if (context.lastDuration != _context.lastDuration)
        _pendingChanges |= ModuleSensitivity::Duration;

    // Invalidate internal module caches (e.g., GitModule TTL) on any context change.
    if ((_pendingChanges & ModuleSensitivity::ContextChange) != ModuleSensitivity::None)
    {
        for (auto& [name, mod]: _modules)
            mod->invalidateCache();
        // Dynamic-callback cache mirrors module caches: invalidate on any context change
        // so user callbacks see current CWD, exit code, etc.
        _dynamicCallbackCache.clear();
    }

    _context = std::move(context);
}

std::optional<std::string> PromptComponent::evaluateDynamicCallback(std::string const& fnName)
{
    if (!_dynamicFieldResolver)
        return std::nullopt;
    if (auto it = _dynamicCallbackCache.find(fnName); it != _dynamicCallbackCache.end())
        return it->second;
    auto result = _dynamicFieldResolver(fnName);
    if (result)
        _dynamicCallbackCache.emplace(fnName, *result);
    return result;
}

void PromptComponent::setTerminalFocused(bool focused) noexcept
{
    auto const wasUnfocused = !_terminalFocused;
    _terminalFocused = focused;
    if (focused && wasUnfocused)
    {
        // On focus gain: invalidate all module caches and schedule immediate refresh
        // so that time-varying data (git branch, battery) is re-queried instantly.
        for (auto& [name, mod]: _modules)
            mod->invalidateCache();
        _nextModuleRefresh = std::chrono::steady_clock::now();
    }
}

void PromptComponent::render(tui::Canvas& canvas)
{
    // Resolve dynamic prompt fields before module evaluation. Each configured
    // `*Fn` names a user F# function; the installed resolver runs it and returns
    // the string to use for this render. nullopt falls back to the static field.
    // Results are cached per fnName and invalidated on context change (see
    // setPromptContext), so intra-render and per-keystroke renders are cheap.
    if (_config.indicatorFn.has_value())
        _context.indicatorOverride = evaluateDynamicCallback(*_config.indicatorFn);
    else
        _context.indicatorOverride.reset();

    auto const& theme = tui::currentTheme();
    auto const canvasWidth = canvas.width();
    auto const totalLines = _inputField.lineCount();
    auto const promptTextWidth = displayWidth(_promptStr);
    auto const totalPromptWidth = HorizontalMargin + leftBarWidth() + PaddingAfterBar + promptTextWidth;

    // Effective content width (excluding margins)
    auto const contentWidth = canvasWidth - (2 * HorizontalMargin);

    // Apply dynamic color overrides: for each color key in `colorFns`, invoke the
    // named F# function, parse the returned string as a ColorSpec, and populate the
    // corresponding member of a frame-local copy of the overrides. The static config
    // is unchanged; only this frame's `resolved` colors reflect the dynamic values.
    //
    // Copying PromptColorOverrides is only necessary when dynamic resolvers are actually
    // configured; otherwise we resolve directly against the static config to avoid the
    // per-keystroke copy overhead (PromptColorOverrides contains an unordered_map).
    PromptColorOverrides const* overridesForResolve = &_config.colorOverrides;
    auto frameOverridesStorage = std::optional<PromptColorOverrides> {};
    if (_dynamicFieldResolver && !_config.colorOverrides.colorFns.empty())
    {
        frameOverridesStorage.emplace(_config.colorOverrides);
        overridesForResolve = &*frameOverridesStorage;

        using ColorField = std::optional<ColorSpec> PromptColorOverrides::*;
        static std::pair<std::string_view, ColorField> const colorFieldMap[] = {
            { "path", &PromptColorOverrides::path },
            { "git_clean", &PromptColorOverrides::gitClean },
            { "git_dirty", &PromptColorOverrides::gitDirty },
            { "git_staged", &PromptColorOverrides::gitStaged },
            { "indicator", &PromptColorOverrides::indicator },
            { "indicator_error", &PromptColorOverrides::indicatorError },
            { "exit_code", &PromptColorOverrides::exitCode },
            { "duration", &PromptColorOverrides::duration },
            { "hostname", &PromptColorOverrides::hostname },
            { "separator", &PromptColorOverrides::separator },
            { "badge", &PromptColorOverrides::badge },
            { "badge_text", &PromptColorOverrides::badgeText },
            { "clock", &PromptColorOverrides::clock },
        };
        for (auto const& [colorKey, fnName]: frameOverridesStorage->colorFns)
        {
            auto resolvedStr = evaluateDynamicCallback(fnName);
            if (!resolvedStr)
                continue;
            auto spec = parseColorSpec(*resolvedStr);
            if (!spec)
                continue;
            for (auto const& [mapKey, field]: colorFieldMap)
                if (mapKey == colorKey)
                {
                    (*frameOverridesStorage).*field = *spec;
                    break;
                }
        }
    }

    // Resolve prompt colors: merge overrides with theme defaults.
    // Stored as a local; the pointer in _context is valid only for this render frame.
    auto const resolved = resolvePromptColors(*overridesForResolve, theme.promptColors);
    _context.resolvedColors = &resolved;
    // Guard: null out after render scope (see end of this function).

    // Resolve concrete background (nullptr when transparent)
    auto const* concreteBg = std::get_if<tui::RgbColor>(&resolved.background);

    // Build aurora background color cache when configured
    auto const hasAurora = !_config.auroraBackground.empty() && !_config.colorOverrides.transparentBackground;
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
    // Returns the aurora bg color at the given column, or the flat bg (nullopt when transparent).
    auto const bgAt = [&](int col) -> std::optional<tui::RgbColor> {
        auto const idx = col - HorizontalMargin;
        if (!bgColors.empty() && idx >= 0 && std::cmp_less(idx, bgColors.size()))
            return bgColors[static_cast<std::size_t>(idx)];
        if (concreteBg)
            return *concreteBg;
        return std::nullopt;
    };

    // Create styles
    auto const dimChrome = !_terminalFocused; // Dim decorative elements when terminal unfocused

    tui::Style bgStyle;
    if (concreteBg)
        bgStyle.bg = *concreteBg;

    tui::Style leftBarStyle;
    leftBarStyle.fg = resolved.separator.solid();
    if (concreteBg)
        leftBarStyle.bg = *concreteBg;
    leftBarStyle.dim = dimChrome;

    tui::Style promptStyle;
    promptStyle.fg = resolved.badgeText.solid();
    if (concreteBg)
        promptStyle.bg = *concreteBg;
    promptStyle.dim = dimChrome;

    tui::Style ghostStyle;
    ghostStyle.fg = resolved.badgeText.solid();
    if (concreteBg)
        ghostStyle.bg = *concreteBg;
    ghostStyle.dim = true;

    // Helper: apply optional background to a style
    auto const applyBg = [](tui::Style& style, std::optional<tui::RgbColor> const& bg) {
        if (bg)
            style.bg = *bg;
    };

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
        // Track input changes for input-sensitive modules.
        if (auto const inputText = _inputField.text(); inputText != _lastModuleInput)
        {
            _lastModuleInput = std::string(inputText);
            _context.currentInput = _lastModuleInput;
            _pendingChanges |= ModuleSensitivity::InputChange;
        }

        // Per-module selective re-evaluation: only modules sensitive to pending changes are updated.
        auto const timerExpired =
            _nextModuleRefresh && std::chrono::steady_clock::now() >= *_nextModuleRefresh;
        if (_pendingChanges != ModuleSensitivity::None || timerExpired)
        {
            updateModuleCache(_config.infoLineModules, _pendingChanges, timerExpired);
            updateModuleCache(_config.rightPromptModules, _pendingChanges, timerExpired);
            _cachedInfoModules = buildModuleVector(_config.infoLineModules);
            _cachedRightModules = buildModuleVector(_config.rightPromptModules);
            _pendingChanges = ModuleSensitivity::None;
            _nextModuleRefresh = computeModuleRefreshDeadline();
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
        else if (concreteBg)
        {
            canvas.fill(
                tui::Rect { .x = HorizontalMargin, .y = infoLineRow, .width = contentWidth, .height = 1 },
                ' ',
                bgStyle);
        }

        auto col = HorizontalMargin;

        // Info line separator
        if (_config.separator == SeparatorStyle::Bar)
        {
            applyBg(leftBarStyle, bgAt(col));
            col += canvas.putString(infoLineRow, col, "\xe2\x96\x8e", leftBarStyle); // U+258E
            tui::Style spStyle;
            applyBg(spStyle, bgAt(col));
            canvas.put(infoLineRow, col, " ", spStyle);
            ++col;
        }
        else if (_config.separator == SeparatorStyle::Rounded)
        {
            tui::Style sepStyle;
            sepStyle.fg = resolved.separator.solid();
            applyBg(sepStyle, bgAt(col));
            sepStyle.dim = dimChrome;
            col += canvas.putString(infoLineRow, col, "\xe2\x95\xad", sepStyle); // U+256D ╭
            applyBg(sepStyle, bgAt(col));
            col += canvas.putString(infoLineRow, col, "\xe2\x94\x80", sepStyle); // U+2500 ─
            tui::Style spStyle;
            applyBg(spStyle, bgAt(col));
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
                    applyBg(spStyle, bgAt(col));
                    canvas.put(infoLineRow, col, " ", spStyle);
                    ++col;
                    tui::Style dimPipeStyle;
                    dimPipeStyle.fg = resolved.separator.solid();
                    applyBg(dimPipeStyle, bgAt(col));
                    dimPipeStyle.dim = true;
                    col += canvas.putString(infoLineRow, col, "\xe2\x94\x82", dimPipeStyle); // U+2502 │
                    applyBg(spStyle, bgAt(col));
                    canvas.put(infoLineRow, col, " ", spStyle);
                    ++col;
                }
                else
                {
                    tui::Style spStyle;
                    applyBg(spStyle, bgAt(col));
                    canvas.put(infoLineRow, col, " ", spStyle);
                    ++col;
                }
            }
            for (auto const& seg: infoModules[i])
            {
                auto segStyle = seg.style;
                applyBg(segStyle, bgAt(col));
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
                        applyBg(spStyle, bgAt(rightCol));
                        canvas.put(infoLineRow, rightCol, " ", spStyle);
                        ++rightCol;
                    }
                    for (auto const& seg: rightModules[i])
                    {
                        auto segStyle = seg.style;
                        applyBg(segStyle, bgAt(rightCol));
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
        else if (concreteBg)
        {
            canvas.fill(tui::Rect { .x = HorizontalMargin, .y = row, .width = contentWidth, .height = 1 },
                        ' ',
                        bgStyle);
        }

        // Draw separator on input lines
        if (_config.separator == SeparatorStyle::Bar)
        {
            auto barStyle = leftBarStyle;
            applyBg(barStyle, bgAt(HorizontalMargin));
            canvas.put(row, HorizontalMargin, "\xe2\x96\x8e", barStyle); // U+258E
        }
        else if (_config.separator == SeparatorStyle::Rounded)
        {
            tui::Style sepStyle;
            sepStyle.fg = resolved.separator.solid();
            applyBg(sepStyle, bgAt(HorizontalMargin));
            sepStyle.dim = dimChrome;
            if (lineIndex == 0)
            {
                canvas.putString(row, HorizontalMargin, "\xe2\x95\xb0", sepStyle); // U+2570 ╰
                applyBg(sepStyle, bgAt(HorizontalMargin + 1));
                canvas.putString(row, HorizontalMargin + 1, "\xe2\x94\x80", sepStyle); // U+2500 ─
            }
            else
            {
                canvas.putString(row, HorizontalMargin, "\xe2\x94\x82", sepStyle); // U+2502 │
            }
        }
        else if (_config.separator == SeparatorStyle::None)
        {
            tui::Style spStyle;
            applyBg(spStyle, bgAt(HorizontalMargin));
            canvas.put(row, HorizontalMargin, " ", spStyle);
        }

        // Padding after separator
        {
            auto const padCol = HorizontalMargin + leftBarWidth();
            tui::Style padStyle;
            applyBg(padStyle, bgAt(padCol));
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

    // Set up InputField with prompt, continuation, ghost text style, and decorator.
    // When a dynamic indicator function is configured, the override computed above
    // (via evaluateDynamicCallback) replaces the static `_promptStr` for this frame.
    _inputField.setPrompt(_context.indicatorOverride.value_or(_promptStr));
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
    _decorator.flatBg = concreteBg ? *concreteBg : tui::RgbColor {};
    _decorator.transparentBg = !concreteBg && !hasAurora;
    _decorator.bgOffset = fieldOriginCol - HorizontalMargin; // Map field col 0 to aurora col offset
    _decorator.theme = &theme;
    _inputField.setTextDecorator(&_decorator);

    // Render InputField into a subcanvas that starts after the left chrome
    auto const fieldArea = tui::Rect {
        .x = fieldOriginCol,
        .y = inputStartRow,
        .width = canvasWidth - fieldOriginCol - HorizontalMargin,
        .height = std::min(totalLines, canvas.height() - inputStartRow),
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
            auto popupRect = tui::Rect { .x = totalPromptWidth, // x (column) - where prompt ends
                                         .y = popupRow,         // y (row) - below or above cursor
                                         .width = std::min(popupSize.width,
                                                           canvasWidth - totalPromptWidth - HorizontalMargin),
                                         .height = popupHeight };

            _completionPopup.setArea(popupRect);
            auto popupCanvas = canvas.subcanvas(popupRect);
            _completionPopup.render(popupCanvas);
        }
    }

    // Render command palette (centered, below input area)
    if (_commandPalette.visible())
    {
        auto const palettePrefSize = _commandPalette.preferredSize();
        auto const paletteWidth = std::min(palettePrefSize.width, canvasWidth);
        auto const paletteRow = inputStartRow + totalLines;
        auto const paletteHeight = std::min(palettePrefSize.height, canvas.height() - paletteRow);
        if (paletteHeight >= 4) // Minimum: border(2) + filter(1) + separator(1)
        {
            auto const paletteX = std::max(0, (canvasWidth - paletteWidth) / 2);
            auto const paletteRect =
                tui::Rect { .x = paletteX, .y = paletteRow, .width = paletteWidth, .height = paletteHeight };
            _commandPalette.setArea(paletteRect);
            auto paletteCanvas = canvas.subcanvas(paletteRect);
            _commandPalette.render(paletteCanvas);
        }
    }

    // Position fuzzy file finder (centered, below input area).
    // Rendering is handled by the component tree (child of PromptComponent with z-index 10).
    if (_fuzzyFileFinder.visible())
    {
        auto const finderPrefSize = _fuzzyFileFinder.preferredSize();
        auto const finderWidth = std::min(finderPrefSize.width, canvasWidth);
        auto const finderRow = inputStartRow + totalLines;
        auto const finderHeight = std::min(finderPrefSize.height, canvas.height() - finderRow);
        if (finderHeight >= 4) // Minimum: border(2) + filter(1) + separator(1)
        {
            auto const finderX = std::max(0, (canvasWidth - finderWidth) / 2);
            _fuzzyFileFinder.setArea(
                tui::Rect { .x = finderX, .y = finderRow, .width = finderWidth, .height = finderHeight });
        }
        else
        {
            _fuzzyFileFinder.hide();
        }
    }

    // Mark bottom padding rows for content height detection (NBSP at column 0)
    for (int i = 0; i < botPad; ++i)
        canvas.put(inputStartRow + totalLines + i, 0, "\xC2\xA0", {});

    // Null out the dangling pointer to the stack-local resolved colors.
    _context.resolvedColors = nullptr;
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

    // If command palette is visible, add space for it below the input
    if (_commandPalette.visible())
    {
        auto const paletteSize = _commandPalette.preferredSize();
        totalHeight += paletteSize.height;
        maxWidth = std::max(maxWidth, paletteSize.width);
    }

    // If fuzzy file finder is visible, add space for it below the input
    if (_fuzzyFileFinder.visible())
    {
        auto const finderSize = _fuzzyFileFinder.preferredSize();
        totalHeight += finderSize.height;
        maxWidth = std::max(maxWidth, finderSize.width);
    }

    return { .width = maxWidth, .height = totalHeight };
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
    return _config.promptSpacing;
}

int PromptComponent::bottomPadding() const noexcept
{
    return _config.promptSpacing;
}

int PromptComponent::cursorRowFromTop() const noexcept
{
    return topPadding() + auroraFadeHeight() + chromeHeight() + _inputField.cursorLine();
}

int PromptComponent::displayWidth(std::string_view text)
{
    int width = 0;
    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    for (auto const& cluster: segmenter)
        width += tui::graphemeClusterWidth(cluster);
    return width;
}

tui::EventResult PromptComponent::onEvent(tui::InputEvent const& event)
{
    auto const* mouse = std::get_if<tui::MouseEvent>(&event);
    if (!mouse)
        return tui::EventResult::Ignored;

    auto const action = handleMouseEvent(*mouse);
    return (action != tui::InputFieldAction::None) ? tui::EventResult::Handled : tui::EventResult::Ignored;
}

tui::InputFieldAction PromptComponent::handleMouseEvent(tui::MouseEvent const& mouse)
{
    // Convert 1-based component-relative to 0-based
    auto const compCol = mouse.x - 1;
    auto const compRow = mouse.y - 1;

    // For scroll events, pass through directly
    if (mouse.type == tui::MouseEvent::Type::ScrollUp || mouse.type == tui::MouseEvent::Type::ScrollDown)
        return _inputField.handleMouse(mouse.type, 0, 0, mouse.modifiers);

    // Compute which input line this falls on
    auto const inputLine = compRow - topPadding() - auroraFadeHeight() - chromeHeight();
    auto const totalLines = _inputField.lineCount();

    // Clamp line to valid range
    auto const clampedLine = std::clamp(inputLine, 0, std::max(0, totalLines - 1));

    // Compute text start column (prompt prefix area)
    auto const fieldOriginCol = HorizontalMargin + leftBarWidth() + PaddingAfterBar;
    auto const promptTextWidth = displayWidth(_promptStr);
    // Continuation prompt matches prompt text width by construction
    auto const textStartCol = fieldOriginCol + promptTextWidth;

    // Display column within text area (clamp to 0 if click is in prompt area)
    auto const textDisplayCol = std::max(0, compCol - textStartCol);

    // Convert display column to grapheme index by walking the line content
    auto const lineContent = _inputField.lineAt(clampedLine);
    auto graphemeIndex = 0;
    if (!lineContent.empty())
    {
        auto segmenter = unicode::utf8_grapheme_segmenter(lineContent);
        auto displayCol = 0;
        for (auto const& cluster: segmenter)
        {
            auto const w = tui::graphemeClusterWidth(cluster);
            if (displayCol + w > textDisplayCol)
                break;
            displayCol += w;
            ++graphemeIndex;
        }
    }

    return _inputField.handleMouse(mouse.type, clampedLine, graphemeIndex, mouse.modifiers);
}

PromptComponent::Action PromptComponent::processInput(tui::InputEvent const& event)
{
    // Mouse events are handled by onEvent/handleMouseEvent, not here.
    if (std::holds_alternative<tui::MouseEvent>(event))
        return Action::None;

    // Handle command palette events first (takes priority over everything)
    if (_commandPalette.visible())
    {
        auto const paletteAction = _commandPalette.processEvent(event);
        switch (paletteAction)
        {
            case tui::CommandPaletteAction::Changed: return Action::Changed;
            case tui::CommandPaletteAction::Executed:
            case tui::CommandPaletteAction::Dismissed: return Action::Changed;
        }
        return Action::Changed;
    }

    // Handle fuzzy file finder events (takes priority over InputField)
    if (_fuzzyFileFinder.visible())
    {
        auto const finderAction = _fuzzyFileFinder.processEvent(event);
        switch (finderAction)
        {
            case tui::FuzzyPickerAction::Accepted:
                if (auto const* selected = _fuzzyFileFinder.selectedItem())
                    insertCompletion(FileCompleter::escapeForShell(*selected));
                _fuzzyFileFinder.hide();
                return Action::Changed;
            case tui::FuzzyPickerAction::Dismissed: return Action::Changed;
            case tui::FuzzyPickerAction::Changed: return Action::Changed;
        }
        return Action::Changed;
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
                auto const ctx = Completer::analyzeContext(_inputField.text(), _inputField.cursor());
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
                // Escape should just close the popup without passing through to InputField
                if (auto const* key = std::get_if<tui::KeyEvent>(&event);
                    key && key->key == tui::KeyCode::Escape)
                {
                    dismissPopup();
                    return Action::Changed;
                }
                // Other keys (typed characters): let event pass through and re-filter
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
                                    _historyCandidates.emplace_back(entry);
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
                        {
                            --(*_historyCycleIndex);
                        }
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
                // Confirm the suggestion for the current text before committing it. A delete may
                // have left a re-prepended guess whose debounced recompute has not run yet;
                // recomputing here (cache-backed) ensures we accept the real suggestion, not the
                // guess, and clears it if the completer no longer offers one.
                updateGhostText();
                if (_inputField.hasGhostText())
                    _inputField.acceptGhostText();
                return Action::Changed;
            }
        }

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
        // '#' on empty input enters agent mode
        if (key->codepoint == '#' && tui::withoutLockKeys(key->modifiers) == tui::Modifier::None
            && _inputField.text().empty())
            return Action::AgentMode;
#endif
    }

    // Process through InputField
    auto action = _inputField.processEvent(event);

    switch (action)
    {
        case tui::InputFieldAction::Submit:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            _exitHintVisible = false;
            if (std::ranges::all_of(_inputField.text(), [](unsigned char c) { return std::isspace(c); }))
                return Action::None;
            return Action::Submit;
        case tui::InputFieldAction::Abort:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            _exitHintVisible = false;
            return Action::Abort;
        case tui::InputFieldAction::NewPrompt:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            _exitHintVisible = false;
            return Action::NewPrompt;
        case tui::InputFieldAction::Eof: {
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();

            if (_config.exitConfirmTimeoutMs <= 0)
                return Action::Eof; // Disabled, exit immediately

            auto const now = std::chrono::steady_clock::now();
            auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastCtrlDTime);

            if (_exitHintVisible && elapsed < std::chrono::milliseconds(_config.exitConfirmTimeoutMs))
            {
                _exitHintVisible = false;
                return Action::Eof; // Second press within timeout → exit
            }

            // First press: show hint, start timer
            _lastCtrlDTime = now;
            _exitHintVisible = true;
            _inputField.setGhostText("Press Ctrl+D again to exit, or Ctrl+L to clear");
            return Action::Changed;
        }
        case tui::InputFieldAction::AgentMode:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            return Action::AgentMode;
        case tui::InputFieldAction::CommandPalette:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            if (_commandRegistry)
                _commandPalette.show(*_commandRegistry, tui::CommandContext::Shell);
            return Action::Changed;
        case tui::InputFieldAction::FuzzyFileFinder:
            _inputField.clearGhostText();
            dismissPopup();
            resetHistoryCycling();
            triggerFuzzyFileFinder();
            return Action::Changed;
        case tui::InputFieldAction::CycleAgentMode:
        case tui::InputFieldAction::CycleThinkingMode:
        case tui::InputFieldAction::CycleModel:
            // Not applicable in shell prompt context; ignore.
            break;
        case tui::InputFieldAction::Changed:
            resetHistoryCycling();
            if (_exitHintVisible)
            {
                _exitHintVisible = false;
                _inputField.clearGhostText();
            }
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

    tui::updateGhostText(_inputField,
                         _suggestCacheText,
                         _suggestCacheResult,
                         [this](auto const& text, auto cursor) { return _completer->suggest(text, cursor); });
}

void PromptComponent::triggerCompletion(bool forceShowPopup)
{
    if (!_completer)
        return;

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Get completions
    auto completions = _completer->complete(text, cursor);

    // Capture any compilation errors from scripted completers
    auto errors = _completer->takeLastErrors();
    if (!errors.empty())
        _pendingCompletionErrors = std::move(errors);

    if (completions.empty())
    {
        dismissPopup();
        return;
    }

    // Prefix matches have empty matchPositions; fuzzy matches have non-empty matchPositions.
    auto const isPrefixMatch = [](auto const& item) {
        return item.matchPositions.empty();
    };

    if (!forceShowPopup)
    {
        if (completions.size() == 1)
        {
            // Single match (prefix or fuzzy): insert directly without showing popup
            insertCompletion(completions[0].text);
            dismissPopup();
            updateGhostText();
            return;
        }

        // If exactly one prefix match exists among multiple candidates, auto-insert it.
        // Fuzzy-only matches should not block an unambiguous prefix completion.
        if (std::ranges::count_if(completions, isPrefixMatch) == 1)
        {
            auto const it = std::ranges::find_if(completions, isPrefixMatch);
            insertCompletion(it->text);
            dismissPopup();
            updateGhostText();
            return;
        }
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
            .detail = item.detail,
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

    // Capture any compilation errors from scripted completers
    auto errors = _completer->takeLastErrors();
    if (!errors.empty())
        _pendingCompletionErrors = std::move(errors);

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
            .detail = item.detail,
            .score = item.score,
        });
    }

    // Update with selection preservation
    _completionPopup.updateItems(std::move(popupItems));
}

void PromptComponent::dismissPopup()
{
    _completionPopup.hide();
    _commandPalette.hide();
    _fuzzyFileFinder.hide();
    _historySearchMode = false;
}

void PromptComponent::triggerFuzzyFileFinder()
{
    auto tree = endo::platform::ProjectFileTree(
        endo::platform::FileTreeConfig { .maxDepth = 20, .maxEntries = 10000 });
    auto files = tree.filePaths(std::filesystem::current_path());
    if (!files.empty())
        _fuzzyFileFinder.show(std::move(files), "File> ");
}

void PromptComponent::triggerHistorySearch()
{
    if (!_history)
    {
        dismissPopup();
        return;
    }

    auto const inputText = std::string(_inputField.text());
    auto options = FuzzySearchOptions {};
    if (_envProvider)
    {
        options.currentCwd = _envProvider->currentDirectory();
        options.home = normalizedHomeDirectory(*_envProvider);
    }
    options.fs = _historyFs;
    options.validateRequiredPaths = _historyFs != nullptr;
    auto results = _history->searchFuzzy(inputText, 200, options);

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
    auto options = FuzzySearchOptions {};
    if (_envProvider)
    {
        options.currentCwd = _envProvider->currentDirectory();
        options.home = normalizedHomeDirectory(*_envProvider);
    }
    options.fs = _historyFs;
    options.validateRequiredPaths = _historyFs != nullptr;
    auto results = _history->searchFuzzy(inputText, 200, options);

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
    if (_exitHintVisible)
    {
        auto const elapsed = std::chrono::steady_clock::now() - _lastCtrlDTime;
        if (elapsed >= std::chrono::milliseconds(_config.exitConfirmTimeoutMs))
        {
            _exitHintVisible = false;
            _inputField.clearGhostText();
        }
    }
}

void PromptComponent::insertCompletion(std::string_view text)
{
    if (!_completer)
        return;

    auto const inputText = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Get the context to find what prefix to replace
    auto ctx = Completer::analyzeContext(inputText, cursor);

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

std::optional<tui::HoverResult> PromptComponent::onHover(int x, int y)
{
    auto const sourcePos = screenToSourcePosition(x, y);

    // Priority 1: Diagnostics (plain text with hints, multi-line)
    if (sourcePos)
    {
        if (auto diag = diagnosticAt(sourcePos->line, sourcePos->character))
        {
            auto tooltipText = diag->message;
            for (auto const& hint: diag->suggestions)
                tooltipText += "\nhint: " + hint;
            return tui::HoverResult {
                .text = std::move(tooltipText),
                .position = { .x = x, .y = y },
                .contentType = tui::TooltipContentType::PlainText,
            };
        }
    }

    // Priority 2: Language hover info (markdown, can be multi-line)
    if (sourcePos)
    {
        auto const text = std::string(_inputField.text());
        if (auto hover = endo::computeHover(text, *sourcePos))
        {
            return tui::HoverResult {
                .text = hover->markdownText,
                .position = { .x = x, .y = y },
                .contentType = tui::TooltipContentType::Markdown,
            };
        }
    }

    // Priority 3: Command tooltip (plain text)
    if (y == topPadding() + auroraFadeHeight() + chromeHeight() && _commandResolver)
    {
        if (auto const cmd = getCommandAtColumn(x))
        {
            auto const info = _commandResolver->resolve(*cmd);
            auto const [cmdStart, _] = getCommandBounds();
            return tui::HoverResult {
                .text = info.tooltip,
                .position = { .x = cmdStart, .y = y },
                .contentType = tui::TooltipContentType::PlainText,
            };
        }
    }

    return std::nullopt;
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
            auto const idx = ((static_cast<std::size_t>(y) * imgWidth) + x) * 4;

            // Horizontal gradient position
            auto const t = (imgWidth > 1) ? static_cast<float>(x) / static_cast<float>(imgWidth - 1) : 0.0f;
            auto const color = multiStopGradient(_config.auroraBackground, t);

            // Pre-multiply alpha: blend aurora color with terminal background
            pixels[idx + 0] = static_cast<std::uint8_t>(((color.r * a) + (bgColor.r * (255 - a))) / 255);
            pixels[idx + 1] = static_cast<std::uint8_t>(((color.g * a) + (bgColor.g * (255 - a))) / 255);
            pixels[idx + 2] = static_cast<std::uint8_t>(((color.b * a) + (bgColor.b * (255 - a))) / 255);
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
    return tui::remainingMs(_diagnosticsPendingSince, DiagnosticsDebounceMs);
}

int PromptComponent::ghostTextTimeoutMs() const
{
    return tui::remainingMs(_ghostTextPendingSince, GhostTextDebounceMs);
}

int PromptComponent::exitHintTimeoutMs() const
{
    if (!_exitHintVisible)
        return -1;
    return tui::remainingMs(_lastCtrlDTime, std::chrono::milliseconds(_config.exitConfirmTimeoutMs));
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

GitModule const* PromptComponent::gitModule() const noexcept
{
    auto const it = _modules.find("git");
    if (it == _modules.end())
        return nullptr;
    return dynamic_cast<GitModule const*>(it->second.get());
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

std::vector<std::string> PromptComponent::takePendingCompletionErrors()
{
    return std::exchange(_pendingCompletionErrors, {});
}

bool PromptComponent::hasPendingCompletionErrors() const noexcept
{
    return !_pendingCompletionErrors.empty();
}

} // namespace endo
