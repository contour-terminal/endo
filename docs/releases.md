---
title: Releases
description: Endo release history and installation instructions.
---

# Releases

## Changelog

### 0.1.0 -- Initial Public Release

**Language & Runtime**

- F#-inspired functional syntax: let bindings, lambdas, pattern matching, pipe operators
- Hindley-Milner type inference (Algorithm W) with optional annotations
- Algebraic data types: records, discriminated unions, tuples
- Result/Option types with `?` propagation and `try-with`/`try-finally`
- Lazy evaluation: `lazy expr`, `force`, `seq { yield ... }` sequences
- Module system with `import`/`open`, standard library, user modules
- Ref cells (`ref`, `.value`, `<-`) with GC write barrier
- Indirect function calls, partial application, closures, mutual recursion
- Mark-and-sweep garbage collector with slab-allocated object pool

**Shell**

- Bash-compatible command execution: pipes, redirects, job control, globbing
- 60+ inline builtins: file ops, text processing, networking, process management
- Structured output: `ls`, `ps`, `jobs` return typed records
- Output recognition files (`.endo-output.yml`) for external command output
- Scoped resource management (`let use`) with auto-dispose
- `history` (list, search, clear) and `source` builtins
- `tail -f` follow mode for files and stdin

**Interactive UX**

- Multiline editor with undo/redo, clipboard (OSC 52), mouse support
- Fish-style ghost text suggestions from persistent history
- Context-aware tab completion (commands, files, variables, git, modules)
- Command palette (Ctrl+Shift+P) with fuzzy search
- Configurable prompt with 10 presets
- Kitty keyboard protocol support

**IDE Integration**

- Full LSP server: hover, completions, go-to-definition, find references, rename,
  semantic tokens, signature help, formatting, code actions, inlay hints, call hierarchy
- Source formatter (`endo format`) with idempotent round-trip
- Debug Adapter Protocol (DAP) scaffolding

**Cross-Platform**

- Linux, macOS, and Windows support
- Platform abstraction layer for processes, pipes, TTY

---

## Installation

### Build from Source

The recommended way to obtain Endo today is to build from the Git repository:

```bash
git clone https://github.com/contour-terminal/endo.git
cd endo
cmake --preset clang-release
cmake --build --preset clang-release
sudo cmake --install build/clang-release
```

See the [Getting Started](getting-started.md) guide for detailed prerequisites and build
instructions.

### Windows Installer (.msi)

Windows releases ship as an `.msi` installer. It installs Endo into a version-specific
directory under `C:\Program Files\Endo\` (for example `C:\Program Files\Endo\1.2.3\`) and
adds that version's `bin` directory to the system `PATH`.

Because each version installs side by side in its own directory, **you can upgrade or
reinstall while Endo is still running** -- the installer never replaces the locked,
in-use binary, so it does not ask you to close running `endo.exe` processes or force a
reboot. Running sessions keep working; you decide when to restart them. After an upgrade,
the new version's `bin` is placed first on `PATH`, so `endo` resolves to the newest
version, and the previous version's files are removed automatically (on the next reboot if
it was still in use). See
[Platform Differences](shell/platform-differences.md#windows) for setup details.

If you are upgrading from a pre-`.msi` Endo that was installed with the old `.exe`
(NSIS) installer, the `.msi` automatically removes that installer's leftover
`C:\Program Files\Endo\bin` entry from `PATH`. Its files (directly under
`C:\Program Files\Endo\`) are not touched by the `.msi`; remove them with the old
uninstaller or by hand if you no longer need them.

### GitHub Repository

The source code, issue tracker, and development activity are hosted on GitHub:

[**github.com/contour-terminal/endo**](https://github.com/contour-terminal/endo)

### Package Managers

!!! warning "Coming Soon"
    Package manager support is on the roadmap but not yet available.

Planned package manager support includes:

| Package Manager | Platform | Status |
|----------------|----------|--------|
| Homebrew | macOS, Linux | Planned |
| Scoop | Windows | Planned |
| AUR | Arch Linux | Planned |
| APT / PPA | Debian, Ubuntu | Planned |
| RPM / COPR | Fedora, RHEL | Planned |
| Nix | NixOS, any | Planned |

If you would like to help package Endo for your preferred distribution, contributions are
welcome. See [Contributing](contributing.md) for details.

## Versioning

Endo follows [Semantic Versioning](https://semver.org/). Until the 1.0 release, the API
and language syntax may change between minor versions. After 1.0, backward compatibility
will be maintained within major versions.
