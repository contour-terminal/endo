---
title: Contributing
description: How to contribute to the Endo project.
---

# Contributing

Contributions are welcome. Whether it's a bug report, a feature request, or a pull
request -- every bit helps shape Endo into the shell it should be.

## Getting Started

### 1. Fork and Clone

```bash
git clone https://github.com/<your-username>/endo.git
cd endo
git remote add upstream https://github.com/contour-terminal/endo.git
```

### 2. Create a Feature Branch

```bash
git checkout -b feature/amazing-thing
```

### 3. Build and Test

Endo uses CMake presets. For development, use the debug preset:

```bash
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --preset=clang-debug
```

For performance-sensitive changes, also verify with the release preset:

```bash
cmake --preset clang-release
cmake --build --preset clang-release
ctest --preset=clang-release
```

### 4. Commit and Open a Pull Request

```bash
git add <files>
git commit -m "feat: describe your change"
git push origin feature/amazing-thing
```

Then open a pull request on GitHub against the `master` branch.

## Code Style

Endo is written in modern C++23. Please follow these guidelines:

- **Use `clang-format`** after changes to ensure consistent formatting. The project
  includes a `.clang-format` configuration file.
- **Prefer `constexpr`**, `std::ranges`, and `std::format` where applicable.
- **Use `std::expected`** for error handling with functional-style methods (`and_then`,
  `or_else`, `transform`, `transform_error`).
- **Use `auto`** type declarations to improve readability.
- **Document** new functions, parameters, return values, classes, structs, and their
  members using Doxygen-style comments.
- **Use `const` correctness** throughout the codebase.
- **Avoid NOLINT comments** -- address clang-tidy reports at the source instead of
  suppressing them.

## Testing

Endo uses two test systems:

- **`.endo` test files** — for language behavior, IR generation, and execution tests.
  Located in `tests/` and run by the `endo-test` runner.
- **Catch2 C++ tests** — for internal C++ API tests (lexer, parser, type system, IDE).
  Located alongside source files (e.g., `Foo_test.cpp` next to `Foo.cpp`).

### Running tests

```bash
# Run all tests (Catch2 + endo-test)
ctest --preset=clang-debug

# Run only .endo tests
./build/clang-debug/src/endo-test/endo-test

# Filter .endo tests by pattern
./build/clang-debug/src/endo-test/endo-test "lists/*"
./build/clang-debug/src/endo-test/endo-test "*recursion*"

# TAP output format
./build/clang-debug/src/endo-test/endo-test --format tap

# List available tests
./build/clang-debug/src/endo-test/endo-test --list
```

### Writing a new `.endo` test

Create a `.endo` file in the appropriate `tests/` subdirectory with directives at
the top, followed by source code:

```
# description: Addition of two integers
# expect: 52

let x = 42
let y = 10
println (x + y)
```

Available directives:

| Directive | Purpose |
|---|---|
| `# description: <text>` | Human-readable test description |
| `# expect: <line>` | Expected output line (repeatable, joined with `\n`) |
| `# expect-exit: <code>` | Expected exit code (default: 0) |
| `# expect-error: <substring>` | Expected compilation error (empty = any error) |
| `# mode: <mode>` | `execute` (default), `ir-only`, `parse-only`, `structured` |
| `# skip: <reason>` | Skip this test |
| `# session-separator: <sep>` | Split source into REPL prompts |
| `# mock-env: KEY=VALUE` | Set mock environment variable |
| `# mock-which: PROG=/path` | Set mock which path |
| `# expect-env: KEY=VALUE` | Verify environment variable after execution |
| `# expect-nonempty` | Assert output is non-empty |

### Test directory structure

| Directory | Content |
|---|---|
| `basics/` | Let bindings, identifiers, comments, numeric literals |
| `arithmetic/` | Binary ops, unary ops, float arithmetic |
| `functions/` | Definitions, application, closures, partial application |
| `recursion/` | Let rec, mutual recursion |
| `lambdas/` | Lambda expressions, placeholder sugar |
| `control-flow/` | If-then-else, mutable assignment, block scopes |
| `match/` | Pattern matching on literals, options, results |
| `patterns/` | Or-patterns, as-patterns, tuple patterns |
| `types/` | Option, result, tuples, type annotations |
| `try-catch/` | `?` operator, try-with, try-finally |
| `pipelines/` | Pipeline operator |
| `lists/` | Construction, HOFs, comprehensions, ranges |
| `strings/` | Concatenation, interpolation, string library |
| `records/` | Record types, discriminated unions |
| `session/` | REPL persistence tests |
| `errors/` | Compile-time error detection |
| `builtins/` | Print, which, env, standard library |
| `shell/` | Shell integration, command substitution |
| `structured/` | Structured commands (ps, ls, docker, git) |
| `export/` | Let export, env verification |
| `let-in/` | Let-in expressions, nested scoping |
| `regression/` | Bug fix regression tests |

### When to use `.endo` vs Catch2

- **Language behavior** (syntax, semantics, output, errors) → `.endo` test file
- **C++ API** (lexer tokens, AST node construction, type inference) → Catch2 test

## Bug Reports

When reporting a bug, please include:

- Steps to reproduce the issue
- Expected behavior vs. actual behavior
- The Endo version (or Git commit hash)
- Your operating system and compiler version
- Any relevant error messages or log output

## Feature Requests

Feature requests are welcome. Please check the [Roadmap](roadmap/index.md) first to see if
your idea is already planned. When proposing a new feature:

- Describe the use case and motivation
- Show example syntax or behavior if applicable
- Note any potential impact on existing features

## Project Structure

```
src/
  endo-language/     # Core language library (lexer, parser, AST, IR)
  endo-test/         # .endo test runner (standalone, no Catch2)
  shell/             # Shell runtime (builtins, job control, prompt)
  tui/               # Terminal UI library (input, rendering, widgets)
  CoreVM/            # Stack-based bytecode virtual machine
tests/               # .endo test files (language behavior tests)
```

## Roadmap Reference

When working on a feature, check the [Roadmap](roadmap/index.md) for dependencies and
context. Ensure prerequisites are complete before starting work on dependent features.

## License

By contributing, you agree that your contributions will be licensed under the
[Apache License 2.0](https://github.com/contour-terminal/endo/blob/master/LICENSE).
