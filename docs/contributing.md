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
git remote add upstream https://github.com/christianparpart/endo.git
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

- All new functionality should be covered by unit tests.
- Run the full test suite before submitting a pull request.
- Tests are located alongside source files (e.g., `Foo_test.cpp` next to `Foo.cpp`).
- Use the existing test helpers (`executeSourceAndGetOutput()`, `executesSuccessfully()`,
  `generatesIRSuccessfully()`) for language and IR tests.

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
  shell/             # Shell runtime (builtins, job control, prompt)
  tui/               # Terminal UI library (input, rendering, widgets)
  CoreVM/            # Stack-based bytecode virtual machine
```

## Roadmap Reference

When working on a feature, check the [Roadmap](roadmap/index.md) for dependencies and
context. Ensure prerequisites are complete before starting work on dependent features.

## License

By contributing, you agree that your contributions will be licensed under the
[Apache License 2.0](https://github.com/christianparpart/endo/blob/master/LICENSE).
