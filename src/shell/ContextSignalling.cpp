// SPDX-License-Identifier: Apache-2.0
#include "ContextSignalling.hpp"

#include <algorithm>
#include <array>
#include <csignal>
#include <format>
#include <ranges>

namespace endo::shellintegration
{

namespace
{
    constexpr std::string_view St = "\033\\";

    /// Longest value, in encoded bytes, UAPI.15 allows for a field.
    constexpr std::size_t MaxEncodedValueSize = 255;

    /// The field whose oversized value is truncated rather than dropped.
    constexpr std::string_view TruncatableKey = "cmdline";

    /// Maps a signal to the `exit=` value classifying a command it ended.
    struct ExitRow
    {
        int signal;
        std::string_view exitValue;
    };

    constexpr auto ExitTable = std::array {
        ExitRow { .signal = SIGINT, .exitValue = "interrupt" },
        ExitRow { .signal = SIGSEGV, .exitValue = "crash" },
        ExitRow { .signal = SIGILL, .exitValue = "crash" },
        ExitRow { .signal = SIGFPE, .exitValue = "crash" },
        ExitRow { .signal = SIGABRT, .exitValue = "crash" },
#if !defined(_WIN32)
        ExitRow { .signal = SIGBUS, .exitValue = "crash" },
        ExitRow { .signal = SIGSYS, .exitValue = "crash" },
        ExitRow { .signal = SIGTRAP, .exitValue = "crash" },
#endif
    };

#if !defined(_WIN32)
    /// Gives a signal its symbolic name for the `signal=` field.
    struct SignalName
    {
        int signal;
        std::string_view name;
    };

    constexpr auto SignalNameTable = std::array {
        SignalName { .signal = SIGHUP, .name = "SIGHUP" },
        SignalName { .signal = SIGINT, .name = "SIGINT" },
        SignalName { .signal = SIGQUIT, .name = "SIGQUIT" },
        SignalName { .signal = SIGILL, .name = "SIGILL" },
        SignalName { .signal = SIGTRAP, .name = "SIGTRAP" },
        SignalName { .signal = SIGABRT, .name = "SIGABRT" },
        SignalName { .signal = SIGBUS, .name = "SIGBUS" },
        SignalName { .signal = SIGFPE, .name = "SIGFPE" },
        SignalName { .signal = SIGKILL, .name = "SIGKILL" },
        SignalName { .signal = SIGUSR1, .name = "SIGUSR1" },
        SignalName { .signal = SIGSEGV, .name = "SIGSEGV" },
        SignalName { .signal = SIGUSR2, .name = "SIGUSR2" },
        SignalName { .signal = SIGPIPE, .name = "SIGPIPE" },
        SignalName { .signal = SIGALRM, .name = "SIGALRM" },
        SignalName { .signal = SIGTERM, .name = "SIGTERM" },
        SignalName { .signal = SIGCHLD, .name = "SIGCHLD" },
        SignalName { .signal = SIGCONT, .name = "SIGCONT" },
        SignalName { .signal = SIGSTOP, .name = "SIGSTOP" },
        SignalName { .signal = SIGTSTP, .name = "SIGTSTP" },
        SignalName { .signal = SIGTTIN, .name = "SIGTTIN" },
        SignalName { .signal = SIGTTOU, .name = "SIGTTOU" },
        SignalName { .signal = SIGURG, .name = "SIGURG" },
        SignalName { .signal = SIGXCPU, .name = "SIGXCPU" },
        SignalName { .signal = SIGXFSZ, .name = "SIGXFSZ" },
        SignalName { .signal = SIGVTALRM, .name = "SIGVTALRM" },
        SignalName { .signal = SIGPROF, .name = "SIGPROF" },
        SignalName { .signal = SIGSYS, .name = "SIGSYS" },
    };

    /// The signal a stopped job is reported with.
    constexpr int StopSignal = SIGTSTP;

    std::optional<std::string_view> signalName(int signal)
    {
        auto const* const it = std::ranges::find(SignalNameTable, signal, &SignalName::signal);
        if (it == SignalNameTable.end())
            return std::nullopt;
        return it->name;
    }
#endif

    constexpr bool needsEscape(char ch) noexcept
    {
        return ch == '\\' || ch == ';';
    }

