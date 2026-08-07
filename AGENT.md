# Endo — Agent Guidelines

## Project Architecture

Endo is a modern interactive shell combining F#-style functional programming with bash-style shell scripting.

### Compilation Pipeline

```
Source → Lexer → Parser → AST → Semantic Analysis → Code Generation → CoreVM (bytecode execution)
```

### Component Map

| Directory | Purpose |
|-----------|---------|
| `src/CoreVM/` | Bytecode virtual machine, IR, type system, garbage collector, runtime |
| `src/endo-language/` | Compiler frontend: lexer, parser, AST, semantic analysis, codegen, formatter, IDE support |
| `src/shell/` | Interactive shell: builtins, job control, completion, history, TTY abstraction |
| `src/lsp/` | Language Server Protocol implementation (50+ providers across 4 tiers) |
| `src/platform/` | OS abstraction: `ProcessProvider`, `FileInfoProvider`, `EnvironmentProvider`, `FileSystem` |
| `src/agent/` | AI agent integration (LLM providers, MCP client) |
| `src/dap/` | Debug Adapter Protocol |
| `src/editor-protocol/` | Editor communication abstractions (`DocumentStore`, JSON transport) |
| `src/tui/` | Terminal UI components |
| `src/http/` | HTTP client abstraction (for `fetch` builtin) |
| `src/endo-test/` | E2E test runner for `.endo` test files |
| `src/testing/` | Shared testing utilities (test helpers, dialog suppression) |
| `src/crispy/` | Vendored utility library (from Contour Terminal) |
| `src/vtparser/` | Vendored VT terminal sequence parser (from Contour Terminal) |
| `src/coro/` | Vendored C++23 coroutine primitives — `Task`, `whenAll`/`whenAny`, cancellation (from Contour Terminal) |
| `src/net/` | Vendored coroutine-native networking — `EventLoop`, sockets, TLS, HTTP server (from Contour Terminal) |
| `tests/` | E2E test files organized by category (37+ subdirectories) |
| `docs/language/` | Language specification (18 pages, MkDocs site) |

---

## Design Patterns & Principles

**Dependency injection and data-driven design are always prioritized.** Treat both as the default for new code, not as optional refinements. Deviate only when there is a strong, explicitly-stated reason (e.g. a measured hot path where indirection is unacceptable), and call out that justification in the code and the PR summary.

### Dependency Injection via Constructor Injection

All OS, file system, network, and I/O access is abstracted behind interfaces and injected via constructors. Never hard-code side effects.

Canonical examples:
- `src/shell/TTY.hpp` — abstract terminal interface with `PosixTTY` / `WindowsTTY` implementations
- `src/platform/FileSystem.hpp` — file system abstraction (exists, read, write, list, metadata)
- `src/platform/ProcessProvider.hpp` — process listing abstraction (Linux, Darwin, Windows)
- `src/platform/EnvironmentProvider.hpp` — env var access abstraction (POSIX, Windows)
- `src/http/HttpClient.hpp` — HTTP abstraction injected into `ProviderFactory`, `McpClient`

When adding new functionality that touches the OS or network, define an abstract interface in `src/platform/` (or the relevant component), implement per-platform, and inject it.

### Data-Driven Design

Drive behavior from data — tables, descriptors, configuration — rather than from hand-written per-case logic. The goal is to avoid naive, repetitive implementations: when you find yourself writing N near-identical branches, copy-pasted blocks that differ only in a few constants, or several `switch`/`if` ladders that must be kept in lockstep, replace them with a single set of data and one piece of code that interprets it. Adding a new case should mean adding a data row, not writing new logic.

Reach for this pattern when you see:
- Repeated `switch`/`if` ladders over a closed set of cases
- Copy-pasted code blocks differing only in literal values (names, flags, descriptions, types)
- Several features (dispatch, parsing, help text, completion, LSP) that must all be updated together whenever a case is added — a sign they should share one descriptor

Canonical example: builtin metadata is declared once in `static constexpr` descriptor tables and consumed by many features. The `InlineOptionDef[]` tables in `src/shell/builtins/InlineCommandDescriptors.cpp` (e.g. `kRmOptions`, `kCpOptions`) declare each builtin's flags a single time, and that one declaration drives dispatch, argument parsing, help generation, completion, and LSP. To teach a builtin a new flag you add a row to its table — no new branching code. See "Adding a New Builtin Function" below for the practical workflow.

- Avoid hardcoding values; use configuration, tables, or descriptors
- Prefer a descriptor table feeding many consumers over parallel hand-maintained code paths
- As stated above, prioritize this approach by default; deviate only with a strong, documented reason

