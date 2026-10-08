# `clip` builtin — copy to the clipboard via the terminal

Date: 2026-10-08
Status: approved design

## Goal

A builtin `clip` copies its standard input, or its arguments, to the system clipboard — the
terminal-side equivalent of `pbcopy`, `xsel` or `Set-Clipboard`. It writes only; reading the
clipboard is out of scope.

The copy travels through the terminal, never through a native OS clipboard API, so it works the
same locally, over SSH and inside containers:

- **OSC 5522** (kitty's clipboard protocol) when the terminal supports it — arbitrary MIME types,
  large payloads, and a confirmed result (`DONE` or an error code).
- **OSC 52** otherwise — plain text only, fire and forget.

## User-facing behavior

```
clip [OPTIONS] [TEXT...]

  -t, --type MIME   MIME type of the data (default: text/plain)
  -p, --primary     Copy to the primary selection instead of the clipboard
  -h, --help        Display help
```

- With positional arguments, they are copied as literal text, joined with single spaces, without a
  trailing newline. Arguments are never file names (`clip < file` copies a file).
- Without arguments, standard input is read to EOF and copied byte for byte (`echo hi | clip`
  copies `hi\n`, like `pbcopy`). `clip` typed alone waits for Ctrl+D.
- The escape sequences go to the **controlling terminal** (`/dev/tty`, `CONIN$`/`CONOUT$`), never
  to stdout, so `clip foo > f` and `x=$(clip foo)` still copy. `clip` writes nothing to stdout.
- Exit status 0 on success. On failure a message `clip: <reason>` goes to stderr and the exit
  status is 1.
- Payloads above 64 MiB (the minimum OSC 5522 must accept) are rejected as too large.
- A non-text MIME type on a terminal that only speaks OSC 52 is an error; nothing is sent.

Out of scope: reading/pasting, native OS clipboards, MIME detection from content, tmux/screen
passthrough wrapping (tmux handles OSC 52 itself with `set-clipboard on`).

## Architecture

The protocol work is generic and lives in core-cpp (`core::tui`); endo adds the builtin. core-cpp is
changed first and released; endo moves its pin.

### core-cpp: `ClipboardProtocol` (pure functions)

`src/core/tui/ClipboardProtocol.{hpp,cpp}`

- `enum class ClipboardTarget { Clipboard, Primary }`.
- `enum class ClipboardTransport { Osc5522, Osc52 }` — which protocol carried a copy.
- `enum class ClipboardWriteError { NoTerminal, UnsupportedMimeType, PermissionDenied, TooLarge,
  Busy, InvalidData, IoError, PrimaryUnavailable, NoConfirmation }`.
- `encodeOsc52(std::string_view data, ClipboardTarget) -> std::string` —
  `ESC ] 52 ; c|p ; <base64> ESC \`. `TerminalOutput::copyToClipboard()` delegates to it.
- `encodeOsc5522Write(std::string_view data, std::string_view mime, ClipboardTarget) -> std::string`
  — `OSC 5522;type=write[:loc=primary] ST`, then one
  `OSC 5522;type=wdata:mime=<base64 mime>;<base64 chunk> ST` per chunk of at most
  `Osc5522ChunkSize = 4096` raw bytes (one empty chunk for empty data), then `OSC 5522;type=wdata ST`.
- `parseOsc5522WriteStatus(std::string_view oscPayload)
  -> std::optional<std::expected<void, ClipboardWriteError>>` — `nullopt` when the payload is not a
  `5522;type=write:status=…` reply; `DONE` maps to success, an error code to its enum value.
- `describe(ClipboardWriteError) -> std::string_view` and the status-code mapping come from one
  descriptor table (enum, wire code, message):

| Error | Wire code | Message |
|---|---|---|
| `NoTerminal` | — | no controlling terminal to send the clipboard sequence to |
| `UnsupportedMimeType` | — | terminal only supports OSC 52 (plain text); cannot copy this MIME type |
| `PermissionDenied` | `EPERM` | terminal refused clipboard access (EPERM) |
| `TooLarge` | `EFBIG` | data too large for the terminal's clipboard (EFBIG) |
| `Busy` | `EBUSY` | terminal clipboard busy (EBUSY) |
| `InvalidData` | `EINVAL` | terminal rejected the clipboard data (EINVAL) |
| `PrimaryUnavailable` | `ENOSYS` | terminal has no primary selection (ENOSYS) |
| `IoError` | `EIO` | terminal reported an I/O error, or writing to it failed |
| `NoConfirmation` | — | terminal did not confirm the copy in time |

An unknown wire code maps to `IoError`.

### core-cpp: `VtParser` OSC replies (opt-in)

- New input event `OscResponse { std::string payload; }` (content between `ESC ]` and the ST or
  BEL), added to the `InputEvent` variant and to `isProtocolReport()`.
- New states `OscBody` (and ST detection), terminated by `ESC \` or BEL, capped by
  `MaxOscLength` exactly like `MaxDcsLength`.
- **Off by default.** `VtParser(VtParser::Options { .recognizeOsc = true })` turns it on. Today
  `ESC ]` is Alt+] for the prompt; recognizing OSC globally would swallow what follows Alt+]. Only
  the clipboard channel enables it.

### core-cpp: `TerminalChannel` (seam to the controlling terminal)

`src/core/tui/TerminalChannel.hpp` (+ `posix/`, `windows/` implementations)

```cpp
class TerminalChannel {
  public:
    virtual ~TerminalChannel() = default;
    [[nodiscard]] virtual auto access() const noexcept -> ChannelAccess = 0; // ReadWrite or WriteOnly
    [[nodiscard]] virtual auto write(std::string_view bytes) -> std::expected<void, ClipboardWriteError> = 0;
    [[nodiscard]] virtual auto poll(int timeoutMs) -> std::expected<std::vector<InputEvent>, ClipboardWriteError> = 0;
};
[[nodiscard]] auto openControllingTerminal() -> std::expected<std::unique_ptr<TerminalChannel>, ClipboardWriteError>;
```

- POSIX: `open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC)`; failure → `NoTerminal`. When the
  process group is the terminal's foreground group, `ICANON` and `ECHO` are cleared for the
  channel's lifetime (`ISIG` kept, so Ctrl+C still raises `SIGINT`) and restored in the
  destructor without throwing; otherwise the channel is write-only (no `SIGTTIN` for a background
  `clip`). `poll()` decodes with an OSC-enabled `VtParser`; `EINTR` is retried — the writer's
  timeouts bound every wait, and a `SIGCHLD` must not abort a copy.
- Windows: `CONIN$`/`CONOUT$`, `ENABLE_VIRTUAL_TERMINAL_INPUT` on input and
  `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on output while open, modes restored on destruction.
