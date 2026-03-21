// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/output/TableFormatter.hpp>

#include <endo-language/builtins/BuiltinImpls.hpp>

#include <http/HttpClient.hpp>

#include <tui/MarkdownRenderer.hpp>
#include <tui/TerminalOutput.hpp>

#include <CoreVM/types/TypedObject.hpp>

#include <filesystem>
#include <format>

#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

namespace
{

using endo::http::HttpClient;
using endo::http::HttpRequest;

/// Performs the common fetch logic: downloads URL to a file, returns Result (Ok filename / Error msg).
void executeFetch(CoreVM::Params& args,
                  std::string const& url,
                  std::vector<std::string> headers,
                  bool interactive,
                  endo::TTY const& tty,
                  endo::FileSystem const& fs)
{
    auto* runner = args.caller();

    // Determine output filename from URL, or generate a unique one
    auto filename = endo::http::extractFilenameFromUrl(url);
    auto outputPath = fs.currentPath();

    if (filename)
    {
        outputPath /= *filename;
    }
    else
    {
        // Generate unique filename via mkstemp
        auto pattern = (outputPath / "fetch_XXXXXX").string();
#if defined(_WIN32)
        auto const fd = _mktemp_s(pattern.data(), pattern.size() + 1);
        if (fd != 0)
#else
        auto const fd = mkstemp(pattern.data());
        if (fd == -1)
#endif
        {
            auto* resultObj = runner->makeErrorResult(
                reinterpret_cast<uintptr_t>(runner->newString("Failed to create temporary file")),
                CoreVM::LiteralType::String);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(resultObj)));
            return;
        }
#if !defined(_WIN32)
        close(fd);
#endif
        outputPath = pattern;
    }

    HttpClient client;
    HttpRequest request {
        .url = url,
        .headers = std::move(headers),
    };

    // Progress bar on stderr when interactive, stderr is a TTY, and not suppressed
    bool const showProgress = interactive && tty.isStderrTerminal() && getenv("ENDO_FETCH_QUIET") == nullptr;

    bool progressWritten = false;
    if (showProgress)
    {
        request.progressCallback = [&tty, &progressWritten](size_t total, size_t now) -> bool {
            if (total > 0)
            {
                progressWritten = true;
                auto const pct = static_cast<int>((now * 100) / total);
                auto const barWidth = 30;
                auto const filled = (pct * barWidth) / 100;
                std::string bar(static_cast<size_t>(filled), '=');
                if (filled < barWidth)
                {
                    bar += '>';
                    bar.append(static_cast<size_t>(barWidth - filled - 1), ' ');
                }
                tty.writeToStderr(std::format("\r[{}] {}%", bar, pct));
            }
            else if (now > 0)
            {
                progressWritten = true;
                tty.writeToStderr(std::format("\rfetch: {} bytes received", now));
            }
            return true;
        };
    }

    auto result = client.download(request, outputPath);

    // Clear progress bar only if progress output was actually written
    if (progressWritten)
        tty.writeToStderr(std::format("\r{}\r", std::string(60, ' ')));

    CoreVM::TypedObject* resultObj = nullptr;

    if (result.has_value() && result->statusCode >= 200 && result->statusCode < 300)
    {
        // Ok(filename) — return the relative filename
        resultObj = runner->makeOkResult(
            reinterpret_cast<uintptr_t>(runner->newString(outputPath.filename().string())),
            CoreVM::LiteralType::String);
    }
    else if (result.has_value())
    {
        // HTTP error (non-2xx): delete the file and return Error(message)
        auto const removeResult = fs.remove(outputPath);
        (void) removeResult;
        auto msg = std::format("HTTP {}", result->statusCode);
        resultObj = runner->makeErrorResult(reinterpret_cast<uintptr_t>(runner->newString(msg)),
                                            CoreVM::LiteralType::String);
    }
    else
    {
        // Curl/transport error: partial file already deleted by download(), return Error(message)
        resultObj =
            runner->makeErrorResult(reinterpret_cast<uintptr_t>(runner->newString(result.error().message)),
                                    CoreVM::LiteralType::String);
    }

    args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(resultObj)));
}

} // namespace