### Error Handling: `std::expected<T, E>`

Use `std::expected` for all fallible operations. Prefer monadic chaining (`and_then`, `or_else`, `transform`, `transform_error`) over if/else chains. Do not use `crispy::result` — `std::expected` is the standard.

Example:
```cpp
return sendRequest("initialize", std::move(params))
    .and_then([this](nlohmann::json const& result) -> std::expected<Capabilities, Error> {
        // ...
    });
```

Exceptions are reserved for truly exceptional, unrecoverable situations (e.g., `QuotaExceeded` in the VM).

### AST Visitor Pattern

Compiler and LSP features that traverse the AST use `ast::Visitor` and `pattern::PatternVisitor`. Follow this pattern for new language features (see `src/endo-language/format/SourceFormatter.cpp` as a canonical example).

### Memory Management

- Smart pointers (`std::unique_ptr`, `std::shared_ptr`) for ownership
- RAII for resource management
- Mark-and-sweep garbage collection exists **only inside CoreVM** for cyclic references among `TypedObject` instances — do not use or extend it elsewhere

---

## C++ Coding Guidelines

- Prefer C++23: `constexpr`, `std::ranges`, `std::format`, `std::expected`, structured bindings
- C-style loops are forbidden; use range-based for loops exclusively
- Use `std::views::iota` and other views for generating and transforming ranges
- Use `std::span` for passing arrays and contiguous sequences
- Use `auto` type deduction to improve readability
- Use `const` correctness throughout (refs, pointers, member functions)
- Mark return values `[[nodiscard]]` where ignoring the result would be a bug
- Document new public functions, classes, structs, and their members using Doxygen style:
  ```cpp
  /// Short description.
  /// @param name Description.
  /// @return Description.
  ```
- Naming conventions and static analysis rules are defined in `.clang-tidy` (authoritative source); clang-tidy runs automatically in debug builds
- Code formatting rules are defined in `.clang-format`; run `clang-format` after changes
- Do not suppress clang-tidy warnings with `NOLINT` comments; fix the underlying issue
- Use smart pointers for ownership; do not use raw owning pointers
- Do not introduce new third-party dependencies without strong justification
- Do not bypass the platform abstraction layer in `src/platform/`

---

## Endo Language & Documentation

- The language specification lives in `docs/language/` (multiple pages covering types, functions, pattern matching, error handling, etc.)
- Implementation status is tracked in `ROADMAP-Language.md`
- Be humble and factual in documentation. Avoid marketing language. Be transparent about limitations and current state.

---

## Vendored sources