- `core::tui::testing::ScriptedTerminalChannel` records writes and answers them with scripted
  replies, for tests.

### core-cpp: `ClipboardWriter` (the service)

```cpp
class ClipboardWriter {
  public:
    using ChannelFactory = std::function<std::expected<std::unique_ptr<TerminalChannel>, ClipboardWriteError>()>;
    struct Timeouts { std::chrono::milliseconds probe { 500 }; std::chrono::milliseconds status { 5000 }; };
    ClipboardWriter(ChannelFactory openChannel, core::platform::IClock& clock, Timeouts timeouts = {});
    [[nodiscard]] auto write(std::string_view data, std::string_view mime, ClipboardTarget target)
        -> std::expected<ClipboardTransport, ClipboardWriteError>;
};
```

The channel is opened per `write()` and closed afterwards; nothing holds the terminal between
copies.

**Probe** (once per writer; result cached in `std::optional<bool>`):

1. Write `CSI ? 5522 $ p` followed by `CSI c` (DA1) in one write.
2. Poll until a `DeviceAttributesReport` arrives or `timeouts.probe` elapses. Terminals answer in
   order, so a DECRQM reply precedes DA1. The DA1 reply is always awaited, so no reply lingers in
   the tty for the prompt.
3. OSC 5522 is supported iff a `DecModeReport` for mode 5522 had status 1, 2 or 3.
4. A write-only channel skips the probe and uses OSC 52; the result is not cached.

**Write:**

1. Transport OSC 52 and a MIME type other than `text/plain` / `text/plain;…` →
   `UnsupportedMimeType`, nothing sent.
2. OSC 52: write `encodeOsc52(…)` → `Osc52`.
3. OSC 5522: write `encodeOsc5522Write(…)`; poll for an `OscResponse` that
   `parseOsc5522WriteStatus` accepts, ignoring other events. `DONE` → `Osc5522`; error code → the
   error; nothing within `timeouts.status` → `NoConfirmation`.

