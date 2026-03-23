<div align="center">

# Endo

**A modern, cross-platform shell where functional programming meets everyday productivity.**

[![Linux](https://img.shields.io/badge/Linux-supported-brightgreen?logo=linux&logoColor=white)](#installation)
[![macOS](https://img.shields.io/badge/macOS-supported-brightgreen?logo=apple&logoColor=white)](#installation)
[![Windows](https://img.shields.io/badge/Windows-supported-brightgreen?logo=windows&logoColor=white)](#installation)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](#license)

*The shell you always wanted — F#-inspired, Bash-convenient, everywhere.*

---

[Getting Started](#getting-started) · [Features](#features) · [Examples](#examples) · [Documentation](https://endo-lang.org/) · [Installation](#installation) · [Contributing](#contributing)

</div>

> **Early Development** — Endo is under active development and has not yet had an official release.
> The language, builtins, and APIs may change. That said, there is already a lot to explore — dive in and share your feedback!

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
ls -la
git status && echo "All clean"

# F#-style bindings and string interpolation
let name = "world"
println $"Hello, {name}!"
```

**Shell output meets functional pipelines:**

```fsharp
# Pipe shell command output straight into F# transforms — it's still a shell
ps aux |> lines |> filter (contains "nginx" _) |> length
|> fun n -> println $"Found {n} nginx processes"

# Process git history with functional pipelines
git log --oneline |> lines |> take 5 |> each println
```

**Functional data processing:**

```fsharp
# Placeholder lambdas keep pipelines concise
[10; 25; 3; 42; 7] |> filter (_ > 10) |> map (_ * 2)   # [50; 84]

# Curried functions and partial application
let add x y = x + y
let add10 = add 10
[1; 2; 3] |> map add10              # [11; 12; 13]

# Function composition
let double = _ * 2
let inc = _ + 1
let doubleThenInc = double >> inc
print (doubleThenInc 5)              # 11
```

**Pattern matching at your prompt:**

```fsharp
# Option types for safe value handling
match (env "EDITOR") with
| Some editor -> print $"Using {editor}"
| None        -> print "No editor set"

# Result types for explicit error handling
let safeDiv x y =
    if y == 0 then Error "division by zero"
    else Ok (x / y)

match safeDiv 10 0 with
| Ok n    -> print $"Result: {n}"
| Error e -> print $"Failed: {e}"
```

**Lists, ranges, and comprehensions:**

```fsharp
# Ranges and comprehensions
let squares = [for x in [1..10] -> x * x]
let evens = [for x in [1..20] when x % 2 == 0 -> x]

# Recursive processing with pattern matching
let rec sum acc lst =
    match lst with
    | [] -> acc
    | head :: tail -> sum (acc + head) tail

print (sum 0 [1; 2; 3; 4; 5])       # 15
```

**Cross-platform scripting:**

```fsharp
# Works the same on Linux, macOS, and Windows
let config_dir =
    match (env "OS") with
    | Some "Windows_NT" -> "C:/Users/endo/config"
    | _                 -> $"{env "HOME" ?| "/tmp"}/.config/endo"

mkdir -p $config_dir
```

## Getting Started

```bash
# Clone and build
git clone https://github.com/contour-terminal/endo.git
cd endo
cmake --preset clang-release
cmake --build --preset clang-release

# Launch
./build/clang-release/src/shell/endo
```

## Installation

### From Source

```bash
git clone https://github.com/contour-terminal/endo.git
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
