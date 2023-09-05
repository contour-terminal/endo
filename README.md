# Crush Shell

**Companion shell to Contour Terminal.**

**DO NOT FORK JUST YET, AS MASTER BRANCH WILL BE FORCE-PUSHED DURING INITIAL PHASE**


Let's make it clear, we have great shells like `bash` and `zsh` or even `fish` already.

This shell is not meant to compete with `bash`.
It aims to be primarily used as an interactive shell, similar to `fish` shell,
however, utilizing the more modern terminal features you might think of,
while retaining most of the well known good syntax features of Bash.

Crush shell aims to be as close as reasonably possible compatible with bash,
but explicitly does not attempt to be a bash clone.

## Mission

Design modern interactive shell with first-class UX in mind.

- ✅ Core POSIX shell features to satisfy power users (pipes, job management, environment variables, ...)
- ✅ First class Unicode support with respect to grapheme cluster display (if terminal supports it)
- ✅ IDE like command prompt, including mouse support using new (passive mouse tracking) VT extension
- ✅ LSP-like features like tooltips on mouse-hoever and auto-complete like in an IDE
- ✅ DAP: Debugging Mode via [Debugging Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol//)
- ✅ Make URIs and paths clickable (OSC-8) in the prompt
- ✅ Utilize VT420 host writable status line to indicate shell status
- ✅ Fast working directory traversal as inspired by fish shell
- ✅ Support input binding customizations

## status: core & language (milestone 1)

There is no timeline, as Contour Terminal still remains the top priority. Crush comes second.

- [x] basic process execution
- [x] shell pipes with processes
- [ ] shell pipes with builtin commands
- [ ] file descriptor redirects
- [ ] job management
- [x] builtin `read` function (basic version)
- [ ] set variable and export to inheritable environment
- [ ] variable substitution
- [x] bash-like if-statement
- [x] bash-like while-statement
- [ ] bash-like functions
- [ ] bash-like brace expansions
- [ ] bash-like tilde (~) expansions
- [ ] bash-like parameter expansions
- [ ] bash-like arithmetic expansions
- [ ] bash-like pathname expansions
- [ ] bash-like command substitution: `$()` and its backtick version
- [ ] bash-like process substitution: `<(cmd)` and `>(cmd)`
- [ ] operator && and ||
- [ ] export and unset variables

## status: terminal UX (milestone 2)

- [ ] rich text editor for the prompt
- [ ] rich text editor: mouse integration, for cursor positioning and tooltips (via VT extension)
- [ ] rich text editor: text selection (via VT extension)
- [ ] rich text editor: LSP like completion and suggestions
- [ ] customizable prompt
- [ ] utilize host programmable statusline

## Open Questions

- Does it make sense to support Windows?
