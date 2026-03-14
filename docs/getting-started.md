---
title: Getting Started
description: Build Endo from source and write your first script.
---

# Getting Started

This guide walks you through building Endo from source, launching the shell, and writing
your first commands.

## Prerequisites

Before building, ensure you have:

- A **C++23-compatible compiler** (Clang 17+ recommended, GCC 14+ also works)
- **CMake 3.26** or newer
- **Git** for cloning the repository
- A Linux or macOS system (Windows support is in progress)

!!! note "Compiler Support"
    Endo uses modern C++23 features including `std::expected`, `std::format`, and `constexpr`
    extensively. Clang 17+ with libc++ provides the best support.

## Build from Source

### Clone the Repository

```bash
git clone https://github.com/contour-terminal/endo.git
cd endo
```

### Configure and Build

Endo uses CMake presets for streamlined builds. For a **release build** (recommended for
daily use):

```bash
cmake --preset clang-release
cmake --build --preset clang-release
```

For a **debug build** (for development and testing):

```bash
cmake --preset clang-debug
cmake --build --preset clang-debug
```

### Install (Optional)

```bash
sudo cmake --install build/clang-release
```

## Launch Endo

After building, launch the interactive shell:

```bash
./build/clang-release/src/shell/endo
```

You should see the Endo prompt. Try a few commands:

```endo
# Classic shell commands work as expected
echo "Hello from Endo!"
ls -la
pwd
```

## Hello World

### Shell Style

```endo
echo "Hello, World!"
```

### F# Style

```endo
let greeting = "Hello, World!"
println greeting
```

### With String Interpolation

```endo
let name = "World"
println $"Hello, {name}!"
```

### Combining Shell and F#

```endo
# Capture shell output and process it functionally
let user = & whoami
println $"Welcome, {user}!"

# Pipe shell output into F# functions
ls | lines |> length |> fun n -> println $"Found {n} entries"
```

## Run the Tests

To verify your build is working correctly:

```bash
ctest --preset=clang-release
```

Or for the debug build:

```bash
ctest --preset=clang-debug
```

## Configuration

Endo loads `~/.config/endo/init.endo` on startup. You can place aliases, prompt
configuration, and other setup there:

```endo
# ~/.config/endo/init.endo

# Set the prompt preset
set_prompt_preset "endo-signature"

# Define aliases
let ll ...args = & exa -l ...args
let gs ...args = & git status ...args
```

To start Endo without loading this file, use `--no-profile`:

```bash
endo --no-profile
```

## Next Steps

- [Shell Features](shell/index.md) -- Learn about interactive features, builtins, and
  structured output
- [Language Reference](language/index.md) -- Deep dive into the Endo language
- [FAQ](FAQ.md) -- Common questions about pipes, lambdas, and more
- [Examples](examples/index.md) -- Practical code examples
- [Contributing](contributing.md) -- Help shape Endo's future
