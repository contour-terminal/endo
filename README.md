# Contrair Shell

**Companion shell to Contour Terminal.**

Let's make it clear, we have great shells like `fish` and `zsh` or even `bash` already.

This shell is meant not to compete with `bash` nor `zsh` but rather to be similar to `fish`,
however, utilizing all modern terminal features you might think of.

Think of it as a research project on what is possible in a shell from a UX point of view.

## Mission

Do not try to be POSIX compliant as this is not meant for scripting (use Bash for that!) but rather
design modern shell with first-class UX in mind.

- ✅ Core shell features (pipes, job management, environment management).
- ✅ First class Unicode support with respect to grapheme cluster display.
- ✅ IDE like command prompt, including mouse support using new (passive mouse tracking) VT extension.
- ✅ LSP-like features like tooltips on mouse-hoever and auto-complete like in an IDE.
- ✅ Make URIs and paths clickable (OSC-8).
- ✅ Utilize VT420 host writable status line to indicate shell status.
- ✅ fast working directory traversal as inspired by fish shell.
