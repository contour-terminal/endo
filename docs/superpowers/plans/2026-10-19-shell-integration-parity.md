# Shell Integration Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** endo sends the shell-integration sequences terminals widely understand, with the prompt-start mark on the prompt's first visible line.

**Architecture:** core-cpp gains *row marks*: a component attaches raw zero-width sequences to (row, column) positions of its canvas, and the inline `Screen` emits them exactly where and whenever it writes into that part of the row. endo's `PromptComponent` declares its semantic zones (prompt start, prompt, continuation, right prompt, input) as row marks; a pure sequence layer turns zones, commands, and contexts into bytes with a data table of terminal quirks; a stateful `ShellIntegration` emitter owns the session state (enabled flag, OSC 3008 ids, user vars) and writes through the injected `TTY`.

**Tech Stack:** C++23, CMake presets, Catch2, endo-test, core-cpp (`core::tui`, `core::platform`, `core::base64`).

**Spec:** `docs/superpowers/specs/2026-10-19-shell-integration-design.md`

## Global Constraints

- Every sequence is terminated with ST (`ESC \`), never BEL.
- `A` is written at column 1 of the prompt's first non-padding, non-aurora row — exactly one `A` per drawn prompt (a new prompt, or Ctrl+L redraw); never for a repaint, resize, or popup.
- `C` and `D` carry exactly one parameter: `OSC 133 ; C ; cmdline_url=<pct> ST`, `OSC 133 ; D ; <status> ST`. No `aid=`.
- Every 133 parameter after the command letter is `key=value`; `B` carries none.
- Continuation marks use `P;k=s`, never `A;k=s`. No `P` at all when `KONSOLE_VERSION` is set; no `P;k=r` when `TMUX` is set.
- Inside tmux (`TMUX` set): 133 and OSC 7 plain; SetUserVar wrapped as `DCS tmux; <ESC doubled> ST`; OSC 3008 not sent.
- No `redraw=`, no `OSC 9;9`, no OSC 633, no VTE OSC 666, no sudo/ssh helpers.
- OSC 3008: IDs are 128-bit random lowercase hex; every `start=` for the shell carries the full field set; the first 3008 of a session goes out after the first `A`; command `start=` goes after `133;C`; fields over 255 bytes: `cmdline=` truncated on a UTF-8 boundary, `cwd=` omitted; `;` → `\x3b`, `\` → `\x5c`, control characters stripped.
- `shell_integration` (Boolean, default `true` — matching `shell_hyperlinks`; *changed from the spec's number, my call*) gates OSC 133, OSC 7, OSC 3008, automatic SetUserVar. Not OSC 2, OSC 8, `CSI ? 2034`, or the `set-user-var` builtin.
- Nothing is written unless the shell is interactive and stdout is a terminal (the builtin: unless stdout is a terminal).
- core-cpp changes go to core-cpp on branch `feature/semantic-row-marks` (based on `741c43e`, the current pin); endo is built against the local checkout with `-DCPM_core-cpp_SOURCE=$HOME/projects/core-cpp -DUSE_COMPILER_CACHE=OFF` until Task 12 moves the pin. Never work around core-cpp from endo.
- Repository rules from `AGENT.md`: `std::expected`, no C-style loops, `CamelCase` constants without `k` prefix, Doxygen on public API, `clang-format` on changed C++, `endo format` on changed `.endo`, no `NOLINT`, warnings are errors.
- Commits end with `Signed-off-by: Christian Parpart <christian@parpart.family>`.

## Review Focus

1. **Prompt drawn on the terminal's last row** (the inline reservation scrolls the page): the `A` must still land on the first visible prompt line → test in Task 1 (`Screen.rowMarks_reservationScroll`).
2. **Command lines with ESC, BEL, `ESC \`, newline, `;`, `%`, invalid UTF-8**: the sequence must stay one sequence and round-trip through percent-decoding → test in Task 3 (`ShellIntegrationSequences.cmdline_hostile_bytes`).
3. **Resize or full redraw while at the prompt**: row 0 must be re-marked as `P;k=i`, never a second `A` → test in Task 7 (`PromptComponent.marks_full_redraw_no_second_start`).
4. **Piped or scripted endo** (`endo -c`, `endo script.endo`, stdout not a TTY): no escape sequences at all, including from `set-user-var` → tests in Task 6 (`ShellIntegration.silent_when_not_a_terminal`) and Task 10.
5. **cwd containing `;`, `\`, newline, or longer than 255 bytes** in OSC 3008 → test in Task 4 (`ContextSignalling.cwd_escaping_and_limit`).

---

### Task 1: core-cpp — row marks and `deleteLines`

**Files:**
- Modify: `~/projects/core-cpp/src/core/tui/Buffer.hpp`, `Buffer.cpp` (store marks, like `ImageRegion`)
- Modify: `~/projects/core-cpp/src/core/tui/Canvas.hpp`, `Canvas.cpp` (`mark()`)
- Modify: `~/projects/core-cpp/src/core/tui/Screen.cpp` (`flushInline` emission)
- Modify: `~/projects/core-cpp/src/core/tui/TerminalOutput.hpp`, `TerminalOutput.cpp` (`deleteLines`)
- Modify: `~/projects/core-cpp/src/core/tui/MockTerminalOutput.hpp`, `MockTerminalOutput.cpp` (record raw writes with position; record `deleteLines`)
- Test: `~/projects/core-cpp/src/core/tui/Screen_test.cpp`

**Interfaces:**
- Produces:
  - `struct core::tui::RowMark { int row; int column; std::string sequence; };` in `Buffer.hpp`
  - `void Buffer::addMark(int row, int column, std::string sequence);` `std::span<RowMark const> Buffer::marks() const noexcept;` `void Buffer::clearMarks() noexcept;` (cleared with the frame, as images are)
  - `void Canvas::mark(int row, int column, std::string sequence);` — canvas-relative coordinates, translated like `put()`
  - `virtual void TerminalOutput::deleteLines(int n);` — writes `CSI <n> M`
  - `struct MockTerminalOutput::RawWrite { int row; int column; std::string text; };` `std::span<RawWrite const> MockTerminalOutput::rawWrites() const noexcept;` `int MockTerminalOutput::deletedLines() const noexcept;`

**Emission rule (the part the signature does not determine):** in `flushInline`, for each row, marks are sorted by column and split the row into segments `[mark.column, next.column)`. Track the last segment emitted on this row (none at row start). Before writing an updated cell at column `c`, find the segment containing `c`; if it differs from the last emitted one, move the cursor to that mark's column, `writeRaw(mark.sequence)`, move back to `c`, then write the cell. A row with no updated cells emits nothing. A full redraw (`_needsFullRedraw`) updates every cell, so it emits every mark. Cells left of the first mark need no mark. Rows covered by an image emit no marks. The mark is written through `TerminalOutput::writeRaw`, so it costs no cursor columns.

- [ ] **Step 1: Branch core-cpp**

```bash
cd ~/projects/core-cpp && git switch -c feature/semantic-row-marks 741c43e
```

- [ ] **Step 2: Write the failing tests** in `Screen_test.cpp`, using the inline-screen pattern of `Screen.inlineCursor_releaseCursorCycle_noDrift` and a test component that renders `rows` lines of text and calls `canvas.mark(...)` for a configurable list:

```cpp
TEST_CASE("Screen.rowMarks_firstFrameEmitsAtColumn")       // mark (1,0,"<A>") → rawWrites has {row=startRow+1, col=0, "<A>"} before any text of row 1
TEST_CASE("Screen.rowMarks_unchangedRowEmitsNothing")      // second draw() with identical content → no new rawWrites
TEST_CASE("Screen.rowMarks_segmentReestablishedOnRepaint") // marks (0,0,"<P>"),(0,10,"<B>"); change only col 12 → one rawWrite "<B>" at col 10, none "<P>"
TEST_CASE("Screen.rowMarks_leftSegmentRepaint")            // same marks; change only col 3 → one rawWrite "<P>" at col 0
TEST_CASE("Screen.rowMarks_reservationScroll")             // mock cursor on last row (24), content 4 rows → "<A>" on mock row of content row 1 after the scroll
TEST_CASE("TerminalOutput.deleteLines_writesDL")           // MockTerminalOutput: deleteLines(1) → deletedLines()==1
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build --preset clang-debug && ctest --preset clang-debug -R "rowMarks|deleteLines"`
Expected: build fails (`mark`, `rawWrites`, `deleteLines` undeclared).

- [ ] **Step 4: Implement** the interfaces above and the emission rule in `Screen::flushInline`. Fullscreen/fixed viewports ignore marks (YAGNI; say so in the `Canvas::mark` Doxygen).

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset clang-debug && ctest --preset clang-debug`
Expected: all pass.

- [ ] **Step 6: Commit** (core-cpp)

```bash
git add -A src/core/tui && git commit -m "feat(tui): row marks — zero-width sequences anchored to canvas cells, emitted by the inline screen"
```

### Task 2: core-cpp — machine id

**Files:**
- Create: `~/projects/core-cpp/src/core/platform/MachineId.hpp`, `MachineId.cpp`, `MachineId_test.cpp`
- Modify: `~/projects/core-cpp/src/core/platform/CMakeLists.txt` (source + test; not in the Emscripten subset)

**Interfaces:**
- Produces: `[[nodiscard]] std::optional<std::string> core::platform::readMachineId(FileSystem const& fileSystem);` — reads `/etc/machine-id`, trims whitespace, returns it only if it is exactly 32 lowercase-or-uppercase hex digits (returned lowercased); otherwise (missing, `uninitialized`, wrong length) `std::nullopt`.

- [ ] **Step 1: Write the failing tests** with the in-memory file system test double from `core/platform/testing/`:

```cpp
TEST_CASE("MachineId.valid")          // "0123456789ABCDEF0123456789abcdef\n" → "0123456789abcdef0123456789abcdef"
TEST_CASE("MachineId.missing")        // no file → nullopt
TEST_CASE("MachineId.uninitialized")  // "uninitialized\n" → nullopt
TEST_CASE("MachineId.wrongLength")    // 31 hex digits → nullopt
```

- [ ] **Step 2: Run to verify they fail** — `ctest --preset clang-debug -R MachineId` → build error.
- [ ] **Step 3: Implement `readMachineId`.**
- [ ] **Step 4: Run to verify they pass** — `ctest --preset clang-debug -R MachineId` → pass.
- [ ] **Step 5: Commit** — `feat(platform): readMachineId() over an injected FileSystem`

### Task 3: endo — pure shell-integration sequences and the quirk table

**Files:**
- Create: `src/shell/ShellIntegrationSequences.hpp`, `src/shell/ShellIntegrationSequences.cpp`, `src/shell/ShellIntegrationSequences_test.cpp`
- Modify: `src/shell/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace endo::shellintegration {
  enum class PromptZone : std::uint8_t { Start, Prompt, Continuation, RightPrompt, Input };
  struct TerminalQuirks { bool suppressPromptParts = false; bool suppressRightPrompt = false; bool insideTmux = false; };
  [[nodiscard]] TerminalQuirks detectTerminalQuirks(core::Environment const& environment);
  [[nodiscard]] std::string promptZoneSequence(PromptZone zone, TerminalQuirks const& quirks); // "" when suppressed
  [[nodiscard]] std::string percentEncode(std::string_view bytes);
  [[nodiscard]] std::string commandStartSequence(std::string_view commandLine);
  [[nodiscard]] std::string commandFinishedSequence(int status);
  [[nodiscard]] std::string currentDirectorySequence(std::string_view path, std::string_view hostName);
  [[nodiscard]] std::string userVarSequence(std::string_view name, std::string_view value);
  [[nodiscard]] std::string tmuxPassthrough(std::string_view sequence);
  }
  ```
- Values: `Start` → `\033]133;A;click_events=1\033\\`; `Prompt` → `\033]133;P;k=i\033\\`; `Continuation` → `\033]133;P;k=s\033\\`; `RightPrompt` → `\033]133;P;k=r\033\\`; `Input` → `\033]133;B\033\\`. `commandStartSequence("")` → `\033]133;C\033\\`. `percentEncode` keeps `[A-Za-z0-9-._~/]`, encodes every other byte as `%XX` uppercase. `userVarSequence` → `\033]1337;SetUserVar=<name>=<base64(value)>\033\\` using `core::base64::encode`. `tmuxPassthrough(s)` → `\033Ptmux;` + `s` with every `\033` doubled + `\033\\`. `currentDirectorySequence` keeps today's `core::platform::fileUri(normalizePath(path), host)` output.
- The quirk table is a `static constexpr` array of rows `{ std::string_view environmentVariable; bool TerminalQuirks::* flag; }`: `KONSOLE_VERSION` → `suppressPromptParts`; `TMUX` → `suppressRightPrompt` and `insideTmux`. A variable counts when present and non-empty. `Prompt`, `Continuation`, `RightPrompt` return `""` under `suppressPromptParts`; `RightPrompt` also under `suppressRightPrompt`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("ShellIntegrationSequences.zones")                 // each zone's exact bytes, no quirks
TEST_CASE("ShellIntegrationSequences.konsole_suppresses_P")  // KONSOLE_VERSION=1 → Prompt/Continuation/RightPrompt == ""; Start/Input unchanged
TEST_CASE("ShellIntegrationSequences.tmux_suppresses_k_r")   // TMUX=/tmp/x → RightPrompt == "", Continuation unchanged, insideTmux
TEST_CASE("ShellIntegrationSequences.cmdline_hostile_bytes") // "a;b\x1b\\\a\n%é\xff" → "\033]133;C;cmdline_url=a%3Bb%1B%5C%07%0A%25%C3%A9%FF\033\\"; result contains exactly one ESC-\ (the terminator)
TEST_CASE("ShellIntegrationSequences.cmdline_empty")         // "" → "\033]133;C\033\\"
TEST_CASE("ShellIntegrationSequences.finished")              // 130 → "\033]133;D;130\033\\"
TEST_CASE("ShellIntegrationSequences.user_var")              // ("WEZTERM_PROG","vim") → "\033]1337;SetUserVar=WEZTERM_PROG=dmlt\033\\"
TEST_CASE("ShellIntegrationSequences.tmux_passthrough")      // "\033]1337;X\033\\" → "\033Ptmux;\033\033]1337;X\033\033\\\033\\"
```

Environment values come from `core::platform::testing::TestProcessEnvironment`.

- [ ] **Step 2: Run to verify they fail** — `cmake --build --preset clang-debug && ctest --preset clang-debug -R ShellIntegrationSequences` → build error.
- [ ] **Step 3: Implement** the functions above.
- [ ] **Step 4: Run to verify they pass** — same command → pass.
- [ ] **Step 5: Commit** — `feat(shell): shell-integration sequence builders and terminal quirk table`

### Task 4: endo — OSC 3008 context signalling

**Files:**
- Create: `src/shell/ContextSignalling.hpp`, `src/shell/ContextSignalling.cpp`, `src/shell/ContextSignalling_test.cpp`
- Modify: `src/shell/CMakeLists.txt`

**Interfaces:**
- Consumes: `endo::platform::WaitResult` (`src/platform/WaitResult.hpp`).
- Produces:
  ```cpp
  namespace endo::shellintegration {
  struct ContextField { std::string_view key; std::string value; };
  [[nodiscard]] std::string contextStartSequence(std::string_view id, std::span<ContextField const> fields);
  [[nodiscard]] std::string contextEndSequence(std::string_view id, std::span<ContextField const> fields);
  [[nodiscard]] std::string newContextId(std::function<std::uint64_t()> const& random); // two draws → 32 hex chars
  /// Outcome of the command whose status a line reports.
  struct CommandOutcome { int status = 0; std::optional<int> signal; bool stopped = false; };
  [[nodiscard]] std::vector<ContextField> commandEndFields(CommandOutcome const& outcome);
  }
  ```
- Encoding: `OSC 3008 ; start=<id> ; key=value ; … ST`. Values: strip bytes < 0x20 and 0x7F, then `\` → `\x5c`, `;` → `\x3b`. After encoding, a value longer than 255 bytes: key `cmdline` → truncate the *unencoded* value on a UTF-8 boundary so the encoded value is ≤ 255; key `cwd` → field omitted; any other key → omitted.
- `commandEndFields` uses a `static constexpr` table `{ int signal; std::string_view exitValue; }` for `SIGINT`→`interrupt`, `SIGSEGV`/`SIGBUS`/`SIGILL`/`SIGFPE`/`SIGABRT`/`SIGSYS`/`SIGTRAP`→`crash`. Rules: `stopped` → `exit=interrupt`, `status=<n>`, `signal=SIGTSTP`; signal in table → that value; other signal → `failure`; no signal → `success` if status 0 else `failure`. `signal=` uses the symbolic name (`SIGSEGV`), from a name table on POSIX; on Windows no `signal=` field. Always `status=<n>`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("ContextSignalling.start_shell")              // id "ab…", fields type=shell,user=u → "\033]3008;start=ab…;type=shell;user=u\033\\"
TEST_CASE("ContextSignalling.cwd_escaping_and_limit")   // cwd "/a;b\\c\nd" → "cwd=/a\\x3bb\\x5ccd"; cwd of 300 bytes → no "cwd=" in output
TEST_CASE("ContextSignalling.cmdline_truncated_utf8")   // 300 × "é" → encoded cmdline value ≤ 255 bytes and valid UTF-8
TEST_CASE("ContextSignalling.exit_classification")      // {0}→success; {1}→failure; {130,SIGINT}→interrupt,signal=SIGINT; {139,SIGSEGV}→crash; {143,SIGTERM}→failure,signal=SIGTERM; {148,stopped}→interrupt,signal=SIGTSTP; {139,nullopt}→failure (no signal=)
TEST_CASE("ContextSignalling.context_id")               // random returning 1 then 2 → "00000000000000010000000000000002"
```

- [ ] **Step 2: Run to verify they fail** — `ctest --preset clang-debug -R ContextSignalling` → build error.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run to verify they pass.**
- [ ] **Step 5: Commit** — `feat(shell): OSC 3008 context signalling encoder and exit classification`

### Task 5: endo — record the real foreground outcome

**Files:**
- Modify: `src/shell/Shell.hpp` (member + accessor), `src/shell/Shell.cpp` (reset before each `execute` in the interactive loops)
- Modify: `src/shell/builtins/ProcessExecution.cpp:355,393` (`builtinCallProcessShellPiped`), `src/shell/builtins/CommandBuilder.cpp:239,277` (`builtinCmdExecPiped`), `src/shell/builtins/InlineCommands.cpp:2838-2925` (`executeInlineTimeout`), `src/shell/builtins/JobControl.cpp:116` (`builtinFg`)
- Test: `src/shell/Shell_test.cpp`

**Interfaces:**
- Consumes: `CommandOutcome` (Task 4), `WaitResult`.
- Produces: `void Shell::recordForegroundWait(WaitResult const& result) noexcept;` (stores the latest), `void Shell::resetForegroundWait() noexcept;`, `[[nodiscard]] shellintegration::CommandOutcome Shell::lastCommandOutcome(int status) const noexcept;` — returns `{status, signal, stopped}` from the recorded wait **only if** the recorded wait's status (`128 + signal` when signaled/stopped, else `exitCode`) equals `status`; otherwise `{status, nullopt, false}` (the status came from something later, e.g. `segv; true`).

- [ ] **Step 1: Write the failing tests** (POSIX only, `#if !defined(_WIN32)`, in the style of the existing `TestPTY` shell tests):

```cpp
TEST_CASE("shell.outcome.signal_recorded")      // execute("sh -c 'kill -SEGV $$'") → lastCommandOutcome(rc).signal == SIGSEGV
TEST_CASE("shell.outcome.exit_139_is_not_a_crash") // execute("sh -c 'exit 139'") → signal == nullopt
TEST_CASE("shell.outcome.later_status_wins")    // execute("sh -c 'kill -SEGV $$'; true") → rc 0, signal == nullopt
```

- [ ] **Step 2: Run to verify they fail** — `ctest --preset clang-debug -R shell.outcome` → build error.
- [ ] **Step 3: Implement** — call `recordForegroundWait(*waitResult)` at each listed wait site where the foreground process is reaped (keep the existing returns/logging).
- [ ] **Step 4: Run to verify they pass.**
- [ ] **Step 5: Commit** — `feat(shell): carry the foreground process outcome, signal included, to the prompt loop`

### Task 6: endo — `ShellIntegration` emitter, `shell_integration` property, loop wiring

**Files:**
- Create: `src/shell/ShellIntegration.hpp`, `src/shell/ShellIntegration.cpp`, `src/shell/ShellIntegration_test.cpp`
- Modify: `src/shell/Shell.hpp`, `src/shell/Shell.cpp:997-1031` (remove `emitPromptStart/End/CommandStart/CommandFinished/CurrentWorkingDirectory`; keep `emitWindowTitle`), `Shell.cpp:1473-1558` and `1580-1646` (both loops), `src/shell/builtins/Environment.cpp:92`, `src/shell/builtins/Registration.cpp` (property next to `shell_hyperlinks`), `src/shell/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 2–5.
- Produces:
  ```cpp
  class ShellIntegration {
  public:
      struct Identity { std::string user; std::string hostName; std::optional<std::string> machineId; int pid; };
      ShellIntegration(TTY& tty, core::Environment const& environment, Identity identity,
                       std::function<std::uint64_t()> random);
      void setInteractive(bool interactive) noexcept;
      void setEnabled(bool enabled);                 // off: end= open shell context; on: new id at next prompt
      [[nodiscard]] bool enabled() const noexcept;
      [[nodiscard]] shellintegration::TerminalQuirks const& quirks() const noexcept;
      [[nodiscard]] std::string promptZoneSequence(shellintegration::PromptZone zone) const; // "" when off
      void beforePrompt(std::string_view cwd);       // OSC 7
      void afterPromptDrawn(std::string_view cwd);   // OSC 3008 shell start= (full fields); user vars
      void commandStarted(std::string_view commandLine, std::string_view cwd); // 133;C, then 3008 command start=, WEZTERM_PROG
      void commandFinished(shellintegration::CommandOutcome const& outcome);   // 3008 end=, then 133;D
      void workingDirectoryChanged(std::string_view cwd);                       // OSC 7 (cd)
      void sessionEnded(int status);                                            // 3008 end= shell
      void setUserVar(std::string_view name, std::string_view value) const;    // builtin path: TTY check only
  };
  ```
- Behavior: every automatic write requires `interactive && tty.isTerminal() && enabled`. OSC 3008 skipped when `quirks().insideTmux`. SetUserVar wrapped with `tmuxPassthrough` when `insideTmux`. Shell `start=` fields in order: `type=shell`, `user`, `hostname`, `machineid` (if any), `pid`, `comm=endo`, `cwd`. `afterPromptDrawn` sends `WEZTERM_USER`, `WEZTERM_HOST`, `WEZTERM_IN_TMUX` (`1`/`0`) every prompt and `WEZTERM_PROG=""` if it was non-empty. `commandStarted` sends `WEZTERM_PROG=<line>` when changed. `commandFinished` sends `D` with `outcome.status`.
- Shell wiring: `Shell` owns `ShellIntegration _shellIntegration` built in its constructor from `_tty`, `_env`, `core::platform::userName(_env)`, `core::platform::cachedHostName()`, `core::platform::readMachineId(<the shell's FileSystem>)`, the process id, and a `std::random_device`-seeded `std::mt19937_64`. Loop order: `beforePrompt(cwd)` → `updatePromptContext()` → `prompt.display()` → `afterPromptDrawn(cwd)` → read → `emitWindowTitle` → `resetForegroundWait()` → `commandStarted(line, cwd)` → execute → `commandFinished(lastCommandOutcome(_exitCode))`. On loop exit: `sessionEnded(_exitCode)`. The `A`/`B` marks are no longer written here (Task 7).
- Property: `_runtime.registerProperty("shell_integration", CoreVM::LiteralType::Boolean)` get/set → `_shellIntegration.enabled()/setEnabled()`.

- [ ] **Step 1: Write the failing tests** with a `RecordingTTY final : public TTY` in the test file (`isTerminal()` configurable, `writeToStdout` appends to a string) and `TestProcessEnvironment`:

```cpp
TEST_CASE("ShellIntegration.silent_when_not_a_terminal")   // isTerminal=false: every method → nothing written
TEST_CASE("ShellIntegration.silent_when_disabled")         // setEnabled(false) before first prompt: nothing written; setUserVar still writes
TEST_CASE("ShellIntegration.prompt_cycle_order")           // beforePrompt, afterPromptDrawn, commandStarted, commandFinished → OSC 7, 3008 shell start (full fields), SetUserVars, 133;C;cmdline_url, 3008 command start, WEZTERM_PROG, 3008 end exit=…, 133;D — in this order
TEST_CASE("ShellIntegration.reannounce_full_fields")       // second afterPromptDrawn repeats user,hostname,machineid,pid,comm with the same id
TEST_CASE("ShellIntegration.tmux")                         // TMUX set: no "]3008;", SetUserVar wrapped, 133 plain
TEST_CASE("ShellIntegration.toggle_off_ends_context")      // after a prompt, setEnabled(false) → one "end=<id>"; setEnabled(true) + afterPromptDrawn → new id
TEST_CASE("ShellIntegration.zone_sequence_off")            // disabled → promptZoneSequence(Start) == ""
```

- [ ] **Step 2: Run to verify they fail** — `ctest --preset clang-debug -R ShellIntegration\\.` → build error.
- [ ] **Step 3: Implement** the class, replace the old emitters, wire both loops and `cd`, register the property.
- [ ] **Step 4: Run the full suite** — `cmake --build --preset clang-debug && ctest --preset clang-debug` → pass. Also configure and build `clang-debug-agent` once (Shell.cpp changed).
- [ ] **Step 5: Commit** — `feat(shell): ShellIntegration emitter with OSC 3008, user vars, tmux awareness, and the shell_integration setting`

### Task 7: endo — prompt zones as row marks

**Files:**
- Modify: `src/shell/ui/PromptComponent.hpp`, `src/shell/ui/PromptComponent.cpp:309-660` (render), `:983-1027` (reuse the new column helper)
- Modify: `src/shell/ui/Prompt.hpp`, `src/shell/ui/Prompt.cpp:398-417` (`display`), `:324-333` (`ClearScreen`)
- Modify: `src/shell/Shell.cpp` (hand the zone function to `prompt`)
- Test: `src/shell/ui/PromptComponent_test.cpp`

**Interfaces:**
- Consumes: `Canvas::mark` (Task 1), `PromptZone`, `ShellIntegration::promptZoneSequence` (Task 6).
- Produces:
  - `using PromptZoneSequenceFn = std::function<std::string(shellintegration::PromptZone)>;`
  - `void PromptComponent::setZoneSequences(PromptZoneSequenceFn fn);` and `void Prompt::setZoneSequences(PromptZoneSequenceFn fn);` (forwards)
  - `void PromptComponent::beginNewPrompt() noexcept;` — the next render marks the first row `Start` instead of `Prompt`; cleared by that render.
  - `[[nodiscard]] int PromptComponent::inputTextStartColumn() const noexcept;` — `HorizontalMargin + leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr)` (the value `handleMouseEvent` computes today; make it use this).
- Marks, per render (skipping a mark whose sequence is `""`): first prompt row `r0 = topPadding() + auroraFadeHeight()` → `(r0, 0, Start or Prompt)`; every later row from `r0 + 1` to the last input row → `(row, 0, Continuation)`; every input row → `(row, inputTextStartColumn(), Input)`; the right prompt, when drawn, → `(infoLineRow, rightCol, RightPrompt)` with `rightCol` as computed at `PromptComponent.cpp:627`. Padding and aurora rows get none.
- `Prompt::display()` calls `beginNewPrompt()` before `_screen->draw()`; the `ClearScreen` action calls it before setting `needsRedraw`.

- [ ] **Step 1: Write the failing tests** — render the component into a `core::tui::Buffer` through a `Canvas`, with a zone function returning `"<A>"`, `"<P>"`, `"<S>"`, `"<R>"`, `"<B>"`, and assert `buffer.marks()`:

```cpp
TEST_CASE("PromptComponent.marks_two_line_spacing_1")      // rows: 0 none; 1 <A>@0; 2 <S>@0 and <B>@inputTextStartColumn(); bottom padding none
TEST_CASE("PromptComponent.marks_single_line_spacing_0")   // row 0 <A>@0 and <B>@col
TEST_CASE("PromptComponent.marks_boxed_with_aurora")       // spacing 1, aurora on (cellPixelHeight>0, auroraBackground set): <A> on row 2, <S> on rows 3..5+, <B> on input rows
TEST_CASE("PromptComponent.marks_multiline_input")         // 3 input lines → <S>@0 and <B>@col on each input row
TEST_CASE("PromptComponent.marks_right_prompt")            // rightPromptModules set → <R> at the right column of the info row
TEST_CASE("PromptComponent.marks_full_redraw_no_second_start") // beginNewPrompt(); render; render again → second render has <P> on r0, no <A>
TEST_CASE("PromptComponent.marks_suppressed_zone")         // zone fn returns "" for Continuation → no mark on those rows
```

- [ ] **Step 2: Run to verify they fail** — `ctest --preset clang-debug -R PromptComponent.marks` → build error.
- [ ] **Step 3: Implement**, and pass `[this](auto zone) { return _shellIntegration.promptZoneSequence(zone); }` from `Shell` to `prompt.setZoneSequences` in the constructor.
- [ ] **Step 4: Run the full suite** — pass.
- [ ] **Step 5: Manual check in Contour** — build endo, run it in Contour with the default TwoLine prompt and `shell_prompt_spacing <- 1`; run `ls` three times; jump to previous prompt (Contour's ScrollMarkUp binding) lands on the info line, not the blank line above it.
- [ ] **Step 6: Commit** — `fix(shell): prompt-start mark on the first visible prompt line; continuation, right-prompt and input zones as row marks`

### Task 8: endo — transient prompt keeps the marked line

**Files:**
- Modify: `src/shell/ui/Prompt.hpp`, `src/shell/ui/Prompt.cpp:629-695` (`emitTransientPrompt`)
- Test: `src/shell/ui/PromptComponent_test.cpp` (or a new `src/shell/ui/Prompt_test.cpp` added to `src/shell/CMakeLists.txt`)

**Interfaces:**
- Consumes: `TerminalOutput::deleteLines`, `MockTerminalOutput::deletedLines/rawWrites` (Task 1).
- Produces: a free function in `Prompt.cpp`'s header-visible detail namespace:
  `struct TransientGeometry { int topPad; int aurora; int chrome; int inputLines; int cursorLine; int botPad; };`
  `void collapseForTransient(core::tui::TerminalOutput& out, TransientGeometry const& g);` — moves to the top of the prompt region, clears the top padding rows, and when `g.aurora > 0` deletes the aurora row with `out.deleteLines(g.aurora)` so the `A`-marked line moves up into row `topPad`; leaves the cursor at column 1 of row `topPad`. `emitTransientPrompt` calls it, then writes the transient content and clears/positions the remaining rows with the row count reduced by `g.aurora`.

- [ ] **Step 1: Write the failing tests** with `MockTerminalOutput`:

```cpp
TEST_CASE("Prompt.transient_collapse_deletes_aurora_row")  // aurora=1 → deletedLines()==1; cursorRow()==start+topPad, cursorCol()==0
TEST_CASE("Prompt.transient_collapse_without_aurora")      // aurora=0 → deletedLines()==0
```

- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement** `collapseForTransient` and use it.
- [ ] **Step 4: Run the full suite** — pass; manually confirm in Contour with `shell_prompt_transient` on and the aurora on: spacing unchanged, jump-to-prompt lands on the transient line.
- [ ] **Step 5: Commit** — `fix(shell): transient prompt collapses around the marked line`

### Task 9: endo — click to move with mouse tracking off

**Files:**
- Modify (only if a test fails): `src/shell/ui/PromptComponent.cpp:983-1027`
- Test: `src/shell/ui/PromptComponent_test.cpp`

**Interfaces:**
- Consumes: `PromptComponent::processInput`, `inputTextStartColumn()` (Task 7). `click_events=1` itself is already in the `Start` sequence (Task 3).

- [ ] **Step 1: Write the tests**

```cpp
TEST_CASE("PromptComponent.click_press_without_release_moves_cursor") // type "hello world"; Press at (row of input, col inputTextStartColumn()+3 → 1-based) with no Release; type 'X' → text "helXlo world", no selection
TEST_CASE("PromptComponent.click_on_chrome_snaps_to_input")          // Press on the info row → cursor on input line 0 (existing behavior kept)
```

- [ ] **Step 2: Run them** — `ctest --preset clang-debug -R PromptComponent.click`. If both pass, skip to Step 4.
- [ ] **Step 3: Fix** — a lone `Press` must leave no pending drag/selection state in `InputField::handleMouse` usage; if the fix belongs in core-cpp's `InputField`, make it there (Task 1 branch) with its own test.
- [ ] **Step 4: Commit** — `test(shell): click-to-move works from a lone press (kitty, Ghostty click_events)`

### Task 10: endo — `set-user-var` builtin

**Files:**
- Modify: `src/shell/builtins/InlineCommandDescriptors.cpp` (descriptor row + `SetUserVarOptions`), `src/shell/builtins/InlineCommands.cpp` (`Shell::executeInlineSetUserVar`), `src/shell/Shell.hpp`
- Modify: `src/endo-language/builtins/BuiltinSignatures.hpp` (`registerInlineBuiltins()` if inline builtins are listed there)
- Test: `tests/builtins/set-user-var-*.endo`

**Interfaces:**
- Consumes: `ShellIntegration::setUserVar` (Task 6).
- Produces: inline builtin `set-user-var NAME VALUE` — `.usageLine = "set-user-var NAME VALUE"`, `.briefDescription = "Publish a user variable to the terminal (OSC 1337 SetUserVar)."`, no options besides help. Exit 0 on success and when stdout is not a terminal (silent no-op); exit 2 with a usage message for a wrong argument count or an empty name or a name containing `=`.

- [ ] **Step 1: Write the failing endo-tests** (`# mode: shell`):

```endo
# tests/builtins/set-user-var-not-a-tty.endo
# description: set-user-var is a silent no-op when stdout is not a terminal
# expect: done
# expect-exit: 0
set-user-var FOO bar
println "done"
```

```endo
# tests/builtins/set-user-var-usage.endo
# description: set-user-var rejects a missing value
# expect-exit: 2
set-user-var FOO
```

- [ ] **Step 2: Run to verify they fail** — `ctest --preset clang-debug -R set-user-var` → fail (unknown command).
- [ ] **Step 3: Implement** the descriptor row and handler; run `endo format` on the new `.endo` files.
- [ ] **Step 4: Run to verify they pass.**
- [ ] **Step 5: Commit** — `feat(shell): set-user-var builtin`

### Task 11: endo — documentation and roadmap

**Files:**
- Create: `docs/shell/shell-integration.md`
- Modify: `mkdocs.yml` (nav entry after "Configuration"), `docs/shell/configuration.md` (`shell_integration` section beside "Prompt Spacing"), `docs/shell/builtins.md` (`set-user-var`), `docs/language/standard-library.md` (if inline builtins are listed there), `ROADMAP.md`, `src/shell/ui/PromptComponent.hpp:241` (stale "0 on first display" comment)

- [ ] **Step 1: Write `docs/shell/shell-integration.md`** covering, factually and without claims about untested terminals: every sequence endo sends and when (table from the spec's "Resulting shapes" plus OSC 7, OSC 3008, SetUserVar); `shell_integration`; how the spacer rows relate to command blocks; the quirk table (Konsole, tmux) and `allow-passthrough on` for user vars in tmux; the known limitation that continuation flags outlive shrinking rows in Contour and tmux; what Contour does with each.
- [ ] **Step 2: Build the docs** — `mkdocs build --strict` → no warnings (skip if mkdocs is not installed and say so in the summary).
- [ ] **Step 3: Commit** — `docs(shell): shell integration page, shell_integration setting, set-user-var`

### Task 12: Move the core-cpp pin

**Files:**
- Modify: `cmake/EndoThirdParties.cmake:357-363`

- [ ] **Step 1: Ask the user before pushing** the core-cpp branch `feature/semantic-row-marks` to `origin` and opening its PR (outward-facing).
- [ ] **Step 2: Push and open the core-cpp PR** — `cd ~/projects/core-cpp && git push -u origin feature/semantic-row-marks && gh pr create --fill`.
- [ ] **Step 3: Set `GIT_TAG`** to the pushed commit's full hash and update the comment to `# feature/semantic-row-marks (row marks, deleteLines, readMachineId) on top of feature/clipboard-writer; moves to the release tag once core-cpp publishes it.`
- [ ] **Step 4: Fresh configure without the local override** — `cmake --preset clang-debug --fresh && cmake --build --preset clang-debug && ctest --preset clang-debug` → pass. Repeat build for `clang-debug-agent`.
- [ ] **Step 5: Commit** — `build: pin core-cpp with row marks`