`src/crispy`, `src/vtparser`, `src/coro` and `src/net` are **not** part of this
repository. They are sparse-checked-out from
[contour-terminal/contour](https://github.com/contour-terminal/contour), which is their
source of truth, and are gitignored here. CMake fetches them automatically at configure
time when they are missing; `scripts/get_contour_dirs.py` does it manually.

Which directories are fetched, from which repository, and at which ref are declared in
`scripts/contour-pin.json` — the single source of truth, read by both the script and
`src/CMakeLists.txt`. To vendor another directory or move to a different upstream branch,
edit that file; no code changes are needed.

```bash
python3 scripts/get_contour_dirs.py                  # fetch at the pinned ref
python3 scripts/get_contour_dirs.py --ref some/branch # override the ref once
ENDO_CONTOUR_REF=some/branch python3 scripts/get_contour_dirs.py  # same, via env
```

**Never edit files under these directories** — every fetch overwrites them. Fixes belong
upstream in contour, and flow back here on the next fetch. Endo-side code adapts to their
APIs, not the other way around.

---

## Building

```bash
# Configure (debug with ASAN, UBSAN, clang-tidy)
cmake --preset clang-debug

# Build
cmake --build --preset clang-debug

# Run all tests
ctest --preset clang-debug

# Coverage
cmake --preset clang-coverage
cmake --build --preset clang-coverage --target coverage
# HTML report in build/clang-coverage/coverage/

# Release (for performance testing)
cmake --preset clang-release
cmake --build --preset clang-release
```

---

## Testing

### Prefer endo-test (E2E tests)

Write E2E tests as `.endo` files in the `tests/` directory. Organize by category (e.g., `tests/builtins/`, `tests/control-flow/`, `tests/patterns/`).

**Test file format** — directives at top, source code below:

```endo
# description: What this test verifies
# expect: expected output line (repeatable, joined with \n)
# expect-exit: 0
# mode: execute

let x = 42
println x
```

**Key directives:**

| Directive | Purpose |
|-----------|---------|
| `# description: <text>` | Human-readable test description |
| `# expect: <line>` | Expected output line (repeatable) |
| `# expect-exit: <code>` | Expected exit code (default: 0) |
| `# expect-error: <substring>` | Expected compilation error (repeatable) |
| `# expect-nonempty` | Assert output is non-empty |
| `# mode: <mode>` | `execute` (default), `ir-only`, `parse-only`, `structured`, `shell` |
| `# skip: <reason>` | Skip this test |
| `# mock-env: KEY=VALUE` | Set mock environment variable |
| `# mock-which: PROG=/path` | Set mock which path |
| `# expect-env: KEY=VALUE` | Verify environment variable after execution |
| `# aux-file: <filename>` | Start auxiliary file section (multi-file/module tests) |
| `# main-file:` | End aux file section |
| `# module-path: <path>` | Add module search path |
| `# session-separator: <sep>` | Split source into REPL prompts |
| `# source-file: <path>` | Load external file as session prompt |
| `# unused-detection` | Enable unused value detection |

**Test modes:**
- `execute` — full pipeline: parse, analyze, codegen, run (default)
- `ir-only` — parse and generate IR only, no execution
- `parse-only` — parse only, verify no parse errors
- `structured` — execute with pre-populated structured command state
- `shell` — execute through a real Shell instance (for script/module tests)

### Use Catch2 only when endo-test cannot cover it

Catch2 unit tests go in the source tree as `*_test.cpp` files. Use them for:
- Internal data structures (PrefixTree, Cidr, ObjectPool)
- Parsing logic that needs programmatic assertions (InlineArgParser, FindExpression)
- Components that don't produce observable output (GarbageCollector, TypeSystem)

If the behavior can be expressed as "given this source, expect this output/error", use endo-test instead.

### Formatting `.endo` files

Configuration is in `.endo-format`. When `.endo` files are created or updated, format them:
```bash
endo format <file.endo>       # format in place
endo format --check <file>    # verify formatting (used in CI)
```

---

## Adding Features

### New Builtin Function

1. **Register** the builtin in `src/shell/builtins/Registration.cpp` using the method-chaining API:
   ```cpp
   _runtime.registerFunction("name")
       .param<CoreVM::CoreNumber>("arg")
       .returnType(CoreVM::LiteralType::Void)
       .bind(&Shell::builtinName, this);
   ```
2. For inline builtins (commands with options/flags), add a descriptor entry in `src/shell/builtins/InlineCommandDescriptors.cpp` — this drives dispatch, arg parsing, help text, and completion
3. **Document** in `docs/language/standard-library.md` and ensure the builtin metadata provides description text for LSP hover/completion
4. **Register for LSP** via `src/endo-language/builtins/BuiltinSignatures.hpp` (`registerInlineBuiltins()`)
5. **Test** with endo-test E2E tests in `tests/builtins/`

### New Language Syntax / Semantics

1. **Specify** in the relevant `docs/language/` page
2. **Lexer** — add tokens in `src/endo-language/lexer/`
3. **Parser** — add AST nodes and parsing in `src/endo-language/parser/`
4. **Semantic analysis** — add type checking in `src/endo-language/sema/`
5. **Code generation** — emit bytecode in `src/endo-language/codegen/`
6. **Formatter** — handle the new AST nodes in `src/endo-language/format/SourceFormatter.cpp`
7. **LSP providers** — update relevant providers (completion, hover, semantic tokens, etc.) in `src/lsp/`
8. **Test** with endo-test E2E tests covering happy path, errors, and edge cases

### New Platform Feature

1. Define an abstract interface in `src/platform/` (or the relevant component)
2. Implement per-platform in `src/platform/linux/`, `src/platform/darwin/`, `src/platform/windows/`, `src/platform/posix/`
3. Inject via constructor — never `#ifdef` platform checks in business logic

---

## Workflow (post-implementation checklist)

1. Run `clang-format` on added/changed C++ files
2. Run `endo format` on added/changed `.endo` files
3. Run the full test suite: `ctest --preset clang-debug`
4. Update the relevant roadmap file(s):
   - `ROADMAP.md` — general features and milestones
   - `ROADMAP-Language.md` — language feature implementation status
   - `ROADMAP-LSP.md`, `ROADMAP-DAP.md`, `ROADMAP-AGENT.md`, etc. — if applicable
5. Extend documentation in `docs/` for any language, CLI, or user-facing changes
6. Execute `/simplify` to reduce code duplication and quality issues
   - If the simplify command finds issues that are out of scope, then **ask** the user whether or not to address them. If they say yes, then address them and include the changes in the PR. If they say no, then ignore them and move on.
7. In the summary, include:
   - Performance impact assessment (if any)
   - Risk assessment
   - Code coverage results
