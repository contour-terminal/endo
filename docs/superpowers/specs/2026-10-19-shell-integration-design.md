# Shell Integration: Contour Parity — Design

Date: 2026-10-19
Status: awaiting the user's approval

## Intent

endo's terminal shell integration has fallen behind what terminals understand. The reference
terminal is [Contour](https://github.com/contour-terminal/contour) (`~/projects/contour`), and the
wider rule (decided by the user) is: **a shell-integration feature that is widely understood by
other terminals is implemented in endo; where Contour lacks it, a Contour ticket is filed.** The
survey behind this covered the trees in `~/usr/src/terminals/` (kitty, Ghostty, WezTerm, iTerm2,
Windows Terminal, Konsole, VTE, foot, Rio, Warp, mintty, tmux, mlterm, xterm.js). endo
should send everything Contour can make use of, sent correctly, so that Contour's command blocks,
prompt navigation, sticky header, finish notifications, "copy last command output", context
breadcrumb and crash/kill details all work with endo as they do with Contour's own bash/zsh/fish
integrations.

It also fixes one reported bug: with the user's setting (multi-line prompt, `shell_prompt_spacing`
of 1), the terminal's prompt-start mark sits **one line too early** — on the blank spacer row
above the prompt instead of on the prompt's first visible line.

## Goals

1. **Prompt start lands on the first visible prompt line**, for every layout (SingleLine,
   TwoLine, Powerline, Boxed), with `shell_prompt_spacing` 0 or 1, and with or without the aurora
   sixel row.
2. **Send the command line with command start**: `OSC 133 ; C ; cmdline_url=<percent-encoded> ST`.
3. **OSC 3008 hierarchical context signalling** (UAPI.15): a `shell` context for the session and
   a `command` context per executed command, including exit classification and signal.
4. **Continuation and right-prompt marks**: `OSC 133 ; P ; k=s ST` at the start of every prompt
   line after the first, and `OSC 133 ; P ; k=r ST` where the right prompt begins.
6. **Click to move the cursor**: `click_events=1` on `A`, and endo handles the reported click.
7. **User variables**: `OSC 1337 ; SetUserVar=<name>=<base64> ST` for a fixed set, plus a builtin.
8. **tmux awareness**: SetUserVar is wrapped for tmux passthrough.
9. **A setting to turn shell integration off.** Today it is hard-wired on.
10. **A documentation page** describing what endo sends, when, and what a terminal does with it.

## Non-goals

- **VS Code's OSC 633** (dropped by the user).
- Features only one terminal understands: kitty's `special_key=1`, Ghostty's `redraw=last`,
  iTerm2's `1337;CurrentDir/RemoteHost/ShellIntegrationVersion/SetMark` and 133 `E`–`H`, OSC 7's
  `?machineID=` (iTerm2), Windows Terminal's `9001;CmdNotFound` and `633;Completions`, mintty's
  `CSI ? 7711`, Warp's DCS/OSC 9278 hook protocol.
- **`OSC 9;9`** (ConEmu cwd): kitty and Contour turn it into a notification and WezTerm rejects it;
  every terminal that reads it also reads OSC 7.
- **`redraw=0`**: it tells the terminal the shell does *not* redraw on resize; endo does.
- `133;N` and `133;L`: equivalent to what endo already does (`A`; endo's own partial-line
  indicator does the fresh-line).
- The removed `CSI > M` line marks.
- **VTE's lifecycle signals** (`OSC 666` precmd/preexec/postexec, legacy `OSC 777` precmd/preexec)
  — dropped by the user.
- **`sudo`/`ssh` terminfo helpers** in the style of kitty's ssh kitten and Ghostty's `+ssh` and
  `sudo --preserve-env=TERMINFO` wrapper — dropped by the user.
- Shipping or changing Contour itself (see "Contour follow-ups").
- Changing how the prompt looks or how spacing is drawn. Only where the marks go changes.

## Anti-goals (would count as failure even if it "works")

