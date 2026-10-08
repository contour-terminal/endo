# `clip` Builtin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `clip` builtin that copies stdin or its arguments to the clipboard through the terminal — OSC 5522 when probed as supported, OSC 52 otherwise.

**Architecture:** core-cpp gains a pure `ClipboardProtocol` (in `core::tui_output`), opt-in OSC replies in `VtParser`, a `TerminalChannel` seam to the controlling terminal (POSIX `/dev/tty`, Windows `CONIN$`/`CONOUT$`, scripted double) and a `ClipboardWriter` service that probes, encodes, sends and confirms. endo adds one descriptor row and `Shell::executeInlineClip`, injects the writer, and teaches endo-test a scripted terminal.

**Tech Stack:** C++23, CMake/CPM, Catch2, endo-test, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-08-clip-builtin-design.md`

## Global Constraints

- core-cpp repository: `~/projects/core-cpp`, branch `feature/clipboard-writer`; endo: `~/projects/endo`, branch `feature/clip-builtin`.
- `core::tui_output` depends on `core::base` only; everything in it that writes goes through `writeToDestination`. `ClipboardProtocol` only composes strings.
- OS differences are separate `posix/` and `windows/` sources chosen by CMake source lists — no `#ifdef` in logic, no `<Windows.h>` in public headers.
- No `bool` in public API where an `enum class` says it (`ChannelAccess`, `ClipboardTarget`, `ClipboardTransport`).
- Fallible functions return `std::expected`; no exceptions.
- Constants `CamelCase` without prefix; private members `_camelBack`; Doxygen `///` on public API; SPDX header line one.
- No C-style `for`; no `NOLINT`; no diagnostic pragmas.
- Timeouts: probe 500 ms, status 5000 ms. OSC 5522 chunk: 4096 raw bytes. endo payload cap: 64 MiB.
- core-cpp format with `python scripts/clang-format.py <paths> --check`; endo with `clang-format` and `endo format` for `.endo` files.
- Every commit ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`.

## Review Focus

1. A terminal that answers neither DECRQM nor DA1 (dumb/pipe-backed) — `clip` must fall back to OSC 52 after ≤ 500 ms, not hang. Covered: `ClipboardWriter_test` "silence".
2. A probe whose DA1 reply arrives split across two reads — must still be recognised. Covered: VtParser split-feed test + writer test feeding replies in two polls.
3. `clip` in a non-interactive run with no controlling terminal (CI, cron) — exit 1 with "no controlling terminal", never a crash. Covered: writer `NoTerminal` test; endo-test `mock-clipboard: none`.
4. An OSC 5522 terminal sending an unrelated OSC (e.g. a colour report) before the status — must be ignored, not taken as failure. Covered: writer test with an interleaved `OscResponse`.
5. Typing Alt+] in the interactive prompt after this change — must still be a key. Covered: VtParser default-options test.

---

## Part 1 — core-cpp

### Task 1: `ClipboardProtocol` (encoders, status parser, error table)

**Files:**
- Create: `src/core/tui/ClipboardProtocol.hpp`, `src/core/tui/ClipboardProtocol.cpp`, `src/core/tui/ClipboardProtocol_test.cpp`
- Modify: `src/core/tui/CMakeLists.txt` (add to `tui_output` HEADERS/SOURCES and `tui_output` test SOURCES), `src/core/tui/TerminalOutput.cpp:212-219` (delegate to `encodeOsc52`)

**Interfaces (Produces):**

```cpp
namespace core::tui {
enum class ClipboardTarget : std::uint8_t { Clipboard, Primary };
enum class ClipboardTransport : std::uint8_t { Osc5522, Osc52 };
enum class ClipboardWriteError : std::uint8_t {
    NoTerminal, UnsupportedMimeType, PermissionDenied, TooLarge, Busy,
    InvalidData, IoError, PrimaryUnavailable, NoConfirmation };
constexpr std::size_t Osc5522ChunkSize = 4096;
constexpr int Osc5522Mode = 5522;
[[nodiscard]] auto encodeOsc52(std::string_view data, ClipboardTarget target) -> std::string;
[[nodiscard]] auto encodeOsc5522Write(std::string_view data, std::string_view mime, ClipboardTarget target) -> std::string;
[[nodiscard]] auto parseOsc5522WriteStatus(std::string_view oscPayload)
    -> std::optional<std::expected<void, ClipboardWriteError>>;
[[nodiscard]] auto describe(ClipboardWriteError error) noexcept -> std::string_view;
[[nodiscard]] auto isPlainTextMimeType(std::string_view mime) noexcept -> bool;
}
```

- [ ] **Step 1: Write failing tests** (`ClipboardProtocol_test.cpp`, Catch2):
  - `encodeOsc52("hi", Clipboard) == "\033]52;c;aGk=\033\\"`; `Primary` → `;p;`; empty → `"\033]52;c;\033\\"`; UTF-8 `"ä"` → `w6Q=`.
  - `encodeOsc5522Write("hi", "text/plain", Clipboard)` == `"\033]5522;type=write\033\\" "\033]5522;type=wdata:mime=dGV4dC9wbGFpbg==;aGk=\033\\" "\033]5522;type=wdata\033\\"`.
  - `Primary` header is `"\033]5522;type=write:loc=primary\033\\"`.
  - Chunking: 0 bytes → exactly one data packet with empty payload; 4096 bytes → one data packet; 4097 → two, second carries base64 of one byte. Count with a helper that counts occurrences of `"type=wdata:mime="`.
  - `parseOsc5522WriteStatus` table: `"5522;type=write:status=DONE"` → success; `EPERM`→`PermissionDenied`, `EFBIG`→`TooLarge`, `EBUSY`→`Busy`, `EINVAL`→`InvalidData`, `ENOSYS`→`PrimaryUnavailable`, `EIO`→`IoError`, `EWHATEVER`→`IoError`; `"52;c;aGk="`, `"5522;type=read:status=OK"`, `"5522;type=write"` (no status) → `nullopt`. Keys may appear in any order (`"5522;status=DONE:type=write"` → success).
  - `describe()` non-empty and distinct for every enumerator (iterate `std::views::iota(0, 9)`).
  - `isPlainTextMimeType`: `text/plain`, `text/plain;charset=utf-8` true; `text/html`, `image/png`, `text/plainx` false.
- [ ] **Step 2: Run** `cmake --build --preset clang-debug --target core-cpp-tui_output-test && ctest --preset clang-debug -R tui_output` → fails to compile (header missing).
- [ ] **Step 3: Implement.** One descriptor table drives errors:

```cpp
struct ErrorRow { ClipboardWriteError error; std::string_view wireCode; std::string_view message; };
constexpr auto ErrorTable = std::array {
    ErrorRow { ClipboardWriteError::NoTerminal, {}, "no controlling terminal to send the clipboard sequence to" },
    ErrorRow { ClipboardWriteError::UnsupportedMimeType, {}, "terminal only supports OSC 52 (plain text); cannot copy this MIME type" },
    ErrorRow { ClipboardWriteError::PermissionDenied, "EPERM", "terminal refused clipboard access (EPERM)" },
    ErrorRow { ClipboardWriteError::TooLarge, "EFBIG", "data too large for the terminal's clipboard (EFBIG)" },
    ErrorRow { ClipboardWriteError::Busy, "EBUSY", "terminal clipboard busy (EBUSY)" },
    ErrorRow { ClipboardWriteError::InvalidData, "EINVAL", "terminal rejected the clipboard data (EINVAL)" },
    ErrorRow { ClipboardWriteError::IoError, "EIO", "terminal reported an I/O error, or writing to it failed" },
    ErrorRow { ClipboardWriteError::PrimaryUnavailable, "ENOSYS", "terminal has no primary selection (ENOSYS)" },
    ErrorRow { ClipboardWriteError::NoConfirmation, {}, "terminal did not confirm the copy in time" },
};
```

  Targets via a two-row table `{ target, osc52Selector ("c"/"p"), osc5522Location ("" / ":loc=primary") }`. Encoders use `core::base64::encode`; chunking iterates `std::views::chunk(data, Osc5522ChunkSize)` (empty data → one empty chunk). Status parser: strip `"5522;"`, split metadata on `':'`, each `key=value`; require `type=write` and a `status`.
  `TerminalOutput::copyToClipboard(text)` becomes `_buffer += encodeOsc52(text, ClipboardTarget::Clipboard);`.
- [ ] **Step 4: Run** tests → pass; existing `TerminalOutput_test` clipboard case still passes.
- [ ] **Step 5: Commit** `feat(tui): ClipboardProtocol encodes OSC 52 and OSC 5522 writes and parses their status`.

### Task 2: `VtParser` recognises OSC replies when asked

**Files:**
- Modify: `src/core/tui/InputEvent.hpp` (add `OscResponse`, variant, `isProtocolReport`), `src/core/tui/VtParser.hpp/.cpp`, `src/core/tui/VtParser_test.cpp`

**Interfaces (Produces):**

```cpp
struct OscResponse { std::string payload; }; // bytes between ESC ] and ST/BEL
class VtParser {
  public:
    enum class OscRecognition : std::uint8_t { AltKey, Response };
    struct Options { OscRecognition osc = OscRecognition::AltKey; };
    static constexpr std::size_t MaxOscLength = std::size_t { 64 } * 1024; // replies are small, like DCS
    VtParser() = default;
    explicit VtParser(Options options) noexcept;
};
```

- [ ] **Step 1: Failing tests:**
  - `VtParser { { .osc = OscRecognition::Response } }.feed("\033]5522;type=write:status=DONE\033\\")` → one `OscResponse` with that payload.
  - BEL terminator `"\033]52;c;aGk=\a"` → `OscResponse { "52;c;aGk=" }`.
  - Split: feed `"\033]5522;ty"` (no events) then `"pe=write:status=EPERM\033\\"` → one event.
  - Overlong: `MaxOscLength + 10` bytes without terminator, then `"x"` → no `OscResponse`, then `KeyEvent` for `x` (back in Ground).
  - Default parser: `"\033]"` → `KeyEvent { codepoint ']' , Alt }` (unchanged).
  - `isProtocolReport(OscResponse{})` is true.
- [ ] **Step 2: Run** `ctest --preset clang-debug -R 'core-cpp.tui$'` → compile failure.
- [ ] **Step 3: Implement:** add `OscBody` state; `processEscape`: `if (byte == ']' && _options.osc == OscRecognition::Response) { _oscBuf.clear(); _state = State::OscBody; return; }` before the Alt+printable branch. `processOscBody`: BEL (0x07) → emit; append byte; `ends_with(StringTerminator)` → strip and emit; else `abandonIfOverlong(_oscBuf, MaxOscBuffer)`. Add case to `feed()` switch.
- [ ] **Step 4: Run** tests → pass (whole `tui` binary: no regressions).
- [ ] **Step 5: Commit** `feat(tui): VtParser reports OSC replies as OscResponse when asked to`.

### Task 3: `TerminalChannel` seam, scripted double and `ClipboardWriter`

**Files:**
- Create: `src/core/tui/TerminalChannel.hpp`, `src/core/tui/testing/ScriptedTerminalChannel.hpp`, `src/core/tui/ClipboardWriter.hpp`, `src/core/tui/ClipboardWriter.cpp`, `src/core/tui/ClipboardWriter_test.cpp`
- Modify: `src/core/tui/CMakeLists.txt` (`tui` HEADERS/SOURCES/test SOURCES)

**Interfaces (Produces):**

```cpp
enum class ChannelAccess : std::uint8_t { ReadWrite, WriteOnly };
class TerminalChannel {
  public:
    virtual ~TerminalChannel() = default;
    [[nodiscard]] virtual auto access() const noexcept -> ChannelAccess = 0;
    [[nodiscard]] virtual auto write(std::string_view bytes) -> std::expected<void, ClipboardWriteError> = 0;
    [[nodiscard]] virtual auto poll(int timeoutMs) -> std::expected<std::vector<InputEvent>, ClipboardWriteError> = 0;
};
using TerminalChannelResult = std::expected<std::unique_ptr<TerminalChannel>, ClipboardWriteError>;
[[nodiscard]] auto openControllingTerminal() -> TerminalChannelResult; // defined in Task 4

