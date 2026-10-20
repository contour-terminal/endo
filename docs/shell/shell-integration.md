---
title: Shell Integration
description: The escape sequences Endo sends to its terminal, when it sends them, and what terminals do with them.
---

# Shell Integration

When Endo runs interactively on a terminal, it tells the terminal what it is doing: where each
prompt starts, where the command line is, when a command runs and how it ended, which directory
the shell is in, and which shell session this is. Terminals use this for things like jumping
between prompts, selecting or folding a command's output, a sticky header with the running
command, finish notifications, and opening a new tab in the current directory.

This page lists every sequence Endo sends, when it sends it, and what is known about how terminals
treat it. It describes what Endo does. What a terminal does with a sequence is up to the terminal,
and the notes about specific terminals come from their documentation and source code unless
stated otherwise. Most were not verified by running Endo in that terminal.

## What is sent

All sequences are terminated with ST (`ESC \`). Nothing is sent unless the shell is interactive
and attached to a terminal, and `shell_integration` is on (see [below](#turning-it-off)).

### OSC 133: semantic prompt marks

| Sequence | When | Notes |
|---|---|---|
| `OSC 133 ; A ; click_events=1 ST` | Once per drawn prompt, at column 1 of the prompt's first visible row. | A new draw is, for example: the prompt after a command, after an abandoned prompt (Ctrl+C), after the screen was cleared (Ctrl+L), and the prompt redrawn below a completion error. Redraws while editing, resizing, or a completion popup do not send it again. |
| `OSC 133 ; P ; k=i ST` | When the first prompt row is repainted. | Re-establishes the row's zone (see [Repaints](#repaints)). |
| `OSC 133 ; P ; k=s ST` | At column 1 of every prompt row after the first. | Chrome rows below the first one (two-line, powerline and boxed layouts), and every row of a multi-line command. |
| `OSC 133 ; P ; k=r ST` | At the column where the right-aligned prompt begins, on the row it is drawn on. | Only when a right prompt is drawn. |
| `OSC 133 ; B ST` | On every input row, where the editable text begins (after the indicator). | |
| `OSC 133 ; C ; cmdline_url=<percent-encoded> ST` | When a command starts running. | The command line is exactly what was submitted. A bare Enter sends `C` without the parameter. |
| `OSC 133 ; D ; <status> ST` | When the command has finished. | `<status>` is the exit status of the command line. |

`C` and `D` carry exactly one parameter each. The percent-encoding leaves `A-Z a-z 0-9 - . _ ~ /`
as they are and encodes every other byte, so ESC, BEL, `;`, `%`, newlines and non-ASCII text
cannot end or corrupt the sequence.

Endo does not send `aid=`, `cl=`, `redraw=` or `OSC 133 ; N`/`L`.

### OSC 7: working directory

`OSC 7 ; file://<host>/<path> ST` is sent before each prompt is drawn and again after `cd`.

### OSC 3008: context signalling

`OSC 3008` ([UAPI.15](https://uapi-group.org/specifications/specs/osc_context/)) announces
hierarchical contexts: the shell session, and one context per command inside it.

| When | Sequence |
|---|---|
| After every prompt is drawn | `OSC 3008 ; start=<shellId> ; type=shell ; user=… ; hostname=… ; machineid=… ; pid=… ; comm=endo ; cwd=… ST`. The same id is re-announced with the full field set and the current `cwd` each time, since a repeated `start=` resets every omitted field. The first one follows the first `133;A`. |
| After `133;C` | `OSC 3008 ; start=<cmdId> ; type=command ; cmdline=… ; cwd=… ST` |
| Before `133;D` | `OSC 3008 ; end=<cmdId> ; exit=<success\|failure\|crash\|interrupt> ; status=<n> [; signal=<NAME>] ST` |
| When the shell exits | `OSC 3008 ; end=<shellId> ; exit=success ; status=<n> ST` |

- Context ids are 128 random bits as 32 lowercase hex digits.
- `machineid=` is read from `/etc/machine-id` and omitted when the platform has none.
- Inside values, `;` is written `\x3b`, `\` is written `\x5c`, and control characters are
  removed. A `cmdline=` longer than 255 bytes is truncated on a UTF-8 boundary (the full line
  travels in `cmdline_url`); a `cwd=` that long is left out rather than cut into a wrong path.
- One command context covers one submitted line, like `133;C`/`D`; it is not one per pipeline
  stage.
- The exit class comes from how the process actually ended, not from its status: a status of 139
  from `exit 139` is a `failure`. No signal and status 0 is `success`, another status is
  `failure`, SIGINT is `interrupt`, SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS and SIGTRAP
  are `crash`, and any other signal is `failure` with `signal=` set. For a pipeline or list the
  outcome is that of the process that produced the reported status.
- A foreground command stopped with Ctrl+Z ends its context with `exit=interrupt` and
  `signal=SIGTSTP`, as UAPI.15 has no "stopped".
- A nested Endo announces its own shell context; it does not end or reuse its parent's.
- Endo does **not** send OSC 3008 inside tmux (see [tmux](#tmux)).

### OSC 1337 SetUserVar: user variables

`OSC 1337 ; SetUserVar=<name>=<base64(value)> ST`, used by WezTerm, kitty and iTerm2 to hand
named values to their configuration. Endo sends these automatically, using the names WezTerm's
own shell integration uses:

| Variable | Value | When |
|---|---|---|
| `WEZTERM_PROG` | The command line while a command runs; empty at the prompt. | When a command starts (only if it differs from the last value), and cleared at the next prompt. |
| `WEZTERM_USER` | The login name. | After every prompt is drawn. |
| `WEZTERM_HOST` | The host name. | After every prompt is drawn. |
| `WEZTERM_IN_TMUX` | `1` inside tmux, `0` otherwise. | After every prompt is drawn. |

Scripts can set their own with the [`set-user-var`](builtins.md#set-user-var) builtin.

Contour does not currently understand `SetUserVar`
([contour-terminal/contour#2161](https://github.com/contour-terminal/contour/issues/2161)).

## Where the marks go

A prompt is drawn as a block of rows, top to bottom:

```text
blank spacer row          (shell_prompt_spacing)         not marked
aurora row, if enabled                                   not marked
first visible prompt row                                 133;A (and, for the
                                                         single-line layout, also
                                                         133;B at the input text)
  second row onwards: further chrome rows                133;P;k=s
  input row(s)                                           133;P;k=s, then 133;B at the input text
blank spacer row          (shell_prompt_spacing)         part of the prompt
```

The mark goes on the first visible prompt row, not on the spacer or aurora row above it. This
matters for terminals that stamp the mark on the line holding the cursor when `A` arrives.

The top spacer row therefore sits between the previous command's `D` and the new `A`, which makes
it the tail of the previous command's block. The bottom spacer row belongs to the prompt, and the
next command's output starts below it.

### Transient prompts

With `shell_prompt_transient` enabled, the prompt collapses to a one-line (or arrow) version
after Enter. The collapse keeps the mark on the line that becomes the transient line:

- the top spacer rows are cleared;
- the aurora row, when drawn, is removed with a line deletion (`DL`), which moves the marked row
  up with its flags;
- the marked row is never erased whole, because tmux drops a line's OSC 133 flags when the
  entire line is erased. The transient text is written first and only the rest of the row is
  cleared; an empty row gets a single space so the clear starts past column 1.

### Repaints

Some terminals (Ghostty and WezTerm, from their sources) tag each cell with the zone that was in
force when it was written, rather than tagging whole lines. Endo repaints rows after the prompt
was first drawn, for example for the clock or git modules and while editing. So whenever it
repaints a prompt row it first re-sends that row's mark (`P;k=i` or `P;k=s` at column 1, `B` at
the input start, `P;k=r` at the right prompt) instead of relying on the earlier one. `A` is only
ever sent for a new prompt. For terminals that keep marks per line (Contour, kitty, tmux,
iTerm2), repeating a mark on the same line and column changes nothing.

### Click to move the cursor

`click_events=1` on `A` asks the terminal to report a left click inside the prompt as an SGR mouse
press (`CSI < 0 ; x ; y M`) even when mouse reporting is off. Endo moves the text cursor to the
clicked position; a click on chrome or padding snaps to the nearest input line. A lone press is
enough, because some terminals send no release. kitty does this, and Ghostty does it when its
`cursor-click-to-move` option is enabled. Terminals that do not know the parameter ignore it.

## Turning it off

```endo
# Disable terminal shell integration
shell_integration <- false

# Re-enable (default)
shell_integration <- true
```

Off suppresses OSC 133, OSC 7, OSC 3008 and the automatic `SetUserVar` updates. It does not
affect OSC 2 (window title), OSC 8 hyperlinks (`shell_hyperlinks`), or the
[`set-user-var`](builtins.md#set-user-var) builtin, which is an explicit request.

Setting it in `init.endo` takes effect before the first prompt, and the session's first OSC 3008
`start=` is only sent after the first prompt is drawn. Switching it off later ends the open
shell context and then sends nothing; switching it on starts a new shell context with a new id at
the next prompt. A command that runs while the setting changes (such as
`shell_integration <- false` typed at the prompt) is finished the way it was started: if its
`133;C` was sent, its command context's `end=` and its `133;D` follow; if not, neither does.

## Terminal-specific behaviour

Endo detects a few terminals from environment variables and adjusts. The adjustments live in one
table in the source (`ShellIntegrationSequences.cpp`).

| Environment | Adjustment | Reason |
|---|---|---|
| `KONSOLE_VERSION` set | No `OSC 133 ; P` at all (`k=i`, `k=s`, `k=r`). `A`, `B`, `C`, `D` are still sent. | Konsole treats `P` like `A`, so every `P` would start a new prompt there (from Konsole's source). |
| `TMUX` set | No `OSC 133 ; P ; k=r`. | tmux treats a `P` other than `k=s` as a prompt start and moves its prompt column (from tmux's source). |
| `TMUX` set | No OSC 3008. | tmux interprets OSC 133 itself and does not forward it, so the outer terminal would see only 3008 and could derive prompt marks from tmux's composited screen, interleaved across panes. |
| `TMUX` set | `SetUserVar` is wrapped in tmux's DCS passthrough. | Only the outer terminal understands it. |

### tmux

tmux interprets OSC 133 and OSC 7 itself, so Endo sends those plain. Tested with tmux 3.7c, which
interprets OSC 133: copy-mode's previous-prompt jump lands on the first visible line of the
prompt, and on the collapsed line of a transient prompt.

`SetUserVar` inside tmux is sent as `DCS tmux ; <sequence with every ESC doubled> ST`, which tmux
forwards only when passthrough is enabled. It is off by default; add this to `tmux.conf`:

```text
set -g allow-passthrough on
```

### Other terminals

What the sequences are documented or known (from source) to do:

| Terminal | Reads |
|---|---|
| Contour | `A`, `B`, `C` (with `cmdline_url`), `D` for command blocks, prompt navigation, folding, the sticky header and finish notifications. `P;k=r` marks where a right prompt begins, so it is left out when the command line is read off the screen. `P;k=s`, `P;k=i` and `click_events` are accepted and ignored. OSC 3008 contributes metadata only (working directory, identity, `exit=` and `signal=`) once OSC 133 has been seen, and only when its `osc_context` setting is on. No `SetUserVar`. |
| kitty | `A`, `B`, `C`, `D`; `click_events=1`; `SetUserVar`. Reads `k=s` only on `A`, so continuation marks are not seen. |
| Ghostty | `A`, `B`, `P;k=s`, `P;k=r`; `click_events=1` for click to move (option `cursor-click-to-move`). |
| WezTerm | OSC 133 zones, `SetUserVar`. |
| iTerm2 | OSC 133 marks, `SetUserVar`. |
| tmux | OSC 133 (prompt navigation in copy mode, `cmdline_url`, `D` status), OSC 7. |

This is a summary of reading each project's documentation and source, not of testing Endo in
each. Terminals not listed were not examined, and unknown sequences are normally ignored.

## Known limitations

- A row's continuation flag outlives the row being a prompt row. When the input shrinks (lines
  deleted from a multi-line command) or a transient prompt collapses, the cleared rows can keep
  their `P;k=s` flag in Contour and tmux. In Contour this has no visible effect, as it ignores
  `k=s`. In tmux, a `k=s` flag on an output line does not make it a prompt-navigation target.
- An abandoned prompt (Ctrl+C) may leave an empty command block in the terminal, as bash and zsh
  do. The new prompt gets its own `A`.
- `OSC 3008` is not sent inside tmux, so there are no command contexts there.
- The nested-shell id (`aid=`) is not sent. It would break kitty's parsing of `C` and `D`.
- Only absolute click reporting (`click_events=1`) is requested, not `=2` or `cl=`.
- VS Code's `OSC 633`, VTE's `OSC 666`/`OSC 777` lifecycle signals, ConEmu's `OSC 9;9`, and
  single-terminal extensions (kitty `special_key`, Ghostty `redraw=last`, iTerm2's
  `CurrentDir`/`RemoteHost`/`SetMark`) are not sent.
