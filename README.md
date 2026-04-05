<div align="center">

# Endo

**A cross-platform shell with F#-inspired functional programming.**

[![Linux](https://img.shields.io/badge/Linux-supported-brightgreen?logo=linux&logoColor=white)](#installation)
[![macOS](https://img.shields.io/badge/macOS-supported-brightgreen?logo=apple&logoColor=white)](#installation)
[![Windows](https://img.shields.io/badge/Windows-supported-brightgreen?logo=windows&logoColor=white)](#installation)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](#license)

---

[Getting Started](#getting-started) · [Features](#features) · [Examples](#examples) · [Documentation](https://endo-lang.org/) · [Installation](#installation) · [Contributing](#contributing)

</div>

> **Early Development** — Endo is under active development. The language, builtins, and APIs
> may change. Feedback and contributions are very welcome!

## What Is Endo?

Endo is an interactive shell and scripting language that combines familiar command-line
conventions with ideas from functional programming — primarily F#. It runs natively on
Linux, macOS, and Windows.

- **Structured pipelines** — data flows as typed records, not only plain text
- **Pattern matching and algebraic types** — `Option`, `Result`, discriminated unions
- **Type inference** — types are checked but rarely need to be written
- **Bash compatibility** — redirects, globs, `&&`/`||`, and command execution work as expected

## Features

### F#-Inspired Language

Pipe operators, pattern matching, immutable-by-default bindings, and first-class functions
form the core of the language.

### Familiar Shell Conventions

Run commands, redirect output, glob files, chain with `&&` and `||`. Everyday shell usage
works the way you'd expect.

### Structured Pipelines

Pipelines pass typed records between stages, so data stays intact from source to sink.

### Cross-Platform

Runs natively on Linux, macOS, and Windows from the same source — scripts are portable
without compatibility layers.

### Context-Aware Completions

Tab completions are informed by the type system, offering relevant suggestions based on
what a command or function expects.

### Explicit Error Handling

Result types (`Ok`/`Error`) and option types (`Some`/`None`) make error paths visible
and composable.

## Examples

**Basic usage:**

```bash
# It's still a shell — run anything
ls -la
git status && echo "All clean"
```

```fsharp
# F#-style bindings and string interpolation
let name = "world"
println $"Hello, {name}!"
```

**Shell commands in functional pipelines:**

```fsharp
# Structured builtins return typed records — no text parsing needed
ps |> filter (_.command |> contains "endo") |> length
|> fun n -> println $"Found {n} endo processes"

# git log returns structured commit data
git log |> take 5 |> each (fun c -> println c.message)
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

Contributions are welcome — bug reports, feature ideas, documentation improvements, or code.

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-thing`)
3. Commit your changes
4. Open a pull request

Please see [CONTRIBUTING.md](CONTRIBUTING.md) for detailed guidelines.

## License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.

---

<div align="center">

[endo-lang.org](https://endo-lang.org/)

</div>