### endo: `clip` builtin

- `ClipOptions` table and one `InlineCommandDescriptor` row (`name = "clip"`,
  `usageLine = "clip [OPTIONS] [TEXT...]"`, `acceptsFileArgs = false`,
  `withStdinFn = &Shell::executeInlineClip`). Help, completion and LSP derive from it.
- `Shell::executeInlineClip(args, outputFd, stdinFd)`: parse options; build the payload from
  arguments or `stdinFd` (`interruptibleReadLoop`, stops past `MaxClipboardPayload = 64 MiB` with
  the `TooLarge` message); `_clipboardWriter->write(…)`; print `clip: <describe(error)>` and return
  1 on error.
- `Shell` owns `std::unique_ptr<core::tui::ClipboardWriter> _clipboardWriter`, by default over
  `core::tui::openControllingTerminal` and the system clock; `setClipboardWriter()` replaces it
  (the `setSixelCapability()` precedent). One writer per shell caches the probe for the session.
- While a builtin runs the prompt's `Terminal` is suspended (cooked mode); the channel saves and
  restores those attributes. Late probe replies are protocol reports the prompt already drops.

## Testing

core-cpp (Catch2):

- `ClipboardProtocol_test.cpp`: OSC 52 for both targets, empty and non-ASCII data; OSC 5522
  header, `loc=primary`, base64 MIME, chunk boundaries (0, 4096, 4097 bytes), end packet;
  `parseOsc5522WriteStatus` table over `DONE`, every code, unknown code, unrelated payloads;
  `describe()` for every value.
- `VtParser_test.cpp`: OSC with ST and with BEL, split across feeds, the length cap; default
  parser still reads `ESC ]` as Alt+].
- `ClipboardWriter_test.cpp` (scripted channel, `ManualClock`): probe selects 5522 for status
  1/2/3 and 52 for 0/4, DA1-only and silence; probe cached across writes; non-text MIME over 52;
  `DONE`/`EPERM`/`EFBIG`; `NoConfirmation`; write-only channel; open failure → `NoTerminal`.
- POSIX `TerminalChannel` over a pty pair: raw mode while open, restored afterwards; bytes reach
  the master; master replies come back as events.

endo (endo-test, `mode: shell`):

- The executor always installs a `ClipboardWriter` over a scripted channel, so a test never
  probes the developer's terminal.
- `# mock-clipboard: osc5522 | osc52 | deny | silent` (default `osc52`) selects a canned terminal
  from one table; `# expect-clipboard: <line>` (repeatable, joined with `\n`) asserts the decoded
  payload, `# expect-clipboard-type: <mime>` the MIME type.
- `tests/builtins/clip/`: arguments joined; stdin with trailing newline; `-p`; `-t text/html`
  over 5522; `-t image/png` over 52 fails; `deny` and `silent` fail with their messages;
  `clip foo > file` keeps stdout empty; `--help`.

## Rollout

1. core-cpp PR (protocol, parser, channel, writer, tests, changelog).
2. endo builds against it during development (`-DCPM_core-cpp_SOURCE=~/projects/core-cpp
   -DUSE_COMPILER_CACHE=OFF`), then pins the core-cpp commit/release that contains it.
3. endo PR (builtin, endo-test harness, tests, docs, roadmap).

## Risks

- Keys typed while a channel is open (the probe once per session, and every OSC 5522 copy's
  status wait) are consumed by the channel, and opening a readable channel discards queued input
  so a late reply to an earlier exchange cannot answer the next. Interactive endo flushes
  typeahead when its prompt resumes anyway; `endo -c` started from another shell loses it.
- A reply that arrives after its channel closed (an OSC 5522 status later than 5 s) is left in the
  terminal for whatever reads it next; the interactive prompt reads `ESC ]` as Alt+].
- Until core-cpp releases this work, endo pins core-cpp at a commit of its pull request; that pin
  must move to the release tag before endo merges.
- OSC 52 success is unconfirmed; terminals with OSC 52 disabled silently ignore the copy (documented).
- Moving endo's core-cpp pin forward may pull in unrelated core-cpp API changes that endo must
  absorb.