- The prompt moving, flickering, or gaining/losing a blank line as a side effect of the fix.
- Marks that create empty or duplicate command blocks in Contour (e.g. a second `A` for one
  prompt, or a block per keystroke redraw).
- Working around a missing core-cpp seam inside endo. Per `AGENT.md`, a missing seam in
  `core::tui` is fixed in core-cpp, released, and picked up by moving the pin.
- Escape injection: a command line or path containing ESC, BEL, `;` or ST must never terminate or
  corrupt a sequence.

## Background: what exists today

All emitters are in `src/shell/Shell.cpp:997-1051`; each writes raw via `_tty.writeToStdout`,
terminated with ST, gated on `_interactive && _tty.isTerminal()`.

| Sequence | Emitter | Payload |
|---|---|---|
| `OSC 133;A` | `emitPromptStart` | none |
| `OSC 133;B` | `emitPromptEnd` | none |
| `OSC 133;C` | `emitCommandStart` | none |
| `OSC 133;D;<exit>` | `emitCommandFinished` | exit code |
| `OSC 7;file://host/path` | `emitCurrentWorkingDirectory` (also on `cd`, `builtins/Environment.cpp:92`) | cwd |
| `OSC 2;<title>` | `emitWindowTitle` | cwd at prompt, command line on execute (C0/C1 stripped) |

Order in the interactive loop (POSIX `Shell.cpp:1473-1558`, Windows `1580-1646` mirrors it):
OSC 7 → **133;A** → OSC 2 (cwd) → `prompt.display()` → **133;B** → `prompt.read()` → OSC 2
(command) → **133;C** → execute → **133;D**.

### Root cause of the off-by-one

The prompt is one inline `core::tui::Screen` (`src/shell/ui/Prompt.cpp:100-111`). Its rows, top
to bottom (`PromptComponent.cpp:441-451, 893-955`): `topPadding` (= `promptSpacing`) → aurora row
(0/1) → chrome (TwoLine/Powerline 1, Boxed 3, SingleLine 0) → input lines → `bottomPadding`
(= `promptSpacing`).

`133;A` is written before `prompt.display()`, while the cursor sits at column 1 of the row the
top padding row is about to occupy. Contour stamps the mark on the logical line holding the
cursor when `A` arrives (`Screen::setMark` → `markLogicalLineAtCursor`, `contour
src/vtbackend/screen/Screen.cpp:6891`) and performs no fresh-line. So the mark lands on the blank row.
With spacing 0 and no aurora, it happens to be correct.

Side notes found while surveying:
- `PromptComponent.hpp:241` documents the top padding as "0 on first display", but `topPadding()`
  always returns `promptSpacing` (`PromptComponent.cpp:948`). Fix the comment.
- On Submit without transient mode, the cursor moves past the input lines **and** the bottom
  padding row before CR LF (`Prompt.cpp:246-270`); output starts below the bottom blank row.

### What Contour reads (reference)

From `contour docs/vt-extensions/osc-133-shell-integration.md`, `osc-3008-context.md`, and
`Screen::processShellIntegration` (`src/vtbackend/screen/Screen.cpp:7039-7175`):

- `A`: marks the logical line at the cursor, closes a still-open `C` as ended, begins a command
  block. `k=c|s|r` turns it into `P`. No fresh-line. **Resize at the prompt:** the "live prompt"
  is the rows from the line the last `A` marked down to the page bottom; Contour leaves those rows
  as the shell drew them (cut/padded, not re-wrapped) because the shell redraws. `redraw=0` tells
  Contour the shell does *not* redraw, so it re-wraps instead.
- `B`: records the logical line and column where input begins.
- `P;k=r`: records where a right prompt begins — used **only** to exclude it when Contour reads the
  command line off the screen. `k=c|s|i` on `P` change nothing.
- `C`: `cmdline_url=` (percent-encoded; value runs to end of sequence) or `cmdline=`; without
  either, Contour reads the command off the screen between `B` and the line end.