class ClipboardWriter {
  public:
    using ChannelFactory = std::function<TerminalChannelResult()>;
    struct Timeouts { std::chrono::milliseconds probe { 500 }; std::chrono::milliseconds status { 5000 }; };
    ClipboardWriter(ChannelFactory openChannel, core::platform::IClock& clock, Timeouts timeouts = {});
    [[nodiscard]] auto write(std::string_view data, std::string_view mime, ClipboardTarget target)
        -> std::expected<ClipboardTransport, ClipboardWriteError>;
};

namespace testing {
/// Scripted terminal: each write() is recorded; `respond(predicate-on-written, replies)` rules
/// queue InputEvents to be returned by later polls; poll() advances the ManualClock by its timeout
/// when nothing is queued, so timeouts elapse without real time.
class ScriptedTerminalChannel final : public TerminalChannel { ... };
struct ScriptedTerminal {           // shared state outliving each per-write channel
    ChannelAccess access = ChannelAccess::ReadWrite;
    std::optional<int> decModeStatus;          // reply to CSI ? 5522 $ p, nullopt = silent
    bool answersDeviceAttributes = true;       // reply to CSI c
    std::optional<std::string> writeStatus;    // "DONE"/"EPERM"/…, nullopt = silent
    std::vector<std::string> written;          // every write, in order
    int opens = 0;
    [[nodiscard]] auto factory(core::platform::ManualClock& clock) -> ClipboardWriter::ChannelFactory;
    [[nodiscard]] auto decodedPayload() const -> std::string;   // OSC 52 or 5522 payload, decoded
    [[nodiscard]] auto mimeType() const -> std::string;         // "" for OSC 52
    [[nodiscard]] auto target() const -> std::optional<ClipboardTarget>; // of the last copy
};
}
```

  `ScriptedTerminal` keeps the scripted double data-driven (one struct answers every protocol case) and is what endo-test reuses.

- [ ] **Step 1: Failing tests** (`ClipboardWriter_test.cpp`, `ManualClock`, `ScriptedTerminal`):
  - status 2 → `write("hi","text/plain",Clipboard)` returns `Osc5522` when `writeStatus="DONE"`; `written[0] == "\033[?5522$p\033[c"`; `written[1]` starts with `"\033]5522;type=write"`.
  - status 1 and 3 → `Osc5522`; status 0 and 4 → `Osc52`, `written[1] == encodeOsc52(...)`.
  - DA1 only (`decModeStatus=nullopt`) → `Osc52`, and clock advanced < 500 ms (returned at DA1, not timeout).
  - total silence (`answersDeviceAttributes=false`, no DECRQM) → `Osc52`, clock advanced ≥ 500 ms.
  - probe cached: two writes → exactly one `"\033[?5522$p"` among `written`; `opens == 2`.
  - non-text MIME over OSC 52 → `UnsupportedMimeType`, `written.size() == 1` (probe only).
  - `writeStatus="EPERM"` → `PermissionDenied`; `"EFBIG"` → `TooLarge`; `nullopt` → `NoConfirmation` after ≥ 5000 ms.
  - an unrelated `OscResponse{"11;rgb:0000/0000/0000"}` queued before `DONE` → still `Osc5522`.
  - write-only channel → no probe written, `Osc52`, and a later read-write writer would probe (not cached).
  - factory returning `NoTerminal` → `write` returns `NoTerminal`.
- [ ] **Step 2: Run** → compile failure.
- [ ] **Step 3: Implement `ClipboardWriter::write`:**

```cpp
auto ClipboardWriter::write(std::string_view data, std::string_view mime, ClipboardTarget target)
    -> std::expected<ClipboardTransport, ClipboardWriteError>
{
    return _openChannel().and_then([&](std::unique_ptr<TerminalChannel> channel) {
        return transportFor(*channel).and_then([&](ClipboardTransport transport) {
            return send(*channel, transport, data, mime, target);
        });
    });
}
```

  `transportFor`: `WriteOnly` → `Osc52`; cached → cached; else probe: `write("\033[?5522$p\033[c")`, then `awaitEvent(channel, _timeouts.probe, isDa1)` collecting a `DecModeReport{mode==5522}` seen on the way; supported iff status ∈ {1,2,3}. `send`: OSC 52 with non-plain-text MIME → `UnsupportedMimeType`; OSC 52 → write `encodeOsc52`; OSC 5522 → write `encodeOsc5522Write`, `awaitEvent(channel, _timeouts.status, isWriteStatus)` → map. `awaitEvent` loops `poll(remaining)` until deadline on `_clock` (rounded up to ms, like `Terminal::awaitReport`).
- [ ] **Step 4: Run** → pass.
- [ ] **Step 5: Commit** `feat(tui): ClipboardWriter probes OSC 5522 and copies through a TerminalChannel`.

### Task 4: Platform `TerminalChannel` implementations

**Files:**
- Create: `src/core/tui/posix/TerminalChannel.cpp`, `src/core/tui/windows/TerminalChannel.cpp`, `src/core/tui/posix/TerminalChannel_test.cpp`
- Modify: `src/core/tui/CMakeLists.txt` (`SOURCES_POSIX`, `SOURCES_WINDOWS`, test `SOURCES_POSIX`)

- POSIX `openControllingTerminal()`: `open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC)`; `-1` → `NoTerminal`. Access is `ReadWrite` iff `tcgetpgrp(fd) == getpgrp()`; then save termios, clear `ICANON | ECHO`, `VMIN=0`, `VTIME=0`, `tcsetattr(TCSANOW)`. Destructor restores (TCSANOW) and closes; ignores failures. `write` → `safeWrite` (`-1` → `IoError`). `poll(timeoutMs)`: `::poll` on the fd; `EINTR` → return empty vector (caller's deadline loop retries); readable → `safeRead` into 4 KiB, feed `VtParser { { .osc = Response } }`.
- Testable core: `posix/TerminalChannelPosix.hpp` (internal, not in the FILE_SET) declares `openTerminalChannel(char const* path, std::optional<ChannelAccess> access) -> TerminalChannelResult`; `openControllingTerminal()` is `openTerminalChannel("/dev/tty", std::nullopt)` (access derived from `tcgetpgrp`). The pty test passes `ChannelAccess::ReadWrite` because a pty opened with `O_NOCTTY` is not the test's controlling terminal.
- Windows: `CreateFileW(L"CONIN$"/L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)`; failure → `NoTerminal`. Save modes; input `ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_PROCESSED_INPUT`, output `|= ENABLE_VIRTUAL_TERMINAL_PROCESSING`. `poll`: `WaitForSingleObject(in, timeout)`, `ReadConsoleInputW`, key-down chars through `detail::Utf16ToUtf8` into the OSC-enabled parser. Always `ReadWrite`.
- [ ] **Step 1: Failing pty test** (`posix/TerminalChannel_test.cpp`): open a pty pair (`posix_openpt`, as `TerminalHangup_test.cpp` does), `openTerminalChannel(slaveName)`; assert slave `ICANON` cleared while open and restored after destruction; `channel->write("abc")` readable on master; master writes `"\033[?5522;2$y"` → `poll(1000)` yields `DecModeReport{5522,2}`; opening `/nonexistent` → `NoTerminal`. (Foreground check: a pty opened with `O_NOCTTY` is not our controlling tty, so `tcgetpgrp` fails → the test helper takes an `ChannelAccess` override argument; production derives it.)
- [ ] **Step 2–4:** implement, run `ctest --preset clang-debug -R 'core-cpp.tui$'` → pass.
- [ ] **Step 5: Commit** `feat(tui): openControllingTerminal opens /dev/tty or the Windows console as a TerminalChannel`.

### Task 5: core-cpp changelog, checks, PR

- [ ] CHANGELOG `[Unreleased]` → `### Added`: ClipboardProtocol, OscResponse/VtParser options, TerminalChannel/openControllingTerminal, ClipboardWriter, ScriptedTerminal. `### Changed`: `TerminalOutput::copyToClipboard` shares `encodeOsc52` (bytes unchanged).
- [ ] `python scripts/clang-format.py <touched files> --check`; `clang-tidy` preset build; `clang-debug` + `gcc-release` build and ctest; `mkdocs build --strict`.
- [ ] Push `feature/clipboard-writer`, `gh pr create` with "Consumer impact" (endo adds `clip`; others: none — additive, `InputEvent` gains an alternative so exhaustive `std::visit` users must handle `OscResponse`: grep contour/endo/tuidu).
- [ ] Drive CI green.