namespace endo
{

void Shell::builtinPrint(CoreVM::Params& context)
{
    std::string const& text = context.getString(1);
    NativeHandle const outputFd =
        _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
    [[maybe_unused]] auto written = platformWrite(outputFd, text.data(), text.size());
}

void Shell::builtinPrintln(CoreVM::Params& context)
{
    std::string const& text = context.getString(1);
    NativeHandle const outputFd =
        _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
    [[maybe_unused]] auto written = platformWrite(outputFd, text.data(), text.size());
    written = platformWrite(outputFd, "\n", 1);
}

void Shell::builtinMarkdownRender(CoreVM::Params& context)
{
    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(context.getInt(1)));
    auto const* content =
        reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(0)));
    if (!content)
        return;

    NativeHandle const outputFd =
        _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);

    if (isTerminal(outputFd))
    {
        tui::TerminalOutput termOutput;
        tui::MarkdownRenderer renderer(termOutput);
        renderer.render(*content);
        termOutput.flush();
        return;
    }
    // Fallback: write raw markdown text
    auto const& text = *content;
    [[maybe_unused]] auto written = platformWrite(outputFd, text.data(), text.size());
    written = platformWrite(outputFd, "\n", 1);
}

void Shell::builtinDisplayResult(CoreVM::Params& context)
{
    auto rawVal = static_cast<uint64_t>(context.getInt(1));
    auto* runner = context.caller();
    NativeHandle const outputFd =
        _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);

    bool const useColor = isTerminal(outputFd);

    // Check if this is a Markdown object — render with terminal formatting
    if (runner->isKnownObject(rawVal))
    {
        auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawVal));
        if (obj->type->id == CoreVM::BuiltinTypeId::Markdown)
        {
            auto const* content =
                reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(0)));
            if (content)
            {
#if !defined(_WIN32)
                if (useColor)
                {
                    tui::TerminalOutput termOutput;
                    tui::MarkdownRenderer renderer(termOutput);
                    renderer.render(*content);
                    termOutput.flush();
                    return;
                }
#endif
                auto const& text = *content;
                [[maybe_unused]] auto written = platformWrite(outputFd, text.data(), text.size());
                written = platformWrite(outputFd, "\n", 1);
            }
            return;
        }
    }

    // Check if this is a list of records — if so, render as table
    if (runner->isKnownObject(rawVal))
    {
        auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawVal));
        if (obj->type->id == CoreVM::BuiltinTypeId::List && isListOfRecords(obj, runner))
        {
            TableConfig config;
            config.style = useColor ? TableStyle::Bordered : TableStyle::Plain;
            config.useColor = useColor;
            config.showIcons = _lsIcons;
            config.showDirectorySlash = _lsDirectorySlash;
            // Let the name column auto-grow for FileInfo records (ls output)
            auto* firstElem = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(obj->getSlot(0)));
            if (firstElem->type->id == CoreVM::BuiltinTypeId::FileInfo)
                config.autoGrowColumn = 0;
            if (useColor)
            {
                if (auto size = _tty.getSize(); size.has_value())
                    config.terminalWidth = size->cols;
            }
            auto table = formatRecordTable(obj, runner, config);
            [[maybe_unused]] auto written = platformWrite(outputFd, table.data(), table.size());
            return;
        }
    }

    // Check if the value is a raw string pointer — quote it for display
    if (runner->isKnownString(rawVal))
    {
        auto const* coreStr = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(rawVal));
        auto str = std::format("\"{}\"\n", std::string_view(*coreStr));
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
        return;
    }

    // Fallback: convert to string and print with newline
    auto str = endo::builtins::valueToString(rawVal, runner);
    str += '\n';
    [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void Shell::builtinFetch(CoreVM::Params& context)
{
    auto const& url = context.getString(1);
    executeFetch(context, url, {}, _interactive, _tty, _fs);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void Shell::builtinFetchWithHeaders(CoreVM::Params& context)
{
    auto const& url = context.getString(1);
    auto* headersList = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(context.getInt(2)));

    // Walk the linked list to extract header strings
    std::vector<std::string> headers;
    auto* cur = headersList;
    while (cur && cur->tag == 1) // tag 1 = Cons
    {
        auto const slot0 = cur->getSlot(0);
        auto const* str = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(slot0));
        if (str)
            headers.emplace_back(*str);
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }

    executeFetch(context, url, std::move(headers), _interactive, _tty, _fs);
}

} // namespace endo