- `D;<status>`: first parameter only.
- `aid=`, `cl=`, `click_events=` are ignored.
- OSC 3008: once any OSC 133 has been seen, 3008 contributes **metadata only** (cwd, identity,
  `exit=crash|interrupt`, `signal=`), never marks. Contour trusts a `cwd=` for spawning/opening
  only with a matching `machineid=` (32 hex digits of machine-id(5)) or, failing that, `hostname=`.
  Gated by Contour's `osc_context.enabled`.

## Design

### 1. Prompt-start placement

**Requirement:** the logical line that receives `OSC 133;A` is the prompt's first non-padding,
non-aurora row:

```
row 0              top padding (blank)        ← previous block's tail, unmarked
row topPad         aurora row (if enabled)    ← unmarked
row topPad+aurora  first chrome row, or the first input row for SingleLine  ← 133;A here
...                input rows                 ← 133;B at the input cursor after the indicator
last               bottom padding (blank)     ← part of the prompt; output starts below it
```

Decisions (agreed in conversation, `My call` marks where the user only approved a guess):

- The top spacer row belongs to the tail of the previous command's block (it lies between that
  command's `D` and the new `A`). *Approved guess.*
- The bottom spacer row stays part of the prompt; output starts after it, as today. *Approved
  guess.*
- `133;B` keeps its current meaning: cursor at the start of editable input on the first input row.

**Mechanism — left to the builder, with constraints:**
- The `A` must be written with the cursor on the target row, column 1, **before** that row's
  content would make Contour's reading of the line ambiguous — i.e. it may be emitted as part of
  the first draw (when the inline Screen has reserved room and positioned the cursor) rather than
  before it.
- Exactly one `A` per drawn prompt (see Clear screen below). Redraws while editing, completion popups, resizes, and the
  growth of the inline region must not re-send it.
- Pinning an unreleased core-cpp commit is acceptable for this work (decided by the user); the
  core-cpp change is part of this plan, as its own PR, and endo's pin moves to that commit.