## Part 2 — endo

### Task 6: Move endo onto the core-cpp branch

- [ ] Configure endo against the checkout: `cmake --preset clang-debug -DCPM_core-cpp_SOURCE=$HOME/projects/core-cpp -DUSE_COMPILER_CACHE=OFF`; build; fix any API drift between v0.5.0 and the branch (commit separately as `build: core-cpp …`).
- [ ] `cmake/EndoThirdParties.cmake`: `GIT_TAG <core-cpp branch commit sha>`, `VERSION 0.7.0` (bump to the release tag once core-cpp releases).
- [ ] Commit `build: pin core-cpp at the commit that adds ClipboardWriter`.

### Task 7: `clip` builtin

**Files:**
- Modify: `src/shell/builtins/InlineCommandDescriptors.cpp` (ClipOptions + row between `cat` and `cp`), `src/shell/Shell.hpp` (decl `executeInlineClip`, `setClipboardWriter`, member), `src/shell/Shell.cpp` (default writer in constructors' shared init), `src/shell/builtins/InlineCommands.cpp` (implementation)

**Interfaces:** `int Shell::executeInlineClip(CoreVM::CoreStringArray const& args, core::platform::NativeHandle outputFd, core::platform::NativeHandle stdinFd);` `void Shell::setClipboardWriter(std::unique_ptr<core::tui::ClipboardWriter> writer);`

```cpp
int Shell::executeInlineClip(CoreVM::CoreStringArray const& args,
                             core::platform::NativeHandle outputFd,
                             core::platform::NativeHandle stdinFd)
{
    auto const& descriptor = *findInlineBuiltin("clip");
    auto const parsed = parseInlineArgs(args, descriptor.options);
    if (parsed.helpRequested)
        return renderMarkdownHelp(outputFd, generateInlineHelp(descriptor));

    auto payload = std::string {};
    if (!parsed.positionalArgs.empty())
        payload = parsed.positionalArgs | std::views::join_with(' ') | std::ranges::to<std::string>();
    else
    {
        auto tooLarge = false;
        auto const exitCode = interruptibleReadLoop(stdinFd, [&](char const* buf, size_t len) {
            if (payload.size() + len > MaxClipboardPayload) { tooLarge = true; return; }
            payload.append(buf, len);
        });
        if (exitCode != 0) return exitCode;
        if (tooLarge) { error("clip: {}", core::tui::describe(core::tui::ClipboardWriteError::TooLarge)); return 1; }
    }

    auto const mime = parsed.getFlagValue("-t").value_or("text/plain");
    auto const target = parsed.hasFlag("-p") ? core::tui::ClipboardTarget::Primary : core::tui::ClipboardTarget::Clipboard;
    return _clipboardWriter->write(payload, mime, target)
        .transform([](auto) { return 0; })
        .or_else([this](core::tui::ClipboardWriteError e) -> std::expected<int, core::tui::ClipboardWriteError> {
            error("clip: {}", core::tui::describe(e));
            return 1;
        })
        .value();
}
```

- [ ] Build, smoke-run `echo hi | ./build/clang-debug/src/shell/endo -c clip` in a real terminal.
- [ ] Commit `feat(shell): clip copies stdin or its arguments to the clipboard via the terminal`.

### Task 8: endo-test scripted clipboard + E2E tests

**Files:**
- Modify: `src/endo-test/TestFileParser.hpp/.cpp` (`mockClipboard`, `expectedClipboard`, `expectedClipboardType`, `expectedClipboardTarget`), `src/endo-test/TestExecutor.cpp` (shell mode installs writer over `core::tui::testing::ScriptedTerminal`, checks expectations), `src/endo-test/main.cpp` (directive help), `AGENT.md` (directive table)
- Create: `tests/builtins/clip/*.endo`

- `mock-clipboard` table: `{ "osc52", {.decModeStatus = 0, .writeStatus = nullopt} }`, `{ "osc5522", {.decModeStatus = 2, .writeStatus = "DONE"} }`, `{ "deny", {.decModeStatus = 2, .writeStatus = "EPERM"} }`, `{ "silent", {.decModeStatus = 2, .writeStatus = nullopt} }`, `{ "none", factory returns NoTerminal }`. Writer `Timeouts { .probe = 500ms, .status = 5000ms }` on a `ManualClock`, so `silent` costs no real time.
- Tests (each `# mode: shell`):
  - `args.endo`: `clip hello world` → `# expect-clipboard: hello world`.
  - `stdin.endo`: `echo hi | clip` → `# expect-clipboard: hi\n`. The executor compares exact bytes: `expect-clipboard` lines are joined with a newline and the escapes `\n` and `\\` in a value are unescaped, so a trailing newline is assertable.
  - `primary.endo`: `clip -p sel` with `osc5522` → `# expect-clipboard: sel`, `# expect-clipboard-target: primary` (executor reads `loc=primary` / `;p;` from the written bytes via `ScriptedTerminal::target()`).
  - `html-5522.endo`: `clip -t text/html '<b>x</b>'` with `osc5522` → payload and `expect-clipboard-type: text/html`.
  - `image-osc52.endo`: `clip -t image/png x` → `expect-exit: 1`, `expect: clip: terminal only supports OSC 52 (plain text); cannot copy this MIME type`.
  - `deny.endo`, `silent.endo`, `no-terminal.endo` → exit 1 with their messages.
  - `redirect.endo`: `clip foo > /dev/null; echo done` → `expect: done`, `expect-clipboard: foo`.
  - `help.endo`: `clip --help` → `expect-nonempty`.
- [ ] `endo format` each test; run `ctest --preset clang-debug -R clip`.
- [ ] Commit `test(endo-test): a scripted terminal for clip, and clip's E2E tests`.

### Task 9: Docs, roadmap, full suite, PR

- [ ] `docs/shell/builtins.md`: `clip` section (usage, options, OSC 5522 vs 52, MIME limit, controlling terminal, tmux `set-clipboard on`, exit status).
- [ ] `ROADMAP.md`: checked item under the shell builtins section. `docs/releases.md` if it carries an unreleased section.
- [ ] `clang-format` touched C++; full `ctest --preset clang-debug`; build `clang-debug-agent` too (Shell.cpp changed).
- [ ] `/simplify` pass.
- [ ] Push, `gh pr create` (summary, performance/risk, coverage), drive CI green; after core-cpp merges, re-pin to the merge commit or release tag.
