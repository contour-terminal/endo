<div align="center">

# Endo

**A modern, cross-platform shell where functional programming meets everyday productivity.**

[![Linux](https://img.shields.io/badge/Linux-supported-brightgreen?logo=linux&logoColor=white)](#installation)
[![macOS](https://img.shields.io/badge/macOS-supported-brightgreen?logo=apple&logoColor=white)](#installation)
[![Windows](https://img.shields.io/badge/Windows-supported-brightgreen?logo=windows&logoColor=white)](#installation)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](#license)

*The shell you always wanted — F#-inspired, Bash-convenient, everywhere.*

---

[Getting Started](#getting-started) · [Features](#features) · [Examples](#examples) · [Installation](#installation) · [Contributing](#contributing)

</div>

## Why Endo?

Shells haven't evolved. You're still gluing strings together, guessing at exit codes, and writing
brittle pipelines. **Endo changes that.** It brings the expressive power of functional programming
to your terminal — without sacrificing the quick-and-dirty convenience you rely on every day.

- **Pipelines that carry structured data**, not just text
- **Pattern matching and algebraic types** right at your prompt
- **Type inference** that stays out of your way until you need it
- **One shell, every platform** — no more `#!/bin/bash` on machines that don't have it

## Features

### Functional at Its Core

Endo's language is heavily inspired by F#. Pipe operators, pattern matching, immutable-by-default
bindings, and first-class functions are not bolted on — they're the foundation.

### Bash-Like When You Need It

Run commands, redirect output, glob files, chain with `&&` and `||`. If your muscle memory
speaks Bash, Endo understands.

### Structured Pipelines

Stop parsing `grep | awk | sed` chains. Endo pipelines pass typed records between stages,
so data stays intact from source to sink.

### Cross-Platform, No Compromises

Native support for Linux, macOS, and Windows. Write scripts once, run them everywhere —
no compatibility layers, no emulation.

### Intelligent Completions

Context-aware tab completions powered by the type system. Endo knows what a command expects
before you finish typing it.

### Sane Error Handling

No more silent failures. Endo uses result types inspired by `Result<T, E>`, giving you
explicit, composable error handling without the ceremony.

## Examples

**Familiar commands, elevated syntax:**

```bash
# It's still a shell — run anything
ls -la | where { .size > 1mb } | sort-by modified

# Variables and string interpolation
let name = "world"
echo $"Hello, {name}!"
```

**Pipelines with structure:**

```bash
# Query processes like data, not text
ps | where { .cpu > 10.0 } | select name cpu mem | sort-by cpu --desc
```

**Pattern matching on the prompt:**

```fsharp
# Handle command results explicitly
match (fetch "https://api.example.com/status") with
| Ok response -> echo $"Status: {response.code}"
| Error e     -> echo $"Failed: {e.message}" >&2
```

**First-class functions and piping:**

```fsharp
# Functional transforms feel natural
let sizes = ls | map { .size } | filter { it > 1kb }
echo $"Large files: {sizes |> length}"
```

**Quick one-liners stay quick:**

```bash
# Rename all .jpeg files to .jpg
ls *.jpeg | each { mv $it.name ($it.name | str replace ".jpeg" ".jpg") }

# Find the 5 largest files recursively
glob **/* | where { .is_file } | sort-by size --desc | take 5
```

**Cross-platform scripting:**

```fsharp
# Works the same on Linux, macOS, and Windows
let config_dir = match (env OS) with
    | Some "Windows_NT" -> $"{env APPDATA}/endo"
    | _                 -> $"{env HOME}/.config/endo"

mkdir -p $config_dir
```

## Getting Started

```bash
# Clone and build
git clone https://github.com/christianparpart/endo.git
cd endo
cmake --preset clang-release
cmake --build --preset clang-release

# Launch
./build/clang-release/src/shell/endo
```

## Installation

### From Source

```bash
git clone https://github.com/christianparpart/endo.git
cd endo
cmake --preset clang-release
cmake --build --preset clang-release
sudo cmake --install build/clang-release
```

### Package Managers

> Coming soon — Homebrew, Scoop, and distro packages are on the roadmap.

## Contributing

Contributions are welcome. Whether it's a bug report, a feature request, or a pull request —
every bit helps shape Endo into the shell it should be.

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-thing`)
3. Commit your changes
4. Open a pull request

Please see [CONTRIBUTING.md](CONTRIBUTING.md) for detailed guidelines.

## License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.

---

<div align="center">

**Endo** — *Stop parsing. Start piping.*

</div>