- If `core::tui::Screen` gives no way to emit a raw sequence at a known row of the inline region
  (or to learn the region's start after the first flush), add that seam in core-cpp and move the
  pin. Do not hand-compute row offsets around `Screen::flushInline`'s LF/CUU reservation from endo.
- Reservation scrolling: when the prompt is drawn near the bottom of the page, `flushInline`
  emits LFs that scroll the page. The mark must still end up on the right line after that scroll
  (Contour's mark follows the logical line, so emitting it after reservation is sufficient).

**Transient prompt** (`Prompt::emitTransientPrompt`, `Prompt.cpp:629-695`): after Submit, the
region collapses to the transient line written at row `topPad` (skipping the aurora row). The
`A` mark must end up on the transient line's logical line. With the aurora row enabled the full
prompt's first visible line is `topPad+1` while the transient line is `topPad` — the builder must
make these agree (e.g. by clearing in a way that keeps the marked line as the transient line), and
must not achieve it by sending a second `A`.

**Abandoned prompt** (`Action::NewPrompt`, `Prompt.cpp:272-294`, e.g. Ctrl+C at the prompt): a new
prompt gets its own `A` on its own first visible line. Whether the abandoned prompt leaves an
empty block in Contour is acceptable — that matches bash/zsh behavior — but there must not be two
`A`s for the new prompt.

**Clear screen** (`Action::ClearScreen`, Ctrl+L, `Prompt.cpp:324`): the screen is cleared and the
same prompt is redrawn at the top, so the marked line is gone. *My call:* re-send `A` on the redrawn
prompt's first visible line, as bash and zsh effectively do (their `PS1` carries the mark). The
"exactly one `A`" rule therefore means one `A` per *drawn* prompt, with ClearScreen counted as a
new draw; it still forbids an `A` per keystroke redraw, resize or popup.

**Agent mode** (`ENDO_ENABLE_AGENT`): the agent input reuses `promptSpacing`
(`Shell.cpp:3196`). Out of scope unless it emits marks; if it does, it follows the same rule.

### 2. Command line on `133;C`

`OSC 133 ; C ; cmdline_url=<percent-encoded command line> ST`

- Percent-encode every byte outside unreserved `[A-Za-z0-9-._~]` plus `/`, so ESC, BEL, `;`, `%`,
  newlines (multi-line commands) and non-ASCII are all safe. Contour decodes as kitty does.
- Empty input (bare Enter) sends `C` without the parameter.
- The command line is exactly what the user submitted (`lineBuffer`), not an expansion.

### 3. Continuation lines, right prompt, nested-shell id

**Cross-terminal constraints found in the survey** (these bind every sequence endo sends):
- Ghostty, WezTerm, and iTerm2 (behind a preference) perform a **fresh-line on `A`**: if the cursor
  is not at column 1 they emit CR LF first. `A` must always be sent at column 1.
- WezTerm rejects a whole 133 sequence if any part after the first is not `key=value`
  (`wezterm-escape-parser/src/osc.rs:749`), and `B`/`I` must carry **no** parameters there.
- Windows Terminal treats every `A` — including `A;k=s` — as a new prompt start and ignores `P`;
  WezTerm ignores `k=` on `A`. So continuation marks use the **`P;k=s`** form, never `A;k=s`. Cost:
  kitty only reads `k=s` on `A`, so kitty does not see continuation marks.
- **Konsole** treats `A`, `N` and `P` alike as a prompt start (`Vt102Emulation.cpp:1195`), so any
  `P` adds a prompt mark there. *My call:* endo sends no `P` at all when `KONSOLE_VERSION` is set.
  Terminal quirks like this live in one data table keyed by environment detection, not in
  conditions scattered across emitters.
- **kitty** reads everything after `D;` as the status and only takes a command line that is the
  first parameter after `C;` (`kitty/screen.c:3640-3644`). So `C` and `D` carry exactly one
  parameter each: `C;cmdline_url=…` and `D;<status>`.

**Continuation marks (`P;k=s`):** sent at column 1 of every prompt row after the row that carried
`A`: TwoLine/Powerline's input row, Boxed's rows after its top border, and each additional row of
a multi-line command. Understood by kitty (A-form only), iTerm2, tmux, Rio, Ghostty, WezTerm;
Contour accepts and ignores it. On input rows, a `B` follows the row's continuation prefix, as
Ghostty's and iTerm2's scripts do (Ghostty needs it for click-to-move).

**Redraws** (*my call*): Ghostty and WezTerm tag each *cell* with the zone in force when it is
written, not each line. endo repaints rows after `B` (clock or git modules, editing), so a repaint
must first re-establish that row's zone: `P;k=s` at column 1 before a prompt row, `B` at the input
start before input cells. The first row's `A` is never re-sent for a repaint (only for a new prompt,
see §1). In line-flag terminals (Contour, kitty, tmux, iTerm2) re-sending `P;k=s` or `B` on the same
line at the same column is idempotent. This is what the core-cpp seam must support: per-row zone
callbacks that the inline Screen invokes whenever it writes into a row, not a one-shot raw write.

**Rows that stop being prompt rows** (input shrinking, transient collapse) keep their line flags in
Contour and tmux even after they are cleared. Accepted as a known limitation and documented: Contour
ignores `k=s`, and in tmux a `k=s` flag on an output line does not make it a prompt-navigation
target.

**Right prompt (`P;k=r`):** kept (decided by the user: it may matter as endo's prompt evolves).
Sent at the column where the right-aligned section (`rightPromptModules`, `PromptConfig.hpp:112`)
begins, on the row it is drawn on, only when one is drawn — today that is the chrome row carrying
`A` (`PromptComponent.cpp:478, 617-641`). Understood by Contour, Warp, iTerm2, Ghostty, WezTerm.
Like the other zone marks it is re-established when that part of the row is repainted, so the seam
needs row *and column* positions. *My call:* not sent under tmux, which treats any `P` other than
`k=s` as a prompt start and moves its prompt column (`tmux/input.c:3330-3338`) — another quirk-table
row.

**Nested-shell id (`aid=`):** *my call: not sent.* Only iTerm2 acts on it, and it acts on the `D`
form, which kitty misreads (above). Putting it only on `A` would give iTerm2 nothing.

**Resulting shapes:**
```
OSC 133 ; A ; click_events=1 ST
OSC 133 ; P ; k=s ST
OSC 133 ; P ; k=r ST
OSC 133 ; B ST
OSC 133 ; C ; cmdline_url=<percent-encoded> ST
OSC 133 ; D ; <status> ST
```

### 3a. Click to move the cursor

`click_events=1` on `A` asks the terminal to report a left click inside the live prompt as an SGR
mouse press, `CSI < 0 ; x ; y M` with absolute 1-based coordinates, even when mouse reporting is
off (kitty `mouse.c:818`; Ghostty `Surface.zig:4274`, behind its `cursor-click-to-move` option).
Click-to-cursor already exists: `PromptComponent::handleMouseEvent` (`PromptComponent.cpp:983-1027`),
with `core::tui::Screen::dispatchMouseEvent` translating absolute rows through
`_inlineContentStartRow`. Today it only fires where endo has mouse tracking on — core-cpp raises
any-motion tracking only when the terminal supports DEC mode 2029 (`core-cpp
src/core/tui/posix/Terminal.cpp:103`), i.e. Contour. `click_events=1` makes kitty and Ghostty
deliver the click with tracking off, so the new work is: send the parameter, make sure endo's input
decoder accepts an SGR press while tracking is off, and act on the press alone (kitty sends no
release, `mouse.c:824`). *My call:* keep the existing behavior, including snapping a click on chrome
or padding to the nearest input line. Contour accepts and ignores `click_events=1`; the user
does not want the feature in Contour, so no ticket.

*My call:* only `click_events=1` (absolute), not `=2` (relative) and not `cl=` (the terminal sends
arrow keys instead; only Ghostty implements it, and synthesised arrows are less exact than a
position).

### 3b. User variables

`OSC 1337 ; SetUserVar=<name>=<base64(value)> ST`, understood by kitty (watchers), WezTerm
(`user-var-changed`, `pane:get_user_vars()`), iTerm2 (badges, titles). Contour lacks it — ticket
contour-terminal/contour#2161.

*My call on the set*, following WezTerm's script since WezTerm configs consume these names:
- `WEZTERM_PROG`: the command line while a command runs, empty at the prompt.
- `WEZTERM_USER`, `WEZTERM_HOST`: at every prompt, as WezTerm's script does, so an `ssh` session
  or nested shell that overwrote them is corrected.
- `WEZTERM_IN_TMUX`: `1` when `$TMUX` is set.

Plus a builtin for scripts (name is the builder's, e.g. `set-user-var <name> <value>`), declared
through the builtin descriptor tables. `WEZTERM_PROG` is sent only when it changes. *My call:* the
builtin works whenever stdout is a terminal, including in non-interactive scripts and with
`shell_integration <- 0` — it is an explicit request, not automatic integration.

### 3c. tmux

tmux interprets OSC 133 (including `P;k=s`, `cmdline_url=`, `D` status) and OSC 7 itself
(`tmux/input.c:3318`, `2738`), never re-emitting 133 outward. SetUserVar is wrapped as `DCS tmux; <sequence with every ESC doubled> ST` when
`$TMUX` is set. (Requires `allow-passthrough on` in tmux; off by default. Document this.) OSC 133 and
OSC 7 are always sent plain. *My call:* OSC 3008 is **not** sent inside tmux: tmux swallows OSC 133,
so the outer terminal would see 3008 alone and Contour would derive marks from it at tmux's
composited cursor, interleaved across panes and unpaired for hidden panes. Which sequences get wrapped is a property in a data table, not a
per-emitter condition.

### 4. OSC 3008 context signalling

Format per UAPI.15: `OSC 3008 ; start=<id> [; field=value]... ST` and `OSC 3008 ; end=<id>
[; field=value]... ST`. In values, `;` is written `\x3b` and `\` is written `\x5c`; control
characters are stripped.

| When | Sequence |
|---|---|
| Interactive session starts (TTY) | `start=<shellId>;type=shell;user=;hostname=;machineid=;pid=;comm=endo;cwd=` |
| Each prompt (re-announce, updates cwd), sent **after** that prompt's `133;A` | `start=<shellId>` with the **full** field set again, `cwd=` updated |
| Command execution begins (alongside `133;C`) | `start=<cmdId>;type=command;cmdline=<line>;cwd=<cwd>` |
| Command finished (alongside `133;D`) | `end=<cmdId>;exit=<success\|failure\|crash\|interrupt>;status=<n>[;signal=<SIGNAME>]` |
| Shell exits | `end=<shellId>;exit=success;status=<n>` |

- IDs: 128-bit random, lowercase hex (UAPI.15 requires unguessable IDs).
- Exit classification uses the **real process outcome**, not a guess from the status (decided by
  the user: a status of 139 from `exit 139` must not read as a crash). Today the foreground wait
  sites learn the signal (`waitResult->signaled/signal` in `builtins/ProcessExecution.cpp:355`,
  `CommandBuilder.cpp:239/277`, `InlineCommands.cpp:2838ff`, and `fg` in `JobControl.cpp`) but only
  log it, and `execute()` returns a bare `int`. This work carries a structured outcome (status plus
  optional terminating/stopping signal) from every wait site up through `execute()` — a separate
  phase of the plan. Classification: no signal and status 0 → `success`; non-zero → `failure`;
  SIGINT → `interrupt`; SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT/SIGSYS/SIGTRAP → `crash`; other
  signals → `failure` with `signal=`. This mapping lives in a data table.
- `machineid=` from `/etc/machine-id` when present (32 hex), omitted otherwise; `hostname=` from
  `core::platform::cachedHostName()`. The machine-id read goes through an injected platform
  abstraction (core-cpp's `core::platform` if it fits, else a core-cpp addition) — no direct file
  access from `Shell.cpp`.
- Builtins and pipelines: one `command` context per submitted line, matching `133;C/D` (not one
  per pipeline stage).
- A repeated `start=` resets every omitted field to its default (UAPI.15; Contour
  `TerminalContext.cpp:51-78`), so each re-announce carries `type`, `user`, `hostname`,
  `machineid`, `pid`, `comm` and `cwd`. Losing `machineid`/`hostname` would make Contour stop
  trusting `cwd=`.
- The session-start `start=` is also sent after the first `133;A`, so Contour has seen OSC 133
  before any 3008 and never synthesises a duplicate block from it.
- Ordering: the command `start=` is sent after `133;C`, as a child of the shell context — Contour
  links the context to the block only then (`CommandBlockStore::commandContextStarted`).
- Length: Contour drops any 3008 field over 255 decoded bytes. *My call:* truncate `cmdline=` to
  255 bytes on a UTF-8 boundary (the full line already travels in `cmdline_url`); omit a `cwd=`
  that is longer rather than truncate it into a wrong path.
- Pipelines and lists (`a | b`, `a; b`, `a && b`): *my call:* the line's outcome is the status
  `133;D` already reports, and the signal is the one belonging to the process that produced that
  status.
- Ctrl+Z (foreground job stopped): *my call:* end the context with `exit=interrupt`, the status
  `133;D` reports, and `signal=SIGTSTP`. UAPI.15 has no "stopped", and the prompt that follows
  starts a new cycle anyway.
- Nested shells: endo started from another shell announces its own new `shell` context; it does
  not try to `end` or reuse a parent's.

### 5. Turning it off

A new property, `shell_integration`, registered beside the other `shell_*` properties
(`Registration.cpp`) and documented in `docs/shell/configuration.md`. *My call on name and
shape:* a Boolean (default `true`), matching `shell_hyperlinks`. Off suppresses OSC 133, OSC 7, OSC 3008, and SetUserVar. OSC 2 (title), OSC 8 hyperlinks, and the Contour
semantic-block extension (`CSI ? 2034`) keep their existing controls.

Toggling at runtime (*my call*): switching off sends `end=<shellId>` (if a context is open) and then
nothing; switching on starts a new shell context with a new id at the next prompt.

Since `init.endo` runs before the first prompt, setting it there takes effect before the first
`A`. The session-start `3008 start=` must therefore be deferred until the first prompt so the
setting is honored.

### 6. Structure

- Gating (`_interactive && _tty.isTerminal() && enabled`) in one place, not repeated per emitter.
- The emitters write through the injected `TTY`, as today, so tests can capture them.
- Follow data-driven design where several sequences share a shape (e.g. the exit-classification
  table, the 3008 field list), per `AGENT.md`.

### 7. Documentation

New page `docs/shell/shell-integration.md` (linked from the shell docs index): each sequence endo
sends, when, its parameters, the `shell_integration` setting, how the spacer rows relate to
blocks, and what Contour features it powers. Note other terminals read the OSC 133 subset; do not
claim support for terminals that were not tested. Also update `ROADMAP.md`.

## Testing

- **Placement**: a test that captures the bytes written during prompt display for each layout ×
  spacing (0/1) × aurora (on/off), and asserts the row of the `A` relative to the first visible
  prompt row. Whether this is a Catch2 test over `Prompt` with a recording terminal or an endo-test
  `shell` mode test is the builder's call; endo-test cannot currently assert escape output, so
  Catch2 is likely.
- Transient mode: after Submit, `A`'s line is the transient line (with and without aurora).
- Exactly one `A` per prompt across edits, resize, completion popup, and NewPrompt.
- `cmdline_url` encoding: ESC, BEL, `;`, `%`, newline, UTF-8.
- `P;k=s` on every prompt row after the first, for each layout and for multi-line input growing
  and shrinking; re-established before every repaint of a row; no `P` at all under Konsole.
- `P;k=r` at the right prompt's column only when one is drawn; not under tmux.
- `C` and `D` carry exactly one parameter (kitty).
- Click to move: an SGR press with mouse tracking off moves the cursor; a press with no release
  works; existing snapping behavior unchanged.
- SetUserVar: base64 encoding; `WEZTERM_PROG` only on change and cleared at the prompt;
  user/host every prompt; builtin works in scripts.
- tmux: with `$TMUX` set, SetUserVar is wrapped with ESC doubled; 133 and 7 are plain; no 3008.
- OSC 3008: re-announce carries the full field set; first 3008 follows the first `A`.
- OSC 3008: field escaping, exit classification table, id stability across prompts, deferral
  until first prompt, `shell_integration <- 0` suppresses everything listed.
- **Manual check in Contour**: jump-to-prompt lands on the first prompt line; fold hides output
  and the following spacer; sticky header shows multi-line commands; a `kill -SEGV` child shows
  as crashed.

## Contour follow-ups (filed, not part of this work)

- contour-terminal/contour#2161 — Support `OSC 1337 SetUserVar`.
- contour-terminal/contour#2162 — `OSC 9;9` pops a "ConEmu" notification instead of setting the cwd.
- contour-terminal/contour#2163 — OSC 133: optional nonce to reject forged command lines and marks.

## Left to the builder

- The exact shape of the core-cpp per-row zone seam (subject to the core-cpp rule and §3).
- How the transient rewrite keeps the marked line.
- Test harness choice for escape-sequence assertions.
- Internal structure of the emitters.

## Revisions after the conversation

- An early playback proposed `redraw=0`; it was dropped because it means the opposite of what endo
  does. `k=s` and `k=r` were briefly dropped as "ignored by Contour", then reinstated by the user
  under the widely-understood rule.
- A second review found that `aid=` breaks kitty's `C`/`D` parsing, that OSC 3008 re-announces
  must carry every field, that Konsole treats `P` like `A`, and that click-to-cursor already
  exists. The design was adjusted accordingly (`aid=` dropped, quirk table, full re-announce,
  3008 not sent inside tmux).