    /// Size of @p ch once encoded: `\x5c` and `\x3b` take four bytes, everything else one.
    constexpr std::size_t encodedWidth(char ch) noexcept
    {
        return needsEscape(ch) ? 4 : 1;
    }

    constexpr bool isControl(char ch) noexcept
    {
        return static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) == 0x7F;
    }

    constexpr bool isUtf8Continuation(char ch) noexcept
    {
        return (static_cast<unsigned char>(ch) & 0xC0U) == 0x80U;
    }

    std::string stripControls(std::string_view value)
    {
        auto result = std::string {};
        result.reserve(value.size());
        std::ranges::copy_if(value, std::back_inserter(result), [](char ch) { return !isControl(ch); });
        return result;
    }

    std::string encode(std::string_view value)
    {
        auto result = std::string {};
        result.reserve(value.size());
        for (auto const ch: value)
        {
            if (ch == '\\')
                result += "\\x5c";
            else if (ch == ';')
                result += "\\x3b";
            else
                result.push_back(ch);
        }
        return result;
    }

    /// Encoded size of the code point starting at @p start in @p value.
    std::size_t codePointWidth(std::string_view value, std::size_t start)
    {
        auto const continuations = value.substr(start + 1) | std::views::take_while(isUtf8Continuation);
        return std::ranges::fold_left(continuations,
                                      encodedWidth(value[start]),
                                      [](std::size_t sum, char ch) { return sum + encodedWidth(ch); });
    }

    /// Length of the longest prefix of @p value, ending on a UTF-8 boundary, that encodes to at most
    /// MaxEncodedValueSize bytes.
    std::size_t truncatedLength(std::string_view value)
    {
        auto used = std::size_t { 0 };
        for (auto const index: std::views::iota(std::size_t { 0 }, value.size()))
        {
            if (isUtf8Continuation(value[index]))
                continue; // counted with its code point
            auto const width = codePointWidth(value, index);
            if (used + width > MaxEncodedValueSize)
                return index;
            used += width;
        }
        return value.size();
    }

    /// Encodes one field as `key=value`, or returns an empty string when it must be left out.
    std::string encodeField(ContextField const& field)
    {
        auto const stripped = stripControls(field.value);
        auto encoded = encode(stripped);
        if (encoded.size() > MaxEncodedValueSize)
        {
            if (field.key != TruncatableKey)
                return {};
            encoded = encode(std::string_view(stripped).substr(0, truncatedLength(stripped)));
        }
        return std::format("{}={}", field.key, encoded);
    }

    std::string contextSequence(std::string_view verb,
                                std::string_view id,
                                std::span<ContextField const> fields)
    {
        auto result = std::format("\033]3008;{}={}", verb, id);
        for (auto const& field: fields)
        {
            if (auto const encoded = encodeField(field); !encoded.empty())
            {
                result.push_back(';');
                result += encoded;
            }
        }
        result += St;
        return result;
    }

    std::string_view exitValueFor(CommandOutcome const& outcome)
    {
        if (outcome.stopped)
            return "interrupt";
        if (!outcome.signal)
            return outcome.status == 0 ? "success" : "failure";
        auto const* const it = std::ranges::find(ExitTable, *outcome.signal, &ExitRow::signal);
        return it == ExitTable.end() ? "failure" : it->exitValue;
    }

} // namespace

std::string contextStartSequence(std::string_view id, std::span<ContextField const> fields)
{
    return contextSequence("start", id, fields);
}

std::string contextEndSequence(std::string_view id, std::span<ContextField const> fields)
{
    return contextSequence("end", id, fields);
}

std::string newContextId(std::function<std::uint64_t()> const& random)
{
    auto const high = random();
    auto const low = random();
    return std::format("{:016x}{:016x}", high, low);
}

std::vector<ContextField> commandEndFields(CommandOutcome const& outcome)
{
    auto fields = std::vector<ContextField> {
        { .key = "exit", .value = std::string(exitValueFor(outcome)) },
        { .key = "status", .value = std::to_string(outcome.status) },
    };
#if !defined(_WIN32)
    auto const signal = outcome.stopped ? std::optional<int> { StopSignal } : outcome.signal;
    if (signal)
        if (auto const name = signalName(*signal))
            fields.push_back({ .key = "signal", .value = std::string(*name) });
#endif
    return fields;
}

} // namespace endo::shellintegration
