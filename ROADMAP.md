# Endo Shell - Roadmap

## Executive Summary

Endo Shell is a modern interactive shell designed as a companion to Contour Terminal. It prioritizes
first-class user experience, IDE-like features, and modern terminal capabilities while maintaining
reasonable compatibility with Bash syntax.

This roadmap outlines the development path from current state to a feature-complete 1.0 release.
**Note:** This document describes priorities and dependencies, not timelines. Contour Terminal remains
the primary project; Endo development follows as resources permit.

## Vision

- **Modern UX**: Rich text editing, mouse support, LSP-like completions, and syntax highlighting
- **AI-Powered**: Natural language command generation with provider abstraction (local LLMs and cloud APIs)
- **Cross-Platform**: First-class Windows support alongside Linux/macOS
- **Developer-Friendly**: Debug Adapter Protocol (DAP) integration for script debugging

---

## Current Status

### Language Specification

The Endo language specification (`docs/language/`) is complete and defines a hybrid shell language
combining F# functional programming with bash shell scripting. Key features include:

- **F# style bindings**: `let x = 42`, `let mut counter = 0`, `let add x y = x + y`
- **Type inference**: Types are automatically deduced; annotations optional
- **Pattern matching**: Full `match` expressions with guards and destructuring
- **Dual pipelines**: `|>` for function composition, `|` for process pipes
- **Records and unions**: Algebraic data types for structured data
- **Result/Option types**: Functional error handling with `?` propagation

The specification serves as the design document for Phase 1.8 implementation.

### Fully Implemented

| Component | Status |
|-----------|--------|
| Lexer with shell syntax tokens (incl. `..` as shell identifier) | ✅ |
| Parser (if/while, pipes, commands, redirects) | ✅ |
| AST with visitor pattern | ✅ |
| IR generation to CoreVM bytecode | ✅ |
| Process execution (fork/exec) | ✅ |
| Multi-process pipes | ✅ |
| Builtins: `exit`, `true`, `false`, `read` (-p/-r/-s/-n/-t/-d, IFS splitting), `cd` (incl. `cd -`), `set`, `unset`, `export`, `bind`, `echo`, `which`, `cat`, `sleep`, `fetch`, `rm` (-r/-R/-f/-d/-v/-i/--recursive/--force/--dir/--verbose/--help/--) | ✅ |
| Environment variables (set/get/export) | ✅ |
| Variable substitution (`$VAR`, `${VAR}`, `$?`, `$$`, `$!`, `$0-$9`) | ✅ |
| String interpolation in double-quoted strings (`"hello $USER"`) | ✅ |
| Command substitution (`$(cmd)`, `` `cmd` ``) | ✅ |
| Process substitution (`<(cmd)`, `>(cmd)`) | ✅ |
| Logical operators (`&&`, `||`) | ✅ |
| Redirects (`>`, `>>`, `<`, `2>&1`, `<<<`) | ✅ |
| If-then-else-elif expressions (else optional, multi-expression branches) | ✅ |
| While-do-end statements | ✅ |
| For-in loops (`for var in list do ... end`) | ✅ |
| Break and continue statements | ✅ |
| Return statement | ✅ |
| TTY abstraction with raw mode | ✅ |
| Platform abstraction layer (Pipe, Process, TTY) | ✅ |
| Grapheme cluster support for Unicode | ✅ |
| Tilde expansion (`~`, `~user`) | ✅ |
| Brace expansion (`{a,b,c}`, `{1..10}`) | ✅ |
| Parameter expansion (`${var:-default}`, `${#var}`, etc.) | ✅ |
| Arithmetic expansion (`$((expr))`) | ✅ |
| Pathname expansion (globbing) `*`, `?`, `[...]`, `**` | ✅ |
| Job management (`&`, `jobs`, `fg`, `bg`, `wait`) | ✅ |
| Crash handler (SEGV/ABRT/BUS/ILL/FPE → backtrace log in `~/.local/state/endo/crash/`, C++23 `<stacktrace>` with fallback) | ✅ |

### Not Yet Implemented

See milestone breakdown below.

---

## Milestone 0: Foundation Solidification

**Priority:** Critical
**Rationale:** These foundational improvements enable all subsequent milestones and are required for
Windows support.

### 0.1 UTF-8 Support Completion in Lexer ✅

**Status:** Complete

**Tasks:**
- [x] Implement proper UTF-8 codepoint consumption in `consumeNumber()`
- [x] Implement proper UTF-8 handling in `consumeIdentifier()`
- [x] Implement proper UTF-8 handling in string literal parsing
- [x] Add tests for Unicode identifiers and string content

### 0.2 Platform Abstraction Layer ✅

**Status:** Complete (Windows stubs in place; full implementation deferred to Milestone 4)

**Tasks:**
- [x] Design platform abstraction interface for process management
- [x] Design platform abstraction interface for file descriptors and pipes
- [x] Design platform abstraction interface for TTY/console operations
- [x] Implement Linux/POSIX backend
- [x] Add CMake configuration for platform-specific compilation
- [x] Create Windows stubs (WindowsPipe, WindowsTTY, WindowsProcess)
- [x] Consolidate into `endo-platform` static library (`endo::platform` namespace)
  - [x] `PlatformError` enum replaces platform-related `ShellError` entries
  - [x] Moved Process, Pipe, EnvironmentProvider, ProcessProvider, FileInfoProvider, SignalHandler
  - [x] `SignalCallback` interface decouples SignalHandler from Shell
  - [x] Mock classes for full test isolation (MockProcessManager, MockPipe, MockProcessProvider, MockFileInfoProvider)
  - [x] 16 test cases (60 assertions) in `test-endo-platform`
  - [x] Removed forwarding headers and orphaned source files from `src/shell/`
- [ ] Implement Windows backend (ConPTY, CreateProcess) → Deferred to Milestone 4

### 0.3 Error Handling Modernization ✅

**Status:** Complete

**Tasks:**
- [x] Audit existing error handling
- [x] Introduce `std::expected` for recoverable errors
- [x] Create error type hierarchy for shell errors
- [x] Add structured error reporting with context (line/column, suggestions)

### 0.4 Code Deduplication ✅

**Status:** Complete

**Tasks:**
- [x] Deduplicate `findCommonPrefix` between `shell::Completer` and `tui::Completer` (delegate to tui)
- [x] Remove unused `lsp::containsPosition` (duplicate of `endo::containsPosition` in HoverInfo.hpp)

### 0.5 CoreVM TODO Cleanup ✅

**Status:** Complete

