# Endo Language & documentation
- The endo language specification is in `docs/language/` (split across 15 pages)
- The status of the endo language implementation is tracked in `ROADMAP-Language.md`
- Be humble and open in the documentation and specification. Avoid marketing language and exaggerations. Focus on the facts and be transparent about the current state of the implementation, its limitations, and future plans.

# User-facing built-ins and language syntax / semantics
- User-facing builtin functions and properties must be fully documented and available to the LSP and shell completion system
- language syntax and semantics changes must be fully documented in the language specification and available to the LSP

# Building
- Use CMake with preset "clang-debug" for building and testing on Linux with Clang in debug mode.
- Use CMake with preset "clang-release" for performance testing on Linux with Clang in release mode.

# Testing
- Write tests via e2e tests in `tests` directory to be executed via `endo-test` if possible - use catch2 only if not possible as e2e test.
- Run the tests using `ctest --preset=clang-debug` or `ctest --preset=clang-release` depending on the build type.

# C++ Coding Guidelines
- Use data driven design and avoid hardcoding values in the code.
- Use dependency injection to decouple components and improve testability.
- Document new functions, parameters, returns, classes, structs, and their members using Doxygen style comments.
- Use const correctness throughout the codebase.
- Prefer C++23 with constexpr, std::ranges, std::format, where applicable.
- C-style loops are forbidden; use range-based for loops instead.
- Use std::views::iota and other views for generating and transforming ranges.
- Use std::span for passing arrays and contiguous sequences.
- Use std::expected for error handling and its functional style methods like and_then, or_else, transform, transform_error, etc.
- Use range based for loop, structured bindings, and algorithms from the standard library.
- Use clang-format after changes to format code according to project style.
- Use auto-type declaration for variables to improve code readability.
- Ensure changes are covered by unit tests and aim always for increased code coverage.
- Reports from clang-tidy should not be cast away via NOLINT comments; instead, address the underlying issues.

# Workflow
- When done implementing the changes, always update the ROADMAP.md file to reflect the current state.
- Extend the documentation according to the language or CLI changes, if any.
- When `.endo` files were created or updated, make sure they're formatted according to the endo language specification and that they pass the endo format tool.
- When done with the code changes, execute /simplify command and also avoid code duplications.
- Always mention the performance impact in detail in the summary, if any.
- Always perform and add risk assessment in the summary, if possible.
- Always run full test suite when done.
- Always report the code coverage results in the summary.