**Tasks:**
- [x] Remove obsolete `// TODO: revive stack/imm opcodes` in TargetCodeGenerator (no such opcodes exist)
- [x] Remove obsolete `// TODO emitInstr(Opcode::RLOAD, ...)` in TargetCodeGenerator (ILOAD via makeRegExp is the chosen design)
- [x] Fix `Cidr` and `IPAddress` hash functions to properly hash all bytes (was only hashing first 4 bytes, wrong for IPv6)
- [x] Replace misleading TODO in `Instruction.cpp` CALL stack impact with explanatory comment
- [x] Remove obsolete `// TODO _unresolvedSymbols.push_back(...)` in Program.cpp (field doesn't exist)

---

## Milestone 1: Core Language Features

**Priority:** High
**Rationale:** Completes the shell language to be practically useful for daily work.

### Phase 1.1: Variable System ✅

**Status:** Complete (local variable scope deferred until function support in Phase 1.6)

**Dependency:** None

**Tasks:**
- [x] Implement `$VAR` substitution in commands
- [x] Implement `${VAR}` extended substitution syntax
- [ ] Implement local variable scope (within functions) → Deferred to Phase 1.6
- [x] Implement global variable scope
- [x] Implement `unset` builtin
- [x] Add tests for variable scoping rules
- [x] Implement special variables: `$?`, `$$`, `$!`, `$0-$9`

### Phase 1.2: Redirects and File Descriptors ✅

**Status:** Complete

**Dependency:** Phase 1.1 (variables may appear in redirect targets)

**Tasks:**
- [x] Implement output redirect `>` and `>>`
- [x] Implement input redirect `<`
- [x] Implement file descriptor duplication `2>&1`
- [x] Implement here-documents `<<EOF` (parsing complete, content reading deferred)
- [x] Implement here-strings `<<<`
- [x] Integrate redirects with builtin commands (not just external processes)
- [x] Add comprehensive redirect tests

### Phase 1.3: Logical Operators ✅

**Status:** Complete

**Dependency:** None

**Tasks:**
- [x] Implement `&&` (AND) operator
- [x] Implement `||` (OR) operator
- [x] Implement proper short-circuit evaluation
- [x] Add operator precedence tests

### Phase 1.4: Command and Process Substitution ✅

**Status:** Complete

**Dependency:** Phase 1.2 (redirects), Phase 1.1 (variables)

**Tasks:**
- [x] Implement command substitution `$(command)`
- [x] Implement backtick substitution `` `command` ``
- [x] Implement process substitution `<(command)` (read)
- [x] Implement process substitution `>(command)` (write)
- [x] Handle nested substitutions
- [x] Add substitution tests

### Phase 1.5: Expansions ✅

**Status:** Complete

**Dependency:** Phase 1.1 (variables), Phase 1.4 (substitution for arithmetic)

**Tasks:**
- [x] Implement tilde expansion `~`, `~user`
- [x] Implement brace expansion `{a,b,c}`, `{1..10}`
- [x] Implement parameter expansion `${var:-default}`, `${var:+alt}`, `${#var}`, etc.
- [x] Implement arithmetic expansion `$((expr))`
- [x] Implement pathname expansion (globbing) `*`, `?`, `[...]`
- [x] Implement extended globbing `**` (recursive)
- [x] Define and document expansion order
- [x] Add expansion tests

**Implementation Notes:**
- Brace expansion handled at parse time; parameter expansion supports length, defaults, prefix/suffix removal, replacement
- Pathname expansion: cross-platform via `<filesystem>`, supports `*`, `?`, `[...]`, `**` recursive globbing

### Phase 1.6: Control Flow Completion ✅

**Status:** Complete (except `select` - deferred; C-style for loop requires arithmetic assignment)

**Dependency:** Phase 1.1 (variables for loop iteration)

**Tasks:**
- [x] Implement `for var in list do ... end`
- [x] Implement `for ((init; cond; step)) do ... end` → Deferred: requires arithmetic assignment expressions
- [x] Match expression replaces bash `case...esac` (`match x with | pattern -> ...`)
- [x] Function definitions via `let name args = ...` (replaced bash `function name() { }`)
- [x] Implement `return` statement for functions
- [x] Implement `break` and `continue` for loops
- [x] Add control flow tests

**Implementation Notes:**
- Functions support positional parameters and return values affecting `$?`; C-style for loops and `select` deferred

### Phase 1.7: Job Management ✅

**Status:** Complete

**Dependency:** Platform abstraction (Milestone 0.2)

**Tasks:**
- [x] Implement background execution `&`
- [x] Implement `jobs` builtin
- [x] Implement `fg` builtin
- [x] Implement `bg` builtin
- [x] Implement `Ctrl+Z` suspend handling (external SIGTSTP/SIGCONT)
- [x] Implement job status notifications
- [x] Handle process groups correctly
- [x] Remember exit codes from all pipeline processes
- [x] Add job management tests

**Implementation Notes:**
- Uses `signalfd` on Linux for race-free SIGCHLD/SIGTSTP/SIGCONT handling; falls back to traditional handlers on macOS
- Process groups properly managed; Ctrl+Z at prompt is undo (TUI), Ctrl+Z with foreground job suspends it

### Phase 1.8: F# Style Syntax Extensions

**Status:** Specification Complete, Implementation ~95% Complete

**Dependency:** Milestone 1 core language features

**Rationale:** Endo aims to be a modern shell that combines bash convenience with F# functional
programming ergonomics. This phase adds F#-inspired syntax for variable bindings, functions,
pattern matching, and pipelines while maintaining full backward compatibility with existing
bash-style syntax.

**Specification:** See `docs/language/` for the complete language specification including:
- `let` bindings with type inference (immutable by default, `let mut` for mutable)
- Curried functions with partial application (`let add x y = x + y`)
- Lambda expressions (`fun x -> x * 2`)
- Pattern matching with guards (`match x with | pattern when guard -> result`)
- Discriminated unions and records
- F# style lists (`[1; 2; 3]`) with comprehensions
- Forward pipe operator (`|>`) alongside shell pipe (`|`)
- Result and Option types for error handling
- Error propagation with `?` operator

**Project Structure Reorganization (Completed):**
The codebase has been reorganized for better separation of concerns:

```
src/
├── endo-language/        # Core language library (CMake target: endo)
│   ├── Lexer.hpp/cpp     # Tokenization
│   ├── Parser.hpp/cpp    # Parsing
│   ├── AST.hpp           # AST node definitions
│   ├── Visitor.hpp       # AST visitor interface
│   ├── ASTPrinter.hpp/cpp
│   ├── IRGenerator.hpp/cpp
│   ├── DiagnosticsAdapter.hpp/cpp
│   ├── LogCategories.hpp/cpp
│   ├── LogConfig.hpp
│   └── ScopedLogger.hpp
├── shell/                # Shell runtime (CMake target: endo-shell)
│   ├── Shell.hpp/cpp     # Shell orchestration
│   ├── Prompt.hpp/cpp    # Interactive prompt
│   ├── Environment.hpp   # Environment variables
│   └── ... (builtins, job control, completion, etc.)
├── tui/                  # Terminal UI library (CMake target: tui)
└── CoreVM/               # Virtual machine (CMake target: CoreVM)
```

**Tasks:**
- [x] Complete language specification (`docs/language/`)
- [x] Reorganize project structure (separate `endo-language` library from `shell`)
- [x] Add new tokens to Lexer (`let`, `mut`, `fun`, `match`, `with`, `when`, `type`, `of`, `rec`, `and`, `as`, `->`, `<-`, `|>`, `Some`, `None`, `Ok`)
- [x] Implement type system foundation (`Type.hpp`, `TypeEnv.hpp`, `Unification.hpp` - TypeInference deferred until AST extensions)
- [x] Add Pattern AST nodes (`Pattern.hpp` with PatternVisitor, utility functions, and factory functions)
- [x] Extend Parser for `let` bindings and function definitions
- [x] Add F# expression AST nodes (`BinaryExpr`, `UnaryExpr`, `PipelineExpr`, `ApplicationExpr`, `IdentifierExpr`, `IntLiteralExpr`, `FloatLiteralExpr`, `BoolLiteralExpr`, `ParenExpr`)
- [x] Implement F# expression parser with operator precedence (`|>`, `||`, `&&`, comparisons, `+/-`, `*/%`, `**`, unary, application)
- [x] Add `|>` forward pipe operator to expression parsing
- [x] Add comprehensive tests for F# let bindings and expressions (39 test cases with 214 assertions)
- [x] Add ASTPrinter support for new F# nodes
- [x] Add IRGenerator stubs for F# nodes (placeholder implementations for future work)
- [x] Extend Parser for lambda expressions (`fun x -> x * 2`)
- [x] Add LambdaExpr AST node with ASTPrinter and IRGenerator stub
- [x] Add comprehensive lambda tests (8 test cases covering single/multiple params, nesting, pipelines)
- [x] Extend Parser for match expressions with guards
- [x] Extend Parser for list literals and ranges
- [x] Extend Parser for list comprehensions (`[for x in items -> expr]`)
- [x] Extend Parser for record literals and type definitions
- [x] Implement pattern matching compilation in IR generator
- [x] Add AST nodes for Option/Result types (`OptionExpr`, `ResultExpr`) and error propagation (`TryExpr`, `TryWithExpr`)
- [x] Extend Parser for Option/Result constructors (`Some`, `None`, `Ok`, `Error`)
- [x] Extend Parser for `?` postfix operator and `try expr with | pattern -> handler` expressions
- [x] Add IR generation stubs for Option/Result/Try expressions (runtime support requires CoreVM sum types)
- [x] Complete runtime support for Option/Result types in CoreVM (sum type representation) — Complete (GC deferred)
  - [x] Type descriptor infrastructure, typed objects with refcounting, runtime config
  - [x] VM opcodes for object operations (OALLOC, ORETAIN, ORELEASE, OGETTAG, OSETTAG, OGETSLOT, OSETSLOT, OTYPEID, OISTYPE)
  - [x] IR instructions, TargetCodeGenerator visitors, IRBuilder methods, Runner execution
  - [x] TypeRegistry integrated into ConstantPool; IRGenerator emits object instructions for Option/Result
  - [x] PatternIRGenerator handles constructor patterns (Some/None/Ok/Error)
  - [x] Scope-based reference counting (ORELEASE on scope exit)
  - [x] Source location infrastructure for runtime error reporting (sparse location tables, `RuntimeError`, `runWithResult()`)
  - [x] Runtime checks: division by zero, invalid type ID, null object dereference, slot bounds
  - [ ] Mark-and-sweep GC for cycle collection — Deferred

- [x] Complete `?` operator runtime implementation (unwrap or propagate)
  - [x] IRGenerator emits tag check and early return for `?` operator
  - [x] Full integration with function context for error propagation
  - [x] `FSharpFunctionContext` pushed only for functions returning Result/Option
  - [x] Early return block and storage created for `?` operator unwrap-or-propagate
  - [x] Auto-wrapping for type-consistent return values (`ReturnKind` enum, `wrapInResultOrOption()`)
- [x] Implement `try-with` expression IR generation
  - [x] Constructor pattern matching, multiple handlers, guard expressions
  - [x] Dynamic comparison opcodes (VCMPEQ/NE/LT/LE/GT/GE) for runtime-typed values
- [x] Implement `try-finally` expression IR generation (redirects `?` operator to cleanup block, supports nesting)
- [x] Implement IR generation for F# core expressions (literals, identifiers, binary/unary ops, parentheses)
- [x] Implement IR generation for F# function definitions and application (inlining approach)
- [x] Implement IR generation for F# pipelines (`|>` operator)
- [x] Implement IR generation for F# lambda expressions (`fun x -> x * 2`)
- [x] Implement closures (capturing outer scope variables)
- [x] Implement partial application in IR generator
- [x] Implement recursion support for F# functions (`let rec` with tail-call optimization)
- [x] Implement mutual recursion (`let rec f ... and g ...`) with dispatch-loop optimization
- [x] Implement `let...in` expressions for scoped bindings (`let x = 5 in x + 10`)
- [x] Implement or-patterns in match expressions (`| 1 | 2 | 3 -> "small"`)
- [x] Implement as-patterns in match expressions (`| n as val -> ...`)
- [x] Persist F# function definitions across REPL prompts (`FSharpPersistentState`)
- [x] Implement if-then-else expressions (`IfExpr` AST node, parser, IR codegen with alloca/branch/merge)
- [x] Multi-expression if-then-else branches (F# offside rule) — `parseFSharpExprSequence()` with column-based termination, wraps in `BlockExpr`, 7 test cases
- [x] Implement mutable variable assignment (`MutAssignStmt` AST node, `<-` operator, mutability tracking via `BindingInfo`)
- [x] Implement tuple expressions (`TupleExpr` AST node, 2-/3-element tuples via TypedObject with Tuple2/Tuple3 types)
- [x] Implement tuple pattern matching (full `TuplePattern` in `PatternIRGenerator` with slot extraction and sub-pattern chaining)
- [x] Implement standard library builtins (`string_length`, `int_of_string`, `string_of_int`, `not`)
- [x] Implement `env` builtin — returns `option<str>` for environment variables (13 test cases)
- [x] Implement `rand` builtin — `rand` (no args) returns random positive integer > 0; `rand A B` returns random integer in [A, B] (6 test cases)
- [x] Support `?` operator at top-level (global) scope — exits program with code 1 on None/Error instead of requiring a function context
- [x] Remove `fst`/`snd` builtins — now user-definable via pattern matching (simplifies compiler, proves language expressiveness)
- [x] Add `ROADMAP-Language.md` for tracking F# feature implementation status
- [x] Implement float (double) primitive type — 18 opcodes, constant folding, auto-promotion, pattern matching, 17 test cases
- [x] Implement multi-line expression support in parser (match arms, try-with, if-then-else, lambda, let-in, mutual recursion)
- [x] Implement numeric base literals (hex `0xFF`, octal `0o755`, binary `0b1010`, scientific `1e10`) and comments (`#`, `//`, `(* ... *)`)
- [x] Bare top-level F# function calls (`f 42` dispatches to F# parser when `f` is a known function; F# definitions shadow shell commands)
- [x] Implement type annotations for variables, function parameters, and return types
  - [x] Variable, parameter, lambda, and return type annotations with mixed annotated/bare params
  - [x] Static type validation at IR generation; annotations persist across REPL sessions
  - [x] 32 test cases covering execution, type mismatches, parser structure, and ASTPrinter output
- [x] Improve arity enforcement error messages and test coverage
  - [x] Fix grammar: "expects 1 argument" (singular) vs "expects 2 arguments" (plural) for both direct calls and pipelines
  - [x] 8 comprehensive arity enforcement tests: over-application (5 failure cases), exact arity (3 success cases)
- [x] Update syntax highlighting for new constructs (Phase 2.4)
- [x] F#-style interpolated strings: `$"Hello, {name}"` — lexer/parser/AST/IRGenerator/ASTPrinter, escaped braces, 17 test cases
- [x] Update completion for F# style (Phase 2.3) — `LetBindingCompleter` with signature display, smart-case/fuzzy matching, 8 test cases
- [x] Phase 1 Foundation Completions (no new runtime types needed)
  - [x] Unit type `()` — `UnitExpr` AST node, parser recognition in `parseFSharpPrimary`, codegen as `get(0)`
  - [x] String repetition `"ha" * 3` — detects `Mul` with string operand in `visit(BinaryExpr)`, calls `string_repeat` native callback
  - [x] Block scopes `{ let x = 1; x + 2 }` — `BlockExpr` AST node, `parseBlockExpr()`, codegen with `pushFSharpScope`/`popFSharpScope`
  - [x] Function composition `>>` and `<<` — `parseFSharpComposition()` precedence level between pipeline and or, desugars to lambda at parser level
  - [x] Tuple destructuring in `let` — `destructurePattern` field on `LetBindingStmt`/`LetInExpr`, uses `PatternIRGenerator` with pre-allocated binding storage
- [x] Phase 2 list runtime with cons-cell linked lists
  - [x] IR generation for list literals, cons (`::`) operator, concat (`@`) operator, list ranges, and comprehensions
  - [x] Runtime callbacks: `list_head`, `list_tail`, `list_length`, `list_concat`, `list_reverse`, `list_to_string`, `list_map`, `list_filter`, `list_fold`
  - [x] Pattern matching for lists: empty list `[]`, cons pattern `h :: t`, fixed-length patterns `[a; b; c]`
  - [x] List printing via `list_to_string` with recursive formatting
  - [x] 89 list test cases covering literals, operations, pattern matching, nested structures, and runtime functions
- [x] Support standalone `match` expression as a statement
- [x] Transition from AST inlining to closure-based function calls — Complete
  - [x] Phase 0: Extract function call methods from `visit(ApplicationExpr)` into named helpers (`generateFSharpCall`, `generateRecursiveCall`, `generateMutualRecursiveCall`, `generatePartialApplication`)
  - [x] Phase 1: Add UCALL/URET/UTCALL opcodes and frame pointer to VM Runner
    - [x] `_fp` (frame pointer), `CallFrame`, `_callStack` for proper function call isolation
    - [x] LOAD/STORE/STACKROT made FP-relative for function-local stack access
    - [x] `_fp=0` initialization maintains backward compatibility with existing shell/main function code
  - [x] Phase 2: Add `FunctionCallInstr`/`FunctionRetInstr`/`TailCallInstr` IR instructions
    - [x] New instruction classes, IRBuilder methods, TargetCodeGenerator visitors
    - [x] `parameterCount` property on `IRFunction` for parameter alloca skip logic
  - [x] Phase 3: Compile non-recursive F# functions as separate IRFunctions
    - [x] `compileFunctionBody()` creates IRFunction, parameter allocas, codegens body, emits FunctionRet
    - [x] Functions with ALL parameters type-annotated compile as functions (UCALL/URET)
    - [x] Functions without annotations fall back to AST inlining (backward compatible)
    - [x] Error recovery: removes malformed function and truncates report on compilation failure
    - [x] Return type propagated through `FunctionCallInstr` for correct `convertToString` dispatch
    - [x] 15 function-specific tests (typed arithmetic, float, string, bool, if-then-else, fallback)
  - [x] Phase 4: Closures (captured variables as extra arguments)
    - [x] Deterministic capture ordering (sorted alphabetically) stored in `captureOrder`
    - [x] Captures become extra parameters prepended before explicit params in function
    - [x] Call site loads captured values and prepends to args for `FunctionCallInstr`
    - [x] Capture types derived from source alloca types (no annotation needed for captures)
    - [x] 8 closure-specific tests (int, multiple captures, string, float, nested lets, thunk, fallback)
  - [x] Phase 5: Recursive functions (unified tail calls via UTCALL)
    - [x] `compileFunctionBody` now compiles recursive functions (removed `!isRecursive` guard)
    - [x] `compiledFunction` pre-set before body codegen so recursive references emit UCALL/UTCALL
    - [x] Tail position tracking (`_inTailPosition`, `_compilingFunction`) for UCALL vs UTCALL decisions
    - [x] Tail position propagation: IfExpr (condition=false, branches=inherit), BinaryExpr (operands=false), ApplicationExpr (args=false, call=inherit), MatchExpr (scrutinee=false, arms=inherit), LetInExpr (value=false, body=inherit)
    - [x] Recursive capture loads use function scope (not outer scope) to avoid cross-function alloca references
    - [x] Null-result handling for tail calls in IfExpr, MatchExpr (check `_compilingFunction`)
    - [x] Old loop-based TCO and dispatch-loop remain as fallback for untyped recursive functions
    - [x] 5 recursive function tests (countdown, factorial, sum, multiple calls, capture)
  - [x] Phase 6: Cleanup and REPL function persistence
    - [x] Removed `_useClosureCalls` feature flag — function compilation is always attempted
    - [x] REPL-persisted functions now re-compute captures and compile as functions at prompt start
    - [x] Closure captures from previous prompts now work correctly (persisted values in scope)
    - [x] 3 session function tests (recursive, closure, multiple calls)
    - [x] Old AST inlining paths retained as fallback for functions without type annotations or with non-primitive inferred types
  - [x] Hindley-Milner type inference (Algorithm W) as separate pre-pass
    - [x] `TypeInferencer` class runs before IR generation, producing `InferenceResult` map
    - [x] Full Algorithm W: literals, identifiers, binary/unary ops, if-then-else, lambda, application, pipeline, let-in, match, block, list, cons, concat, range, tuple, option, result, try, try-with, try-finally, list comprehension, shell expressions
    - [x] Pattern inference: literal, variable, wildcard, tuple, list, cons, constructor, as, or, guarded, record patterns
    - [x] Statement inference: compound, expr, let binding (function, destructuring, simple), mutable assignment
    - [x] Ad-hoc overloading: `+` with string/float detection, comparison ops, logical ops
    - [x] Recursive and mutual recursive function inference
    - [x] Let-polymorphism via `generalize`/`instantiate`
    - [x] Integration: primitive types (int, float, bool, str, unit) applied to function parameters, enabling function compilation without explicit annotations
    - [x] Complex types (list, option, result, function, tuple) inferred but not applied (function compilation limitations)
    - [x] Extended `createStandardTypeEnv()` with print, println, string_length, int_of_string, string_of_int, not, ::, @, ~-
    - [x] 20 type inference tests (12 unit + 8 e2e) covering literals, operators, recursion, partial annotations, lambda, option, if-branches, factorial, identity, add, subtract, multiply, comparison, let-in, bool functions, typed-still-works, mixed annotations
  - [x] Higher-order functions (passing functions as arguments)
    - [x] `functionRefs` map in `FSharpScope` tracks which variables hold function references
    - [x] `capturedFunctionRefs` on `FSharpFunction` preserves function reference info through closures and partial application
    - [x] `lookupFSharpFunctionRef()` walks scope chain for function reference resolution
    - [x] Fallback in `visit(ApplicationExpr)` and `visit(PipelineExpr)`: when `lookupFSharpFunction` fails, resolves via `lookupFSharpFunctionRef`
    - [x] Functions with function-typed parameters skip function compilation (forced to AST inlining for functionRefs tracking)
    - [x] `IdentifierExpr` returns constant function name for variables with function reference mappings
    - [x] 11 HOF tests: basic, lambda arg, twice, compose, partial application, closure capture, multiple function args, pipeline, string function, nested partial application, function alias
  - [x] Record types (Phase 4)
    - [x] `Token::Dot` and `Token::With` in Lexer for field access and record update syntax
    - [x] `.` added to `FSharpReservedSymbols` to break identifiers at dots in F# mode
    - [x] AST nodes: `RecordTypeDefStmt`, `RecordExpr`, `RecordUpdateExpr`, `FieldAccessExpr` with Visitor support
    - [x] Parser: `parseTypeDefinition()` for `type Name = { field: type; ... }`, record/block disambiguation via lookahead (`Identifier =` → record, `let` → block)
    - [x] Parser: field access in `parseFSharpPostfix()` via `Token::Dot` loop, record update via `Token::With` keyword
    - [x] Parser: record pattern matching `{ name; age }` and `{ name = n; _ }` in `parseRecordPattern()`
    - [x] Parser: record destructuring in `let { name; age } = expr` via `Token::BraceOpen` in `parseLet()`
    - [x] IR codegen: `RecordTypeDefStmt` registers custom product type in TypeRegistry with field→slot mappings
    - [x] IR codegen: `RecordExpr` resolves type by name or field names, OALLOC + OSETSLOT per field
    - [x] IR codegen: `RecordUpdateExpr` copies all slots from base, overwrites updated fields
    - [x] IR codegen: `FieldAccessExpr` looks up field slot offset from TypeDescriptor, OGETSLOT
    - [x] IR codegen: type annotation propagation for records through function parameters (`generateFSharpCall`)
    - [x] `PatternIRGenerator`: `RecordPattern` extracts fields by slot offset from `_recordFieldOffsets` map
    - [x] Runtime printing: `object_to_string` formats records as `{ field1 = val1; field2 = val2 }`
    - [x] `LiteralType type` field added to `FieldInfo` in `TypeDescriptor.hpp` for runtime field type dispatch (string, bool, int)
    - [x] PatternIRGenerator fix: always reload scrutinee from storage in RecordPattern to prevent dead temporaries across block boundaries
    - [x] 6 parser tests + 22 IR generation tests covering type defs, field access, updates, pattern matching, functions, destructuring, and printing
  - [x] Discriminated unions / ADTs (Phase 5)
    - [x] AST nodes: `UnionVariantDef`, `UnionTypeDefStmt`, `UnionConstructorExpr` with Visitor support
    - [x] Parser: `parseTypeDefinition()` extended for `type Shape = | Circle of float | Rectangle of float * float | Point` syntax
    - [x] Parser: union constructor expressions in `parseFSharpPrimary()` via `_constructorLookup` map
    - [x] Parser: union constructor patterns in `parsePrimaryPattern()` for match expressions
    - [x] Parser: multi-slot payloads (`of int * int`) parsed with `*` separator, flattened from tuple arguments
    - [x] `CustomSumType` struct on `IRProgram` (parallel to `CustomProductType` for records)
    - [x] `TypeRegistry::registerSumType(unique_ptr)` overload for pre-assigned type IDs
    - [x] `TargetCodeGenerator`: registers custom sum types before code generation
    - [x] IR codegen: `UnionTypeDefStmt` allocates type ID, builds `VariantInfo`, registers constructors
    - [x] IR codegen: `UnionConstructorExpr` emits OALLOC + OSETTAG + OSETSLOT per payload slot (chained)
    - [x] `PatternIRGenerator`: `ConstructorPattern` extended with `_constructorLookup` for user-defined tag matching
    - [x] `PatternIRGenerator`: multi-slot payload extraction with separate `createLoad` per slot (loop-safe)
    - [x] `PatternIRGenerator`: `collectBindings` traverses payload even for unknown constructors (pre-lookup phase)
    - [x] 3 parser tests + 10 IR generation tests covering enums, single/multi-slot payloads, wildcards, functions, and coexistence with Option/Result
  - [x] Variadic parameters and shell aliases (Phase 7)
    - [x] `Token::Ellipsis` (`...`) lexer token for variadic/splat syntax
    - [x] `TypedParameter::isVariadic` flag and `SplatExpr` AST node with Visitor support
    - [x] Parser: `...name` in typed parameters creates variadic param, `...name` in shell args creates `SplatExpr`
    - [x] Parser: `& cmd` at statement level (`Token::Ampersand` case in `parseStmt`) for shell-first execution
    - [x] IR codegen: variadic arguments collected into List (Cons cells) at call site in `visit(ApplicationExpr)`
    - [x] IR codegen: `SplatExpr` generates while loop iterating list, emitting `cmd_arg` per element
    - [x] IR codegen: context-aware `ShellCommandExpr` — capture mode (expression context) vs normal I/O (statement context) via `_shellCommandCaptureMode`
    - [x] IR codegen: `ApplicationExpr` restores capture mode for argument evaluation (expression context)
    - [x] 9 test cases covering variadic params, shell aliases with splat, capture mode, IR generation
  - [x] Fix: `let export mut` re-exports on mutation
    - [x] `BindingInfo.isExported` flag tracks exported variables through scope chain
    - [x] `emitExportVariable()` helper emits load → convertToString → export callback IR
    - [x] `visit(MutAssignStmt)` and `visit(MutAssignExpr)` re-export after store when `isExported`
    - [x] Test runtime registers `export(SS)V` callback updating `mockEnv` map
    - [x] 5 new tests: initial export, mutation re-export, multiple mutations, immutable export, string mutation

**Implementation Notes:**
- See `docs/language/implementation-notes.md` for detailed parser implementation notes
- See `ROADMAP-Language.md` for F# feature implementation status
- **Loop syntax**: `for ... do ... end` / `while ... do ... end` (replaced `done` with `end`)
- **Bare range expressions**: `1..10` and `1..2..10` work as standalone expressions (not only inside `[...]`); `..` precedence between comparisons and arithmetic
- **Dual semantics**: `let` unambiguously starts F# style; `|>` (function pipe) and `|` (shell pipe) are distinct tokens; expression context captures output, statement context prints to terminal
- **Type inference**: Hindley-Milner Algorithm W pre-pass (`TypeInferencer`) integrated into `IRGenerator::generate()`, enabling function compilation without explicit annotations for primitive types
- **Function compilation**: Functions with all typed parameters compile as separate IRFunctions (UCALL/URET/UTCALL); untyped functions fall back to AST inlining
- **Records**: Compiled to TypedObject product types (OALLOC/OSETSLOT/OGETSLOT); update expressions use alloca-protected storage
- **Discriminated unions**: Compiled to TypedObject sum types (OALLOC/OSETTAG/OSETSLOT/OGETTAG/OGETSLOT)
- **Closures**: Captures sorted alphabetically, prepended as extra function parameters; REPL-persisted functions re-compute captures at prompt start
- **Mutual recursion**: Dispatch-loop optimization with integer tag selecting function body; separate from self-recursion's single-function loop
- **Lists**: Cons-cell linked list via TypedObject; comprehension codegen uses two-phase approach (forward iteration + reverse pass)
- **Shell command expressions**: `& command` temporarily leaves F# mode, reuses `SubstitutionExpr` logic for output capture; statement-level `& cmd` runs with normal I/O (no capture)
- **Variadic parameters**: `...args` collects extra arguments into a List at call site; `SplatExpr` in shell commands iterates list emitting `cmd_arg` per element; enables alias-style function definitions (`let ll ...args = & exa -l ...args`)

**See also:** `ROADMAP-StructuredData.md` for Milestone 6 covering structured data
and system interaction (object pipelines, structured commands, data manipulation verbs).
Phase 6.1 (StructuredCommand interface, platform abstraction) and Phase 6.2 (`ps` builtin) are implemented.
HOF list element type annotation propagation fixed for `find`, `reverse`, `take`, `drop`, `sortBy` — enables chained record pipelines.

---

## Milestone 2: Terminal UX

**Priority:** High
**Rationale:** Differentiates Endo from other shells; delivers on the "IDE-like" promise.

### Phase 2.1: Rich Text Editor Foundation

**Status:** Complete

**Dependency:** Milestone 1 complete (need full language for practical editing)

**Implementation Summary:** A comprehensive TUI library has been integrated into the project (`src/tui/`).
The library includes:
- Terminal input/output abstraction (`Terminal`, `TerminalInput`, `TerminalOutput`)
- VT sequence parser (`VtParser`) with support for CSI, SGR mouse, bracketed paste, UTF-8
- Input field with multiline editing, history, and kill ring (`InputField`)
- Various UI components (Box, Dialog, List, LogPanel, StatusBar, Spinner, Text, Theme)
- Sixel image support and Markdown rendering
- Configurable keybinding system (`KeyBindings`, `EditAction`) with modern defaults

**Tasks:**
- [x] Add GUI-style selection model (Shift+arrows, Ctrl+A select all, Ctrl+C copy, Ctrl+X cut)
- [x] Implement undo/redo history (Ctrl+Z undo, Ctrl+Y/Ctrl+Shift+Z redo)
- [x] Implement clipboard integration via OSC 52 (`TerminalOutput::copyToClipboard()` + callback)
- [x] Add mouse click-to-position cursor support (`InputField::setCursorFromClick()`)
- [x] Rewrite `Prompt` class to use `tui::Terminal` + `tui::InputField`
- [x] Remove `InputEditor` dependency from Shell (superseded by TUI library)
- [x] Implement fixed editor region that auto-grows up to 50% of terminal height
- [x] Add multiline editing support with proper rendering and selection highlighting
- [x] Add comprehensive editor unit tests (47 tests covering basic editing, cursor movement, selection, undo/redo, multiline, history, kill ring, clipboard, and UTF-8)
- [x] Implement configurable keybinding framework (`EditAction`, `KeyChord`, `KeyBindings`)
- [x] Partial-line indicator on command completion (fish-style reverse-video `⏎` when command output doesn't end with newline)

**Implementation Notes:**
- Multiline editing uses Alt+Enter or Shift+Enter to insert newlines (Enter submits)
- Editor region scrolls to keep cursor visible when content exceeds max height
- Selection highlighting uses inverse video (SGR 7/27)
- Display width calculation uses libunicode for proper Unicode handling
- Keybinding system maps key chords to edit actions, enabling future vi mode support
- Default keybindings use modern conventions: Ctrl+C=copy, Ctrl+Y=redo, Ctrl+D=delete char (EOF on empty), Ctrl+T=agent mode
- Ctrl+T seamlessly toggles between shell and agent prompts: the agent prompt replaces the shell prompt in-place (and vice versa) with no extra text, vertical gap, or visual jank
- Shift+movement keys extend selection; Ctrl+D is context-sensitive (EOF vs delete)
- Kitty keyboard protocol support: Full handling of Kitty's CSIu escape sequences including:
  - CapsLock and NumLock modifiers (bits 6-7) for proper capitalization with CapsLock active
  - All special keycodes in Private Use Area (57344-63743): lock keys, F13-F35, keypad, media keys, modifier keys
  - CapsLock XOR Shift behavior: either one (but not both) capitalizes letters, matching standard keyboard behavior
  - Colon-separated key subparameters (key:shifted_key:base_layout_key) per Kitty spec
  - Shifted key consumption: when shifted_key is present with Shift modifier, uses shifted codepoint and strips Shift (e.g., Shift+3 → '#' with no Shift)
  - Flag 4 (report alternate keys) now requested alongside flags 1+8 for proper shifted symbol handling
- `bind` builtin command allows runtime keybinding management:
  - `bind` - List all keybindings
  - `bind <key> <action>` - Bind a key to an action (e.g., `bind ctrl+y yank`)
  - `bind -r <key>` - Remove a keybinding
  - `bind --reset` - Reset to defaults
  - `bind --help` - Show available actions and key format

### Phase 2.2: Mouse Integration

**Status:** Complete

**Dependency:** Phase 2.1

**Tasks:**
- [x] Implement passive mouse tracking VT extension support (DEC mode 2029)
- [x] Implement click-to-position cursor
- [x] Implement click-and-drag selection
- [x] Implement double-click word selection (fish-style word boundaries)
- [x] Implement triple-click line selection
- [x] Add mouse interaction tests

**Implementation Notes:**
- Uses Contour's passive mouse tracking (DEC mode 2029) which includes SGR format and uiHandled hint
- Word selection uses fish-style boundaries: path separators (`/`) and punctuation break words
- Events with `uiHandled=true` are skipped (terminal UI consumed them, e.g., for scrollback)
- Scroll wheel scrolls multiline editor content
- 14 new mouse-related tests added to InputField_test.cpp

### Phase 2.3: Completion and Suggestions

**Status:** Complete

**Dependency:** Phase 2.1, Milestone 1 (need language features to complete)

**Notes:**
- Completion system uses an abstraction layer (`CompletionProvider` interface) to allow both local and AI-powered completion providers
- Initial implementation provides local completion based on the current command line context
- Fish-style ghost text suggestions appear dimmed after the cursor
- Tab or Ctrl+Space triggers completion menu; Right arrow, End, or Ctrl+E accepts ghost text

**Tasks:**
- [x] Design completion provider interface (`CompletionProvider`, `CompletionItem`, `CompletionContext`)
- [x] Implement context analysis (`CompletionContext.cpp` - uses Lexer to determine context type)
- [x] Implement command name completion (`CommandCompleter.cpp` - builtins + PATH scanning with caching)
- [x] Implement file path completion (`FileCompleter.cpp` - with tilde expansion)
- [x] Implement variable name completion (`VariableCompleter.cpp` - env vars + special vars)
- [x] Implement option/flag completion stub (`OptionCompleter.cpp` - placeholder for future --help parsing)
- [x] Implement history-based suggestions (`HistoryCompleter.cpp` - prefix matching with recency scoring)
- [x] Implement history abstraction (`History` interface, `InMemoryHistory` implementation)
- [x] Implement persistent shell history (`PersistentHistory` - YAML-based disk persistence with frequency tracking, auto-import from fish/zsh/bash, atomic flush, failed command un-persistence on failure, link error checking — 18 tests)
- [x] Fix invalid commands (program-not-found) persisting to history — set `_exitCode` in all command execution error paths (`builtinCallProcess`, `builtinCallProcessShellPiped`, `builtinCmdExec`, `builtinCmdExecPiped`, `builtinCmdExecPipedBackground`)
- [x] Implement completer orchestrator (`Completer.cpp` - coordinates providers, generates suggestions)
- [x] Add ghost text support to InputField (`setGhostText()`, `acceptGhostText()`, auto-clear on modification)
- [x] Add completion styles to Theme (`ghostText`, `completionItem`, `completionSelected`, `completionDesc`)
- [x] Design and implement completion popup UI (`CompletionPopup.cpp` - bordered list with scroll indicators)
- [x] Integrate completion with Prompt (Tab/Ctrl+Space triggers, menu navigation, ghost text rendering)
- [x] Fix inline history cycling (Up/Down with prefix) to use `History::search()` directly instead of `Completer::complete()` — fixes cycling for file paths, arguments, and options
- [x] Add comprehensive completion tests (`Completer_test.cpp`, `CompletionPopup_test.cpp` - 35 tests)
- [x] Implement F# dot-access completion (`FSharpCompleter.cpp` — Option module methods, `_.field` record fields, `value.method`/`value.field` — 24 tests)
- [x] Extend ghost text to all completion contexts (`Completer::suggest()` — two-phase matching: Phase 1 full-line prefix from Command-capable providers for history, Phase 2 word-level fallback from context-appropriate providers for variables, file paths, arguments — 8 tests)
- [x] Implement git branch name completion (`GitBranchCompleter.cpp` — completes branch names for `checkout`, `switch`, `merge`, `rebase`, `push` (after remote), `branch -d`/`-m`, etc. — parses `fullInput` for subcommand context, queries `git branch` for local/remote branches — 16 tests)
- [x] Replace `GitBranchCompleter` with generic `CommandSpecCompleter` framework — data-driven command completion with `CommandSpec` definitions, `CommandQueryProvider` interface for dynamic data, `QueryCache` (2s TTL) for caching, and `CommandLineParser` for structured parsing. Git as first target: 49 subcommand definitions (Tier 1: full options/args, Tier 2: key options, Tier 3: name+description), dynamic queries for branches, tags, remotes, stashes, status files, config keys, aliases, recent commits. Extensible to any command (docker, cargo, npm, etc.) via `registerCommand()`. — 29 tests

**Implementation Notes:**
- Core completion types (`CompletionItem`, `CompletionProvider`, `Completer`, `SmartCaseMatch`, `FuzzyMatch`) in `src/tui/completer/` as pure TUI model
- Shell-specific providers reorganized into `src/shell/CompletionProviders/` subdirectory for cleaner structure
- Smart case matching: lowercase patterns match case-insensitively; patterns with uppercase match case-sensitively (like Vim's smartcase)
- Fuzzy matching: Typing `ds` matches `Downloads` and `Documents` (matches non-contiguous characters `d...s`); prefix matches scored higher than fuzzy; fuzzy matches have highlighted match positions in completion menu (using `completionMatch` theme style)
- Score bonuses via `SmartCaseConfig`: exact matches get +50, case-exact prefixes get +25 (configurable)
- Score bonuses via `FuzzyConfig`: prefix matches get +50 bonus over fuzzy; quality threshold 20% minimum
- `CompletionContextType` enum: Command, Argument, FilePath, Variable, VariableBrace, Redirect, Option, Unknown
- Ghost text uses SGR 2 (dim) for visual distinction from actual input
- `CompletionPopup` is a proper TUI widget with `show()`/`hide()`/`updateItems()` visibility management, `processEvent()` returning `CompletionAction` enum (Changed, Accepted, Dismissed), and `render()` using relative cursor positioning
- Ctrl+R triggers fuzzy history search popup — shows all history entries (newest first), dynamically re-filters as user types, Enter replaces entire input with selected entry, Escape dismisses without changing input
- Modifier-only keys (bare Ctrl/Alt/Shift via Kitty keyboard protocol) no longer dismiss the completion popup
- Unhandled keys cause popup to dismiss and pass through to parent (removed `None` action)
- Visibility state is properly synced between `CompletionPopup` and `Component` base class
- Dynamic filtering: typing while popup is visible filters the list in real-time; `updateItems()` preserves selection when the selected item still matches, otherwise selects best match; auto-closes on 0 matches
- Popup positioning: In inline mode (primary screen), always renders below cursor - Screen creates space by emitting newlines to use scrollback buffer; in fullscreen/fixed mode, renders above cursor when not enough space below (< 3 rows)
- Tab with multiple completions sharing a common prefix inserts the longest common prefix (LCP) before cycling — matches standard shell behavior (bash, zsh, fish)
- Completion menu appears below cursor with Up/Down/Ctrl+J/Ctrl+K/Tab/Shift+Tab navigation, Enter to accept, Escape to dismiss
- Single completion matches are inserted directly without showing menu
- `Environment` class extracted to `Environment.hpp` for cleaner dependency management
- Shell class creates `Completer` with environment and history, connects to Prompt via `setCompleter()`
- Executed commands are added to both prompt history (Up/Down recall) and completion history (suggestions)
- Test utilities in `src/tui/TestHelpers.hpp` for rendering verification (`canvasToString()`, `renderPopup()`, etc.)
- 44 completion-related tests covering Completer, CompletionPopup, items accessor, LCP integration, and updateItems functionality
- F# dot-access completion (`FSharpCompleter.cpp`): `Option.map`/`Option.bind`/`Option.defaultValue` module methods, `_.field` record field placeholders (from `FSharpPersistentState::recordTypeFields`), and generic `value.method`/`value.field` access — 24 tests
- Ghost text two-phase matching: Phase 1 queries Command-capable providers (History, Command, LetBinding, FSharp) with full-line prefix matching in reverse priority order (history preferred); Phase 2 falls back to word-level prefix matching from all context-appropriate providers via `gatherCompletions()`. Enables ghost text for variables (`$PA` → `TH`), file paths, arguments, and history recall in any position
- Generic command completion framework (`CommandSpecCompleter`): Data-driven specs define subcommands, options (with value kinds), and positional args (with dynamic query tags). `CommandLineParser` parses input into `CommandLineState` (phase detection: Subcommand/Option/OptionValue/Argument). `QueryCache` wraps `CommandQueryProvider` with 2s TTL. `CommandSpecCompleter` dispatches to appropriate completer method based on phase. Git implementation: `createGitSpec()` defines 49 subcommands across 3 tiers, `GitQueryProvider` resolves 11 query tags (branches, tags, remotes, stashes, status-files, tracked-files, config-keys, aliases, recent-commits). Architecture: `CommandSpec.hpp` (data model) → `CommandQueryProvider.hpp` (interface) → `QueryCache.hpp/cpp` → `CommandLineParser.hpp/cpp` → `CommandSpecCompleter.hpp/cpp` → `GitSpec.hpp/cpp`. Extensible: new commands (docker, cargo) require only spec + optional query provider + one `registerCommand()` call.
- [x] Add cmake/ctest `CommandSpec` with preset name completion (`CmakeSpec.hpp/cpp`): `createCmakeSpec()` defines cmake global options (`--preset`, `--build`, `-G`, `-S`, `-B`, `-D`, `-j`, `--config`, `--target`, `--clean-first`, `--verbose`), `createCtestSpec()` defines ctest options (`--preset`, `-j`, `-C`, `--test-dir`, `--output-on-failure`, `--stop-on-failure`, `-R`, `-E`, `-L`, `--verbose`, `--timeout`, `--repeat`, `--rerun-failed`). `CmakeQueryProvider` reads `CMakePresets.json` and `CMakeUserPresets.json` from the current directory, recursively resolving `"include"` arrays (relative paths, cycle-protected via canonical path tracking) and filtering presets by platform `"condition"` objects (equals, notEquals, inList, notInList, not, anyOf, allOf with `${hostSystemName}` variable resolution). Inherited conditions via `"inherits"` chains are transitively evaluated — a preset inheriting from a hidden base with a Windows condition is correctly filtered out on Linux. Extracts non-hidden preset names from all preset arrays (configure/build/test/package/workflow), deduplicates with user presets taking priority, returns sorted results with `displayName` as description. `CommandLineParser` enhanced to detect option values being typed mid-word (e.g., `cmake --preset cl<TAB>` correctly enters `OptionValue` phase). `CompletionProvider::isExclusiveFor()` virtual method added — when a provider returns exclusive results (e.g., DynamicQuery/Enum option values), lower-priority providers like `FileCompleter` are suppressed, preventing directory entries from appearing alongside preset names. — 19 tests
- [x] Add ssh/scp `CommandSpec` with host name completion (`SshSpec.hpp/cpp`): `createSshSpec()` defines ssh global options (`-p`, `-i`, `-l`, `-L`, `-R`, `-D`, `-J` (DynamicQuery for hosts), `-F`, `-o`, `-N`, `-T`, `-t`, `-v`, `-X`, `-A`, `-C`, `-q`, `-4`, `-6`) and a DynamicQuery positional arg for `[user@]hostname`. `createScpSpec()` defines scp options (`-P`, `-i`, `-F`, `-o`, `-r`, `-v`, `-C`, `-q`, `-4`, `-6`, `-3`) with repeatable host positional arg. `SshQueryProvider` parses `~/.ssh/config` line by line, extracting `Host` aliases (multi-host lines split into separate entries), attaching `HostName` values as descriptions, skipping wildcard patterns (`*`, `?`). Recursively follows `Include` directives (relative to `~/.ssh/` or absolute, with `~` expansion), protected against cycles via canonical path tracking. Directives are matched case-insensitively per SSH spec. — 14 tests
- [x] Fix `CompletionContextAnalyzer::analyze()` misclassifying slash-containing argument prefixes (e.g., `origin/master`) as `FilePath` instead of `Argument`. Moved `looksLikeFilePath()` check from before tokenization to after — only applies in command position (first token / after command boundary), not in argument position where `FileCompleter` already handles file paths via the `Argument` type. `CommandSpecCompleter` now properly resolves `git rebase origin/m<TAB>` → `origin/master`. — `CompletionContext_test.cpp` with 14 tests

### Phase 2.3.5: TUI Renderer Architecture

**Status:** Complete

**Dependency:** Phase 2.1

**Rationale:** The original TUI rendering used direct terminal output without central coordination,
causing issues like `CompletionPopup::hide()` not clearing the screen. The new architecture provides
buffer-based immediate-mode rendering with diff-based terminal updates, proper component hierarchy,
and focus group management.

**Tasks:**
- [x] Create geometry types (`Rect`, `Point`, `Size` in `Rect.hpp`)
- [x] Create cell buffer (`Cell`, `Buffer` types)
- [x] Create drawing context (`Canvas` - bounded view into buffer)
- [x] Create component base class (`Component` with event bubbling, focus, hierarchy)
- [x] Create screen coordinator (`Screen` - manages component tree, rendering, events)
- [x] Implement viewport modes (Fullscreen, Inline with terminal scrolling, Fixed)
- [x] Implement focus groups for multi-focus support (shell + AI overlay)
- [x] Add render mode option (`RenderMode::Diff` vs `RenderMode::Full` for benchmarking)
- [x] Migrate `InputField` to `Component` (inherits from Component, implements render(Canvas&), onEvent())
- [x] Migrate `CompletionPopup` to `Component` (inherits from Component, implements render(Canvas&), onEvent())
- [x] Migrate `List` to `Component` (inherits from Component, implements render(Canvas&), onEvent())
- [x] Migrate `SelectDialog`, `ConfirmDialog`, `InputDialog` to `Component`
- [x] Migrate `StatusBar` to `Component`
- [x] Note: `Box` remains as a utility class (Canvas already has drawBox() method)
- [x] Update `Prompt` to use `Screen` coordinator (Phase 10)
  - Created `PromptComponent` class with styled prompt rendering (left bar, background, colors)
  - Integrated `Screen` with `Inline` viewport mode in `Prompt`
  - Uses Canvas-based rendering through the component tree
- [x] Remove old TerminalOutput-based rendering code (Phase 11)
  - Removed deprecated `render(TerminalOutput&, ...)` methods from all widget headers and implementations
  - Removed unused helper methods (`renderBackground`, `calculateBounds`)
  - Updated includes to use `Theme.hpp` instead of `TerminalOutput.hpp` in widget headers
- [x] Add comprehensive unit tests for new rendering system (Phase 12)
  - Added 65 tests in `Renderer_test.cpp` covering:
    - `Point`: construction, equality, offset
    - `Size`: construction, empty, area
    - `Rect`: construction, factory methods, accessors, contains, intersects, offset, inset, expand, intersect, unite
    - `Cell`: equality, reset, continuation, wide character detection
    - `Buffer`: construction, resize, cell access, putString, fill, clear, cursor state
    - `Canvas`: coordinate translation, clipping, drawing operations, subcanvas

**Implementation Notes:**
- Buffer-based immediate mode: Components render to a `Buffer` via `Canvas`, then `Screen` diffs and flushes
- Component hierarchy: Parent-child relationships with z-index ordering for overlays
- Event bubbling: Events propagate from target up through ancestors until handled
- Focus groups: Multiple independent focus contexts (e.g., "prompt", "ai-chat", "overlay")
- Inline viewport: Shell renders at cursor position, grows downward by emitting newlines
- Widget lifecycle: Persistent tree with explicit `addChild()`/`removeChild()`
- `InputField::Model`: Nested class pattern separates logic from rendering, enables unit testing
- Synchronized output: `Screen::flush()` uses DEC mode 2026 (`SyncGuard`) to batch terminal updates and prevent visual tearing. First inline render skips `SyncGuard` to avoid SIGCHLD-interrupted writes leaving the terminal in sync-buffer mode.
- EINTR-safe I/O: All TUI `::write()` and `::read()` syscalls use `safeWrite()`/`safeRead()` helpers (`PosixIO.hpp`) that retry on `EINTR`. Prevents silent failures when background `popen()` children exit and send `SIGCHLD` during terminal I/O.
- Inline cursor management: `flushInline()` properly handles content height changes - moves up by `newLines` (not `contentHeight`) after emitting newlines, and clears excess rows when content shrinks to avoid visual artifacts
- Kitty unscroll extension (WIP): Infrastructure added for `CSI Ps + T` to restore scrollback content. Detected via XTVERSION query. Supported terminals: Kitty, Contour, mintty. Configurable via `UnscrollMode::Auto|Enabled|Disabled`. Currently disabled for inline mode because the sequence shifts the entire screen, which doesn't work well when rendering at the bottom. Needs scroll region approach for proper implementation.
- Unit tests for cursor movement calculations in `Screen_test.cpp` (13 test cases)

**Architecture:**
```
Screen (coordinator)
  ├── owns Buffer (current + previous for diff)
  ├── owns RootComponent (implicit container)
  ├── manages focus groups
  └── dispatches events with bubbling

Canvas (drawing context)
  └── bounded view into Buffer with coordinate translation

Component (base class)
  ├── render(Canvas&) - pure virtual
  ├── onEvent(InputEvent&) - with EventResult for bubbling
  ├── parent/children hierarchy
  ├── visibility, area, z-index
  └── focus group membership
```

### Phase 2.4: Syntax Highlighting

**Status:** Partially Complete (lexer-based token highlighting)

**Dependency:** Phase 2.1

**Notes:**
- Shared `TokenClassification.hpp` in `endo-language` provides `TokenCategory` enum and `classifyTokenCategory()` — used by both the shell prompt and the LSP
- `SyntaxHighlighter` in the shell tokenizes input and produces a per-byte `HighlightMap`
- `PromptComponent::render()` uses the highlight map for segment-based rendering with per-token colors
- Dark theme palette: keywords (purple), numbers (orange), strings (green), operators (cyan), variables (red), constructors (yellow), punctuation (gray)
- LSP `SemanticTokens.cpp` refactored to delegate to shared `classifyTokenCategory()`

**Tasks:**
- [x] Design syntax highlighting architecture (shared TokenClassification, SyntaxHighlighter, PromptComponent integration)
- [x] Implement real-time tokenization (lexer-based per-byte highlight map)
- [x] Integrate with prompt rendering (segment-based renderer with selection support)
- [x] Add highlighting tests (13 test cases covering keywords, numbers, strings, operators, constructors, punctuation)
- [x] Refactor LSP SemanticTokens to use shared classification
- [x] Extract shared hover logic from LSP into `endo-language` (`HoverProvider.hpp/cpp`, `HoverInfo.hpp`, `StubRuntime.hpp`)
- [x] Extract shared diagnostics collector from LSP into `endo-language` (`DiagnosticsCollector.hpp/cpp`)
- [x] Implement parse error underlines in prompt (curly red underlines via extended `UnderlineStyle` enum and SGR 4:3/58;2;r;g;b)
- [x] Implement command-not-found diagnostic (unknown shell commands underlined with error severity)
  - [x] `ProgramCall` AST node stores `programLocation` for precise error ranges
  - [x] `collectDiagnostics()` walks AST to validate commands against builtins, PATH, and defined functions
  - [x] LSP publishes `command not found` errors for unknown commands in `.endo` files
  - [x] Skips explicit paths (`./script`, `/usr/bin/ls`), builtins, shell/F# function definitions
  - [x] Suppresses false positives for F# names persisted from prior REPL prompts
  - [x] Fix StubRuntime missing shell builtin registrations (crash on `which`/`exit`/`bind` in diagnostics)
  - [x] Fix `bind` signature mismatch in Parser (`"bind(S+)I"` → `"bind(s)I"`)
  - [x] 18 unit tests + 2 E2E LSP tests
- [ ] Implement semantic highlighting (valid vs invalid commands)
- [ ] Implement configurable color schemes

### Phase 2.5: Tooltips and Help

**Status:** Partially Complete (hover tooltips for commands)

**Dependency:** Phase 2.2 (mouse), Phase 2.3 (completion data), Phase 5.3 (LSP as shared backend)

**Tasks:**
- [x] Implement mouse-hover tooltip display
- [x] Implement command path tooltips (show full path for executables in $PATH)
- [x] Implement builtin command tooltips (show "shell builtin" for builtins)
- [x] Implement "command not found" tooltips
- [x] Implement symbol hover for any token (keywords, constructors, operators, builtins, user-defined bindings)
  via shared `endo::computeHover()` — shows LSP-style markdown hover information
- [x] Implement parse error hover (hovering over error-underlined region shows error message)
- [x] Implement priority hover chain: diagnostics → language hover → command resolver
- [x] Consume LSP hover/diagnostics capabilities via in-process API for consistent behavior
  between the interactive shell and external editors
- [ ] Implement alias expansion tooltips (variadic let bindings serve as aliases; show expansion preview)
- [ ] Implement inline help for commands
- [ ] Implement error tooltips with suggestions
- [ ] Integrate with man pages for command help

**Implementation Notes:**
- `HoverState` class tracks mouse position with 500ms delay before showing tooltip
- `Tooltip` component supports both plain text and markdown content with scrolling
- `StyledText` class provides styled text rendering with markdown parsing (reuses parsing from `MarkdownRenderer`)
- `CommandResolver` determines command type (external, builtin, alias, not found) and provides tooltip text
- Screen overlay system used for tooltip positioning
- Hover is a first-class Component-level feature: `Component::onHover(x, y)` virtual method returns
  `std::optional<HoverResult>` (text, position, contentType). Screen wires hover callbacks internally
  in its constructor, translating viewport coordinates to component-relative and calling `onHover()`.
- `PromptComponent` overrides `onHover()` with the priority chain: diagnostics → language hover → command resolver
- Tooltip automatically hides when user starts typing (via `Screen::dispatchKeyEvent`); all events
  (not just mouse) are routed through `Screen::dispatchEvent()` so key presses auto-hide tooltips
  without PromptComponent needing any tooltip-awareness
- `onHover()` returns the position of the hovered element (not where the tooltip should appear);
  `showTooltip()` adds the +1 row offset for "below cursor" placement
- Inline mode coordinate tracking handles content shifts caused by tooltip/popup rendering:
  - `_mainContentHeight` tracks main content height before overlays for accurate mouse hit-testing
  - `_peakContentHeight` tracks maximum allocated space to avoid redundant newline emission
  - `_totalNewlinesEmitted` tracks cumulative shift for correct coordinate translation
  - `_inlineContentStartRow` recalculated each frame based on terminal size and newlines emitted
  - Ctrl+L (clear screen) calls `releaseCursor()` to reset coordinate tracking and re-query cursor position

### Phase 2.6: Customizable Prompt

**Dependency:** Phase 2.4 (highlighting for prompt elements)

**Tasks:**
- [x] Design prompt configuration format (`PromptConfig` with layout, separator, transient, modules)
- [x] Implement prompt segment system (`PromptModule` interface, `PromptSegments` data model)
- [x] Implement common segments (path, git, exit status, duration, hostname, clock, battery, toolchain, F# mode, structured output, indicator)
- [x] Implement layout engine (`PromptLayoutEngine` with SingleLine, TwoLine, Boxed, Powerline)
- [x] Implement 10 prompt presets (minimal-arrow, lambda-clean, opencode-bar, powerline, transient, dashboard, boxed-module, gradient-glow, context-adaptive, endo-signature)
- [x] Implement truecolor gradient support for path module (`Gradient.hpp/.cpp`)
- [x] Extend theme system with `PromptColorPalette` and `SyntaxHighlightPalette`
- [x] Make syntax highlighter theme-aware (`categoryColor(cat, theme)`)
- [x] Implement dark/light mode detection via VT CSI ? 2031 h / CSI ? 997 n
- [x] Implement color scheme change notification and auto-switching
- [x] Implement prompt builtin functions (`set_prompt_preset`, `set_prompt_indicator`, `set_prompt_layout`, `set_prompt_separator`, `set_prompt_transient`, `set_prompt_duration_threshold`, `set_prompt_spacing`)
- [x] Add `set_prompt_*` builtins to LSP/completion with parameter value auto-completion (preset names, layout/separator/transient enum values)
- [x] Fix quoted-string completion for `set_prompt_*` builtins (unterminated quote handling in context analyzer, `BuiltinArgumentCompleter` shell provider)
- [x] Add hover documentation for `set_prompt_*` builtin functions
- [x] Implement `~/.config/endo/init.endo` auto-execution
- [x] Fix endo-signature prompt rendering (rounded separators ╭─/╰─, dim │ between modules, gradient path, structured output command filter)
- [x] Implement auto-refresh for live prompt modules (clock 1s, battery 30s) via `refreshInterval()` virtual method
- [x] Implement transient prompt rendering (replace full prompt with compact indicator on submit)
- [x] Add syntax highlighting to transient prompt (reuses `computeHighlightMap` / `categoryColor` from `SyntaxHighlighter`)
- [x] Implement aurora background gradient for endo-signature preset (multi-stop horizontal color interpolation via `multiStopGradient()`)
- [x] Implement sixel aurora fade effect above prompt (pixel-level gradient via `Canvas::drawImage()`, CSI 16t cell size query, pre-encoded sixel caching in `PromptComponent`; transparent alpha for terminal background bleed-through)
- [ ] Implement VT420 host-writable status line integration
- [ ] Support OSC-8 hyperlinks in prompts
- [ ] Add prompt configuration tests
- [x] Implement OSC 133 shell integration (prompt start/end, command start/finished markers)
- [x] Implement OSC 7 current working directory propagation to terminal

---

## Milestone 3: AI Integration

**Priority:** High
**Rationale:** Enables natural language interaction and intelligent assistance.

### Phase 3.1: Provider Abstraction

**Dependency:** None (can develop in parallel with Milestone 1)

**Tasks:**
- [ ] Design AI provider interface
- [ ] Implement local LLM backend (llama.cpp, ollama)
- [ ] Implement Claude API backend
- [ ] Implement Gemini API backend
- [ ] Implement OpenAI API backend
- [ ] Implement provider configuration and selection
- [ ] Handle API keys securely
- [ ] Add provider abstraction tests

### Phase 3.2: Natural Language Commands

**Dependency:** Phase 3.1, Milestone 1 (need full language to generate)

**Tasks:**
- [ ] Implement natural language to shell command translation
- [ ] Implement command explanation ("what does this do?")
- [ ] Implement command suggestion based on intent
- [ ] Design safe execution confirmation flow
- [ ] Add NL command tests

### Phase 3.3: Intelligent Assistance

**Dependency:** Phase 3.1, Phase 2.3 (completion)

**Tasks:**
- [ ] Implement AI-powered command completion
- [ ] Implement error recovery suggestions
- [ ] Implement context-aware help
- [ ] Implement learning from user corrections
- [ ] Add assistance tests

### Phase 3.4: Context Awareness

**Dependency:** Phase 3.1, Milestone 1

**Tasks:**
- [ ] Implement working directory context
- [ ] Implement command history context
- [ ] Implement project detection (git, package.json, etc.)
- [ ] Implement environment-aware suggestions
- [ ] Add context tests

### Phase 3.5: Agent Plan Mode ✅

**Dependency:** Phase 3.1

**Tasks:**
- [x] Plan data model (`Plan.hpp`): `PlanStepStatus`, `PlanStep`, `Plan` structs
- [x] `SubmitPlanTool`: pseudo-tool for LLM to submit structured plans during exploration
- [x] `ToolRegistry::definitions(ToolFilter)`: filtered tool definitions overload
- [x] `AgentSession::processMessageForPlan()`: exploration loop with read-only tools
- [x] `PlanExecutor`: step-by-step execution driver with skip/fail support
- [x] Plan rendering in `AgentResponseRenderer`: `renderPlan()` and `renderPlanProgress()`
- [x] `PlanModeConfig` in `AgentConfig` with YAML persistence
- [x] Shell `/plan` command: exploration → review → y/n/r → execution loop
- [x] Comprehensive test coverage (SubmitPlanTool, PlanExecutor, AgentSession plan mode, ToolRegistry filter)

### Phase 3.6: Agent Tool Use Logging ✅

**Dependency:** Phase 3.1

**Tasks:**
- [x] `ToolStatusCallback` enhanced to pass full `ToolCall const&` (name + arguments)
- [x] `AgentConfig::logToolUses` flag with YAML persistence (`log_tool_uses` key, default: true)
- [x] `formatToolCallArgs()` helper: compact JSON with truncated strings and content redaction
- [x] Tool use lines rendered inline during agent thinking phase (`│ ⚙ tool_name {args}`)
- [x] `ScopedAssign` RAII guard for `activeRenderer` tracking across renderer lifetimes

### Phase 3.7: Agent Context Caching ✅

**Dependency:** Phase 3.1

**Tasks:**
- [x] Cache `ProjectContext` (file tree, rules, memory) as `Shell` member keyed by `cwd`
- [x] Reuse cached context on agent mode re-entry when `cwd` unchanged (skips file scanning)
- [x] Git branch and status always queried fresh (may change between sessions)
- [x] Cache invalidated automatically on `cwd` change

### Phase 3.8: Agent Memory Persistence ✅

**Dependency:** Phase 3.7

**Tasks:**
- [x] `SaveMemoryTool` writes memory files to `~/.config/endo/agent-memory/{filename}.md`
- [x] Auto-create memory directory if it does not exist
- [x] Path traversal protection (reject filenames with `/` or `\`)
- [x] Cache invalidation callback: saving memory clears cached `ProjectContext`
- [x] Tool registered in agent mode (normal mode only, not plan mode)
- [x] Unit tests for save, overwrite, directory creation, and error cases

### Phase 3.9: Agent Slash Command System ✅

**Dependency:** Phase 3.5

**Tasks:**
- [x] `SlashCommand` interface with `name()`, `description()`, `execute()` returning `SlashCommandResult` variant (`PromptRewrite`, `PlanModeRequest`, `DirectOutput`)
- [x] `SlashCommandRegistry` for dynamic command registration with `registerCommand()`, `findCommand()`, `commands()`
- [x] Built-in `/help` (lists all commands) and `/plan` (enters plan mode) commands
- [x] `CallbackSlashCommand` convenience class for lambda-based dynamic registration (skills/plugins)
- [x] `SlashCommandCompleter` implementing `tui::CompletionProvider` with smart-case prefix and fuzzy matching
- [x] `AgentInputComponent` enhanced with `tui::CompletionPopup` + `tui::Completer` for slash command completion
- [x] `Shell::runAgentMode()` refactored: registry-based dispatch replaces hardcoded `/plan` check
- [x] Unknown command error message: `Unknown command: /xyz`
- [x] 23 unit tests (60 assertions) covering registry, commands, completer, dynamic registration, fuzzy matching
- [x] Persistent plan mode with Shift+Tab cycling between plan/execute sub-modes
  - [x] `CycleAgentMode` edit action with Shift+Tab default keybinding
  - [x] `AgentInputComponent` mode badge in header (plan bright, execute dim)
  - [x] Project path in agent header: `branch @ ~/path` (git repo) or `~/path` (non-git)
  - [x] `/plan` (no args) idempotently enters plan mode
  - [x] Normal messages route through `processMessageForPlan()` when plan mode active
  - [x] Plan-result handling extracted to lambda (shared by `/plan <task>` and persistent mode)

### Phase 3.10: Agent Tool Improvements ✅

**Dependency:** Phase 3.1

**Tasks:**
- [x] `shell_execute` rewritten to use `fork/exec` with explicit `bash` (fallback to `/bin/sh`)
  - [x] Proper timeout support via `poll()` polling loop with `SIGKILL` on timeout
  - [x] Command passed as single `execl` argument — no shell expansion quoting issues
  - [x] Child process unblocks inherited signal mask (SIGCHLD/SIGTSTP/SIGCONT/SIGINT) and resets handlers to SIG_DFL
  - [x] Parent poll/read loop handles EINTR from signal interrupts (e.g., SIGWINCH)
- [x] `EndoExecuteTool` (`endo_execute`): evaluates endo source code directly and returns captured output
  - [x] Callback captures stdout/stderr via `tmpfile()` + `dup2()` redirection
  - [x] Excluded from plan mode's allowed tools (can execute shell commands with side effects)
  - [x] Unit tests with mock callbacks (7 test cases, 18 assertions)
- [x] Fix: post-tool-call LLM responses now streamed to user (removed erroneous `streamCb = nullptr` after first iteration)

### Phase 3.11: Explore Tool (Sub-Agent) ✅

**Dependency:** Phase 3.1, Phase 3.5

**Tasks:**
- [x] `ExploreConfig` in `AgentConfig` with `max_turns` YAML setting (default: 10)
- [x] `ExploreTool`: isolated sub-agent with read-only tools (`read_file`, `glob`, `grep`, `git`)
  - [x] Spawns temporary `AgentSession` with local `ToolRegistry` — no write tools, no `explore` recursion
  - [x] Conversation history discarded after execution — only concise answer returned to outer context
  - [x] Input schema: `{ question: string (required), scope?: string }`
  - [x] System prompt with exploration-focused instructions built from shared `ProjectContext`
  - [x] Deferred system prompt injection via `setSystemPrompt()` (async context loading compatible)
- [x] Shell integration: tool registered in `runAgentMode()`, explore prompt set alongside main prompt
- [x] Comprehensive tests (13 test cases, 40 assertions): schema, error handling, tool isolation, provider errors

### Phase 3.12: Agent Conversation Persistence & Ghost Text

- [x] `ConversationHistoryStore`: JSON persistence for agent conversation history
  - [x] Atomic write (`.tmp` + rename), version-stamped format, system prompt exclusion
  - [x] Load on agent mode startup, save after each exchange
  - [x] Comprehensive tests (12 test cases): round-trip all block types, base64 image encoding, corrupt JSON, missing version, atomic write verification
- [x] `AgentHistoryProvider`: completion provider for previous user queries
  - [x] Prefix and fuzzy matching with recency scoring (most recent = highest)
  - [x] Deduplication, slash command exclusion, smart case matching
  - [x] Comprehensive tests (11 test cases): prefix/fuzzy matching, recency ordering, deduplication, edge cases
- [x] Ghost text support in `AgentInputComponent`
  - [x] Debounced ghost text updates (100ms), suggest cache for performance
  - [x] Tab accepts ghost text, Right/End at end of line accepts ghost text
  - [x] Clear ghost text on text change, Escape, Submit, Abort
- [x] `/reset` slash command: clears in-memory history, removes persisted file, resets provider entries
- [x] `AgentSession::loadPersistedMessages()` for history restoration
- [x] Up/Down history navigation fed from persisted queries via `InputField::addHistory()`
- [x] Event loop poll timeout respects ghost text debounce for responsive updates

---

## Milestone 4: Windows Support

**Priority:** High (must-have for 1.0)
**Rationale:** Required for broad adoption; must be developed in parallel, not as an afterthought.

### Phase 4.1: Build System

**Dependency:** Milestone 0.2 (platform abstraction design)

**Tasks:**
- [x] Static linking support (`ENABLE_STATIC_LINKING` option, `cmake/StaticLinking.cmake`)
  - [x] System requirement check (glibc-static, libstdc++-static detection with distro-specific hints)
  - [x] CPM-built dependencies when static: yaml-cpp, libunicode, CURL with mbedTLS backend
  - [x] Guard `-rdynamic` when static (incompatible with `-static`)
  - [x] CI workflow for static build artifact (`static-build.yml`)
- [ ] Add Windows CMake preset
- [ ] Configure MSVC and Clang-cl support
- [ ] Set up Windows CI pipeline
- [ ] Handle Windows-specific dependencies
- [x] Fix CoreVM `jump_to` macro for switch-based VM dispatch loop (Windows/MSVC): `break` inside `do { ... } while(0)` wrapper exits the do-while instead of the switch, causing fall-through to the next case handler and VM stack corruption
- [x] Fix `isInPath()` PATH separator for Windows: use `;` instead of `:`, probe `.exe`/`.cmd`/`.bat` extensions, skip POSIX `owner_exec` permission check

### Phase 4.2: Platform Implementation

**Dependency:** Phase 4.1, Milestone 0.2 (abstraction interface ✅)

**Current State:** Platform abstraction interfaces complete with stub implementations
(`WindowsPipe.cpp`, `WindowsTTY.cpp`, `WindowsProcess.cpp`). Stubs return `NotImplemented` errors.

**Design Decision:** Endo prefers forward slashes (`/`) as path separators on all platforms, including Windows.
Windows APIs accept forward slashes, and this consistency simplifies auto-completion, path manipulation,
and user muscle memory. Backslashes remain valid in user input but are normalized internally.

**Tasks:**
- [ ] Implement CreateProcess-based execution (replace `WindowsProcess.cpp` stubs)
- [ ] Implement Windows pipe handling (replace `WindowsPipe.cpp` stubs)
- [ ] Implement ConPTY integration for terminal (replace `WindowsTTY.cpp` stubs)
- [ ] Implement Windows console input handling
- [ ] Handle Windows path separators (prefer forward slashes `/` over backslashes `\`)
- [ ] Handle drive letters in paths (e.g., `C:/Users/...`)
- [ ] Implement PATHEXT handling for executables
- [ ] Add Windows-specific tests

### Phase 4.3: Windows-Specific Features

**Dependency:** Phase 4.2

**Tasks:**
- [ ] Implement PowerShell interoperability
- [ ] Implement CMD compatibility mode (optional)
- [ ] Handle Windows environment variables (case-insensitive)
- [ ] Support UNC paths
- [ ] Add Windows feature tests

---

## Milestone 5: Developer Tools

**Priority:** Medium
**Rationale:** Enables debugging and profiling of shell scripts; differentiator for power users.

### Phase 5.0: Documentation Snippet Validation ✅

**Status:** Complete

**Dependency:** None

**Rationale:** As the language evolves, code snippets in documentation can silently break. Automated
validation catches these regressions early. The `--check` flag also enables external tools and editors
to perform syntax/semantic validation without executing code.

**Tasks:**
- [x] Add `--check` CLI flag to endo (compile without executing — early return after link)
- [x] Create `scripts/check-doc-snippets.py` to extract and validate ` ```endo ` blocks from markdown files
- [x] Support `<!-- endo-no-check -->` skip markers for illustrative/incomplete snippets
- [x] Integrate as ctest target (`check-doc-snippets` with `docs` label)
- [x] Add GitHub Actions step for CI validation

**Implementation Notes:**
- `--check` reuses `Shell::execute()` with a single `_checkOnly` early-return guard after link — zero code duplication
- Python script uses stdlib only (no pip dependencies), 10s timeout per block, project-local `tmp/` for temp files
- Currently 73 of 144 blocks fail (illustrative fragments, unimplemented features) — fixing these is tracked separately
- ctest runs with `--allow-failures` to report without blocking the build

### Phase 5.1: Debug Adapter Protocol (DAP) Server

**Dependency:** Milestone 1 (complete language), Phase 1.6 (functions)

**Tasks:**
- [ ] Implement DAP server protocol handling, asseccsible via CLI `endo --dap`
- [ ] Implement breakpoint support
- [ ] Implement step execution (into, over, out)
- [ ] Implement variable inspection
- [ ] Implement call stack display
- [ ] Integrate with VS Code DAP extension
- [ ] Add DAP tests

### Phase 5.2: Profiling and Tracing

**Dependency:** Milestone 1

**Tasks:**
- [ ] Implement execution timing
- [ ] Implement command frequency analysis
- [ ] Implement trace output mode
- [ ] Implement performance bottleneck detection
- [ ] Add profiling tests

### Phase 5.3: Language Server Protocol (LSP)

**Dependency:** Milestone 1 (complete language), Phase 2.4 (syntax highlighting can share tokenizer)

**Rationale:** Provides IDE-grade editing support for Endo shell scripts in external editors
(VS Code, Neovim, Helix, Emacs, etc.). Reuses the existing lexer, parser, and AST infrastructure
to deliver rich language intelligence outside the interactive shell.

**Tasks:**
- [x] Implement LSP server transport (stdio, accessible via `endo --lsp`)
- [x] Implement `initialize`/`shutdown`/`exit` lifecycle
- [x] Implement `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didClose` synchronization
- [x] Implement `textDocument/publishDiagnostics` (syntax errors, undefined variables, unknown commands)
- [ ] Implement `textDocument/completion` (commands, file paths, variables, builtins, options)
- [x] Implement `textDocument/hover` (command help via man pages, builtin documentation, variable values)
- [x] Implement `textDocument/definition` (go-to-definition for functions and variable assignments)
- [x] Implement `textDocument/references` (find all references to a function or variable)
- [x] Implement `textDocument/signatureHelp` (parameter hints for functions)
- [x] Implement `textDocument/documentSymbol` (outline of functions, aliases, exported variables)
- [ ] Implement `textDocument/formatting` and `textDocument/rangeFormatting`
- [x] Implement `textDocument/rename` (rename function or variable across script)
- [x] Implement `textDocument/semanticTokens` (semantic highlighting: commands, builtins, variables, strings, operators)
- [ ] Implement `textDocument/codeAction` (quick fixes for common errors, e.g. missing quotes, unset variables)
- [ ] Create VS Code extension with language registration for `.endo` and `.sh` files
- [x] Add LSP server tests (protocol conformance and language feature tests)

**Implementation Notes:**
- Reuse existing `Lexer` and `Parser` for tokenization and AST construction; run in incremental mode
  for fast re-parsing on edits
- Diagnostics should include structured error context from Milestone 0.3's error hierarchy
- Completion provider should share the abstraction from Phase 2.3 where possible
- Semantic tokens map to the same token categories as Phase 2.4's syntax highlighting
- The LSP server runs as a separate process (`endo --lsp`), similar to the DAP server (`endo --dap`)
- Consider using JSON-RPC library or implementing a lightweight handler on top of `std::iostream`
- The LSP's hover, completion, and diagnostics capabilities can be consumed internally by the
  interactive shell (Phase 2.5 Tooltips, Phase 2.3 Completion) via in-process API calls, avoiding
  the need for duplicate logic between the interactive editor and external editor support

---

## Feature Dependency Graph

```
Milestone 0: Foundation
├── 0.1 UTF-8 Completion
├── 0.2 Platform Abstraction ──────────────────────────────┐
└── 0.3 Error Handling                                     │
                                                           │
Milestone 1: Core Language                                 │
├── 1.1 Variables                                          │
│   └── 1.2 Redirects                                      │
│       └── 1.4 Substitution                               │
│           └── 1.5 Expansions                             │
├── 1.3 Logical Operators                                  │
├── 1.6 Control Flow                                       │
└── 1.7 Job Management ────────────────────────────────────┤
                                                           │
Milestone 2: Terminal UX                                   │
├── 2.1 Rich Text Editor                                   │
│   ├── 2.2 Mouse Integration                              │
│   │   └── 2.5 Tooltips                                   │
│   ├── 2.3 Completion ────────────────────────────────────┤
│   └── 2.4 Syntax Highlighting                            │
│       └── 2.6 Customizable Prompt                        │
                                                           │
Milestone 3: AI Integration                                │
├── 3.1 Provider Abstraction (parallel development OK)     │
│   ├── 3.2 Natural Language Commands                      │
│   ├── 3.3 Intelligent Assistance                         │
│   └── 3.4 Context Awareness                              │
                                                           │
Milestone 4: Windows Support                               │
├── 4.1 Build System ◄─────────────────────────────────────┘
│   └── 4.2 Platform Implementation
│       └── 4.3 Windows-Specific Features

Milestone 5: Developer Tools
├── 5.1 DAP Server
├── 5.2 Profiling
└── 5.3 LSP Server ◄──── reuses Lexer/Parser/AST
    └── feeds into 2.5 Tooltips, 2.3 Completion (in-process API)
```

---

## Risk Assessment

### High Risk

| Risk | Mitigation |
|------|------------|
| Platform abstraction complexity | Start abstraction design early; test both platforms continuously |
| CoreVM limitations for shell semantics | Document limitations; consider VM extensions if needed |
| AI provider API changes | Abstract providers well; version lock API clients |

### Medium Risk

| Risk | Mitigation |
|------|------------|
| Bash compatibility gaps | Document intentional differences; provide migration guide |
| Performance with large command histories | Implement lazy loading and pagination |
| ConPTY quirks on Windows | Test on multiple Windows versions; maintain fallback |

### Low Risk

| Risk | Mitigation |
|------|------------|
| UTF-8 edge cases | Use well-tested Unicode libraries; comprehensive test suite |
| DAP protocol complexity | Reference existing implementations; incremental feature support |
| LSP protocol surface area | Implement incrementally; start with diagnostics and completion, add features progressively |

---

## Success Criteria for 1.0 Release

- [ ] All Milestone 0, 1, 2, and 4 tasks complete
- [ ] Milestone 3 Phase 3.1 and 3.2 complete (basic AI integration)
- [ ] Passes comprehensive test suite on Linux and Windows
- [x] Documentation complete (user guide, configuration reference) — MkDocs site in `docs/`
- [x] Browser playground (WASM-compiled interpreter, xterm.js terminal, endo-signature prompt)
- [ ] Performance acceptable for interactive use (< 50ms prompt latency)
- [ ] No critical or high-severity bugs

---

## Contributing

Contributions are welcome. When working on a feature:

1. Check this roadmap for dependencies - ensure prerequisites are complete
2. Create an issue referencing the roadmap task
3. Follow the coding guidelines in `CLAUDE.md`
4. Add tests for new functionality
5. Update this roadmap when tasks are completed

---

*This roadmap is a living document. Updates occur as development progresses and priorities evolve.*
