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

The Endo language specification (`LANGUAGE.md`) is complete and defines a hybrid shell language
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
| Lexer with shell syntax tokens | ✅ |
| Parser (if/while, pipes, commands, redirects) | ✅ |
| AST with visitor pattern | ✅ |
| IR generation to CoreVM bytecode | ✅ |
| Process execution (fork/exec) | ✅ |
| Multi-process pipes | ✅ |
| Builtins: `exit`, `true`, `false`, `read` (-p/-r/-s/-n/-t/-d, IFS splitting), `cd`, `set`, `unset`, `export`, `bind`, `echo`, `which`, `cat`, `sleep` | ✅ |
| Environment variables (set/get/export) | ✅ |
| Variable substitution (`$VAR`, `${VAR}`, `$?`, `$$`, `$!`, `$0-$9`) | ✅ |
| String interpolation in double-quoted strings (`"hello $USER"`) | ✅ |
| Command substitution (`$(cmd)`, `` `cmd` ``) | ✅ |
| Process substitution (`<(cmd)`, `>(cmd)`) | ✅ |
| Logical operators (`&&`, `||`) | ✅ |
| Redirects (`>`, `>>`, `<`, `2>&1`, `<<<`) | ✅ |
| If-then-else-elif-fi statements | ✅ |
| While-do-done statements | ✅ |
| For-in loops (`for var in list; do ...; done`) | ✅ |
| Case statements (`case ... esac`) | ✅ |
| Function definitions (`function name() {}`, `name() {}`) | ✅ |
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
- [ ] Implement Windows backend (ConPTY, CreateProcess) → Deferred to Milestone 4

### 0.3 Error Handling Modernization ✅

**Status:** Complete

**Tasks:**
- [x] Audit existing error handling
- [x] Introduce `std::expected` for recoverable errors
- [x] Create error type hierarchy for shell errors
- [x] Add structured error reporting with context (line/column, suggestions)

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
- String interpolation: Double-quoted strings support variable expansion (`"hello $USER"`), braced variables (`"count: ${COUNT}"`), command substitution (`"date: $(date)"`), arithmetic expansion (`"sum: $((1+2))"`), and escape sequences (`"\n"`, `"\""`). The lexer emits `DblQuoteStart`, content tokens (StringFragment, DollarName, etc.), and `DblQuoteEnd`. The parser creates a `ConcatExpr` AST node with all parts concatenated at runtime.
- Tilde expansion: `~` expands to `$HOME`, `~user` expands to user's home directory
- Brace expansion: Handled at parse time for efficiency (no runtime overhead)
- Parameter expansion: Supports length (`${#VAR}`), defaults (`${VAR:-default}`, `${VAR:=default}`, `${VAR:+alt}`, `${VAR:?error}`), prefix/suffix removal (`${VAR#pattern}`, `${VAR##pattern}`, `${VAR%pattern}`, `${VAR%%pattern}`), and replacement (`${VAR/pattern/replacement}`, `${VAR//pattern/replacement}`)
- Arithmetic expansion: Supports `+`, `-`, `*`, `/`, `%`, `**`, comparisons (`<`, `>`, `<=`, `>=`, `==`, `!=`), logical operators (`&&`, `||`, `!`), and bitwise operators
- Pathname expansion: Cross-platform implementation using `<filesystem>`, supports `*`, `?`, `[...]` bracket expressions with ranges and `**` recursive globbing

### Phase 1.6: Control Flow Completion ✅

**Status:** Complete (except `select` - deferred; C-style for loop requires arithmetic assignment)

**Dependency:** Phase 1.1 (variables for loop iteration)

**Tasks:**
- [x] Implement `for var in list; do ...; done`
- [ ] Implement `for ((init; cond; step)); do ...; done` → Deferred: requires arithmetic assignment expressions
- [x] Implement `case ... esac` pattern matching
- [ ] Implement `select` for menu generation → Deferred: requires TTY interaction
- [x] Implement function definitions `function name() { ... }` and `name() { ... }`
- [x] Implement `return` statement for functions
- [x] Implement `break` and `continue` for loops
- [x] Add control flow tests

**Implementation Notes:**
- For-list loops: Full support with break/continue, proper cleanup of nested loop state
- Case statements: Full glob-style pattern matching with multiple patterns per clause (`|`-separated)
- Functions: Supports positional parameters ($1, $2, ...) and return values affecting $?
- Functions are scoped to the current command execution (not persisted across separate execute() calls)
- C-style for loops and `select` require language features not yet implemented

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
- Background execution (`&`) spawns commands in a new process group and returns immediately
- `jobs` lists all background jobs with their state (Running, Stopped, Done)
- `fg` brings a background job to the foreground and waits for completion
- `bg` resumes a stopped job in the background
- `wait` waits for all or specific background jobs to complete
- `$!` contains the PID of the last background job
- Uses `signalfd` on Linux for race-free SIGCHLD, SIGTSTP, and SIGCONT handling
- Falls back to traditional signal handlers on macOS/BSD
- Process groups are properly managed for job control
- Foreground job control: When running a foreground command (single or pipeline), the shell creates a new process group, transfers terminal control to it, and waits with `WUNTRACED`. When Ctrl+Z is pressed, the process receives SIGTSTP, the shell detects the stopped state, adds the job to the job table, and returns control to the shell. The user can then use `fg` to resume or `bg` to continue in background.
- SIGTSTP handling for shell itself: When the shell receives SIGTSTP (e.g., from parent shell via `kill -TSTP`), it restores terminal to cooked mode, re-raises SIGTSTP with default handling to actually stop, and when resumed (SIGCONT), restores raw mode and redraws the prompt
- Note: Ctrl+Z at the prompt (when no foreground job is running) is used for undo (TUI feature)
- Job control builtins (`jobs`, `fg`, `bg`, `wait`) are recognized as parser directives with dedicated AST nodes

### Phase 1.8: F# Style Syntax Extensions

**Status:** Specification Complete, Implementation In Progress

**Dependency:** Milestone 1 core language features

**Rationale:** Endo aims to be a modern shell that combines bash convenience with F# functional
programming ergonomics. This phase adds F#-inspired syntax for variable bindings, functions,
pattern matching, and pipelines while maintaining full backward compatibility with existing
bash-style syntax.

**Specification:** See `LANGUAGE.md` for the complete language specification including:
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
- [x] Complete language specification (`LANGUAGE.md`)
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
- [ ] Extend Parser for record literals and type definitions
- [x] Implement pattern matching compilation in IR generator
- [x] Add AST nodes for Option/Result types (`OptionExpr`, `ResultExpr`) and error propagation (`TryExpr`, `TryWithExpr`)
- [x] Extend Parser for Option/Result constructors (`Some`, `None`, `Ok`, `Error`)
- [x] Extend Parser for `?` postfix operator and `try expr with | pattern -> handler` expressions
- [x] Add IR generation stubs for Option/Result/Try expressions (runtime support requires CoreVM sum types)
- [~] Complete runtime support for Option/Result types in CoreVM (sum type representation) - In Progress
  - [x] Type descriptor infrastructure (`TypeDescriptor.hpp`, `TypeRegistry.hpp/cpp`)
  - [x] Typed object structure with reference counting (`TypedObject.hpp`)
  - [x] Runtime configuration for error propagation and type checks (`RuntimeConfig.hpp`)
  - [x] Unit tests for type system (19 tests passing)
  - [x] VM opcodes for object operations (OALLOC, ORETAIN, ORELEASE, OGETTAG, OSETTAG, OGETSLOT, OSETSLOT, OTYPEID, OISTYPE)
  - [x] IR instruction classes for object operations (ObjAllocInstr, ObjRetainInstr, etc.)
  - [x] TargetCodeGenerator visitor implementations for object instructions
  - [x] IRBuilder methods for creating object instructions
  - [x] VM Runner execution of object instructions (OALLOC, ORETAIN, ORELEASE, etc.)
  - [x] TypeRegistry integrated into ConstantPool for type lookup at runtime
  - [x] IRGenerator emits object instructions for Option/Result expressions
  - [x] PatternIRGenerator handles constructor patterns (Some/None/Ok/Error)
  - [x] Scope-based reference counting (ORELEASE on scope exit)
  - [x] Comprehensive IR generation tests for Option/Result
  - [x] Source location infrastructure for runtime error reporting
    - [x] `SourceLocation` member on `Instr` class with getter/setter
    - [x] `IRBuilder::setSourceLocation()` for current location tracking
    - [x] `IRGenerator` sets location from AST nodes during code generation
    - [x] Sparse location table in `Handler` for memory-efficient storage
    - [x] `TargetCodeGenerator` builds location table during code emission
    - [x] `ConstantPool` stores location tables for handlers
    - [x] `Handler::locationOf()` binary search lookup for instruction offsets
    - [x] `RuntimeError` struct with message and location for error reporting
    - [x] `Runner::runWithResult()` returns `std::expected<bool, RuntimeError>`
    - [x] Unit tests for location propagation
  - [x] Runtime checks when `RuntimeConfig::typeChecksEnabled` is true
    - [x] Division by zero check in `NDIV` and `NREM` instructions
    - [x] Invalid type ID check in `OALLOC` instruction
    - [x] Null object dereference checks in all object operations (`ORETAIN`, `ORELEASE`, `OGETTAG`, `OSETTAG`, `OGETSLOT`, `OSETSLOT`, `OTYPEID`, `OISTYPE`)
    - [x] Slot index out of bounds checks in `OGETSLOT` and `OSETSLOT`
  - [ ] Mark-and-sweep GC for cycle collection
  - [x] Fix VM stack tracking bug with pattern matching and `?` operator execution
    - [x] `CondBrInstr` now properly discards extras before branching to ensure consistent stack state
    - [x] Uses STACKROT to move condition to correct position, then DISCARD for extras
    - [x] Block boundary handling emits DISCARD when tracking stack exceeds alloca count
    - [x] Fix DISCARD underflow: all function parameter allocas now use `createAllocaInEntryBlock()` instead of `_builder.createAlloca()`
    - [x] Fix dead `scrutinee.reload` in constructor patterns without payload (e.g., `None` arms)
    - [x] Added underflow guard in CondBrInstr visitor for defense-in-depth
- [x] Complete `?` operator runtime implementation (unwrap or propagate)
  - [x] IRGenerator emits tag check and early return for `?` operator
  - [x] Full integration with function context for error propagation
  - [x] `FSharpFunctionContext` pushed only for functions returning Result/Option
  - [x] Early return block and storage created for `?` operator unwrap-or-propagate
  - [x] Fix use-after-free bug when `?` is applied to function call results
    - [x] Copy `returnBlock` and `returnStorage` from context before `codegen(operand)` call
    - [x] Prevents invalidation when nested function calls push new contexts onto vector
  - [x] Fix `?` operator auto-wrapping for type-consistent return values
    - [x] `ReturnKind` enum (`Plain`/`Result`/`Option`) replaces `bool returnsResultOrOption` for precise type tracking
    - [x] `determineReturnKind()` replaces `isBodyResultOrOption()` with support for `LetInExpr`, `IfExpr`, `ApplicationExpr`, and `containsTryExpr()` fallback
    - [x] `needsAutoWrap()` checks if function body's final expression already produces Result/Option
    - [x] `wrapInResultOrOption()` emits OALLOC/OSETTAG/OSETSLOT to wrap raw values in Ok/Some at function return
    - [x] All three function application sites (pipeline-with-args, pipeline-non-recursive, ApplicationExpr) updated to auto-wrap
    - [x] Enables pattern matching on `?`-returning function results: `match (f x) with | Ok n -> n | Error e -> e`
- [x] Implement `try-with` expression IR generation
  - [x] Store body object in alloca for cross-block access
  - [x] Handle `ConstructorPattern` (e.g., `Error e`, `None`) for error binding
  - [x] Extract success value on Ok/Some, run handler on Error/None
  - [x] Multiple handler support with literal pattern matching
    - [x] Added `VCMPEQ` opcode for dynamic value comparison (compares values regardless of compile-time type)
    - [x] Added `VCmpEQInstr` IR instruction and `createVCmpEQ` builder method
    - [x] Updated `PatternIRGenerator` to use dynamic comparison for `Void`/`Object` typed values
    - [x] Added cast support for `Void`/`Object` to `String` via `N2S` for print compatibility
  - [x] Guard expressions in handlers
    - [x] Added full set of dynamic comparison opcodes (`VCMPNE`, `VCMPLT`, `VCMPLE`, `VCMPGT`, `VCMPGE`)
    - [x] Added corresponding IR instructions and IRBuilder methods
    - [x] Updated `IRGenerator::visit(BinaryExpr)` to use `VCmpXX` when operands have dynamic types
    - [x] Added `needsDynamicCompare()` helper to detect `Void`/`Object` typed values
    - [x] Fixed match expression variable binding for constructor patterns to extract payload correctly
- [x] Implement `try-finally` expression IR generation
  - [x] `TryFinallyExpr` AST node, `Token::Finally` keyword, parser support
  - [x] IR generation with `?` interception: redirects `returnBlock` to finally cleanup block
  - [x] Tail call suppression during body codegen to prevent skipping finally
  - [x] Top-level fallback (simple linear codegen when no function context)
  - [x] Nested try-finally chains correctly (inner finally → outer finally)
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
- [x] Fix logical OR operator (3 copy-paste bugs: `createBXor` emitted `BAndInstr`, `BOrInstr` visitor emitted `BAND`, `||` codegen used `createBXor` instead of `createBOr`)
- [x] Fix string concatenation with `+` operator (was always converting to numbers; now detects string operands and uses `createSAdd`)
- [x] Implement if-then-else expressions (`IfExpr` AST node, parser, IR codegen with alloca/branch/merge)
- [x] Fix if-then-else result type inference (defer alloca creation until branch type is known, fixes string/float results)
- [x] Implement mutable variable assignment (`MutAssignStmt` AST node, `<-` operator, mutability tracking via `BindingInfo`)
- [x] Implement tuple expressions (`TupleExpr` AST node, 2-/3-element tuples via TypedObject with Tuple2/Tuple3 types)
- [x] Implement tuple pattern matching (full `TuplePattern` in `PatternIRGenerator` with slot extraction and sub-pattern chaining)
- [x] Implement standard library builtins (`string_length`, `int_of_string`, `string_of_int`, `not`)
- [x] Implement `env` builtin — returns `option<str>` for environment variables
  - [x] Two native callbacks (`env.has(S)B`, `env.get(S)S`) with IR-level Option construction
  - [x] Production runtime (Shell.cpp) reads from `Environment&`, test runtime uses mock env map
  - [x] Stub runtime for LSP/HoverProvider, hover text for `env` identifier
  - [x] 13 test cases covering existing/missing vars, match Some/None, let binding, empty value, multiple vars, functions, default values, IR generation, and `?` operator
- [x] Support `?` operator at top-level (global) scope — exits handler with code 1 on None/Error instead of requiring a function context
- [x] Remove `fst`/`snd` builtins — now user-definable via pattern matching (simplifies compiler, proves language expressiveness)
- [x] Fix boolean literal codegen (`_builder.getBoolean()` instead of `_builder.get()` which silently converted `bool` to `int64_t`)
- [x] Fix `print` for boolean values (conditional branch to `"true"`/`"false"` since no `B2S` opcode exists)
- [x] Fix lexer `))` merging in F# mode (consecutive `)` no longer merged to `DblRndClose` except in arithmetic contexts)
- [x] Add `LANGUAGE_STATUS.md` for tracking F# feature implementation status
- [x] Implement float (double) primitive type with end-to-end support
  - [x] `LiteralType::Float` in type system, 18 float opcodes (FLOAD, FNEG, FADD..FPOW, FCMPEQ..GT, N2F, F2N, F2S, S2F)
  - [x] `ConstantFloat`, float constants in ConstantPool/IRProgram/IRBuilder
  - [x] 13 float IR instruction types (FNegInstr, FAddInstr..FPowInstr, FCmpEQInstr..FCmpGTInstr)
  - [x] IRBuilder float operations with constant folding (createFAdd..createFPow, createFCmpEQ..createFCmpGT)
  - [x] Float cast operations (createN2F, createF2N, createF2S, createS2F)
  - [x] TargetCodeGenerator float visitors, CastInstr map (Float↔Number, Float↔String), FLOAD emitLoad
  - [x] VM Runner float execution via `std::bit_cast<uint64_t>(double)` for zero-cost stack storage
  - [x] IRGenerator: FloatLiteralExpr, auto-promotion (int+float→float via N2F), float negation, print F2S
  - [x] PatternIRGenerator: float literal patterns with FCmpEQ comparison
  - [x] 17 float test cases (literal, arithmetic, division, mixed promotion, comparisons, negation, concat, functions, pow, mod)
- [x] Implement multi-line expression support in parser
  - [x] `consumeNewlines()` helper skips `LineFeed`/`Semicolon` at continuation points
  - [x] Match expressions: arms can span multiple lines (newline-skip with pushback pattern)
  - [x] Try-with expressions: handler arms can span multiple lines
  - [x] If-then-else: condition, then-branch, and else-branch can be on separate lines
  - [x] Lambda expressions: body can be on a separate line after `->`
  - [x] Let-in expressions: value and body can be on separate lines
  - [x] Top-level let bindings: value expression can be on a separate line after `=`
  - [x] Mutual recursion `and` keyword: can appear on a new line after preceding function body
- [x] Implement numeric base literals and comments in lexer/parser
  - [x] Hexadecimal literals: `0xFF`, `0XFF` with base-16 parsing via `std::from_chars`
  - [x] Octal literals: `0o755`, `0O755` with base-8 parsing
  - [x] Binary literals: `0b1010`, `0B1010` with base-2 parsing
  - [x] Scientific notation: `1e10`, `2.5e-3` (already implemented, added tests)
  - [x] `#` line comments (shell style, after whitespace)
  - [x] `//` line comments (C style)
  - [x] `(* ... *)` nestable block comments (F# style)
- [x] Fix mutable variable reassignment not persisting across REPL prompts (runtime value snapshots from stack after execution)
- [x] Bare top-level F# function calls (`f 42` dispatches to F# expression parser when `f` is a known function)
  - [x] Parser tracks `_knownFSharpFunctions` set, populated from `let` definitions and `FSharpPersistentState`
  - [x] Covers `let f x = ...`, `let rec f ... and g ...`, `let f = fun x -> ...`
  - [x] Pre-seeded from persistent state in Shell and test helpers for REPL continuity
  - [x] F# definitions intentionally shadow shell commands of the same name
- [x] Implement type annotations for variables, function parameters, and return types
  - [x] `TypedParameter` AST node with optional `TypePtr` annotation
  - [x] Parser: `parseType()` for function types (`int -> int`), `parseBaseType()` for primitives/generics/tuples, `parseTypedParameter()` for `(x: int)` annotated params
  - [x] Variable annotations: `let x: int = 42`, `let s: str = "hello"`
  - [x] Function parameter annotations: `let add (x: int) (y: int): int = x + y`
  - [x] Lambda annotations: `fun (x: int) -> x + 1`
  - [x] Return type annotations: `let double x: int = x * 2`
  - [x] Mixed annotated and bare params: `let f (x: int) y = x + y`
  - [x] Static type validation at IR generation (parameter types checked at call site, return types checked after body codegen)
  - [x] Type annotations persist across REPL sessions via `FSharpPersistentState`
  - [x] ASTPrinter support for round-trip printing of type annotations
  - [x] 32 test cases covering positive execution, negative type mismatches, parser structure, and ASTPrinter output
- [x] Improve arity enforcement error messages and test coverage
  - [x] Fix grammar: "expects 1 argument" (singular) vs "expects 2 arguments" (plural) for both direct calls and pipelines
  - [x] 8 comprehensive arity enforcement tests: over-application (5 failure cases), exact arity (3 success cases)
- [x] Update syntax highlighting for new constructs (Phase 2.4)
- [x] F#-style interpolated strings: `$"Hello, {name}"`
  - [x] Lexer: 4 new tokens (FStringStart, FStringEnd, FStringExprStart, FStringExprEnd), `consumeFStringContent()` state machine, brace depth tracking for nested expressions
  - [x] Parser: `parseFStringExpression()` with F# expression parsing inside `{expr}` holes
  - [x] AST: `FStringExpr` node with alternating literal and expression parts
  - [x] IRGenerator: `visit(FStringExpr)` using `convertToString()` for Number, Float, Boolean, String support
  - [x] ASTPrinter: round-trip printing with `$"text {expr} text"` format
  - [x] TokenClassification: FStringStart/FStringEnd as String, FStringExprStart/FStringExprEnd as Punctuation
  - [x] HoverProvider: hover text for `$"..."` syntax
  - [x] Escaped braces: `{{` and `}}` produce literal `{` and `}`
  - [x] 17 test cases covering basic, variables, arithmetic, conditionals, type conversions, escaped braces, function application, pipelines, adjacent holes, concatenation, and nested strings
- [ ] Update completion for F# style (Phase 2.3)

**Implementation Notes:**
- See `LANGUAGE.md` Section 14 for detailed parser implementation notes
- Type inference uses Hindley-Milner algorithm with let-polymorphism
- `let` keyword unambiguously starts F# style (bash style uses `VAR=value` without spaces)
- `|>` and `|` are distinct tokens: function pipeline vs shell pipeline
- Dual semantics: expression context captures output, statement context prints to terminal
- Records and unions are compiled to efficient runtime representations
- Pattern matching compiles to decision trees for efficient execution
- List literals: `[1; 2; 3]` parsed as `ListExpr`, `[1..10]` as `ListRangeExpr`
- Lexer enhanced with `peekChar()` to properly recognize float literals (e.g., `2.5` as single token)
- Lexer enhanced with `DotDot` token (`..`) for cleaner range parsing
- Lexer recognizes negative number literals (e.g., `-42` as a single Number token)
- Parser handles unary negation for identifiers (e.g., `-a` parsed as `UnaryExpr(Neg, IdentifierExpr("a"))`)
- List comprehensions: `[for x in 1..10 -> x * x]` with optional `when` filter clause
- Comprehension body supports simple expressions and binary operations (e.g., `x * x`, `x + 1`)
- Lexer F# mode: Context-sensitive tokenization for F# expressions
  - `enterFSharpExpr()`/`leaveFSharpExpr()` manage depth counter for proper nesting
  - F# mode reserves additional symbols: `[]{},:+-*/%^&#` to prevent them from being consumed into identifiers
  - Operators tokenized separately: `+`, `-`, `*`, `/`, `%`, `**`, `^`, `::`, `,`, `:`, `[`, `]`, `{`, `}`, `?`
  - `-` followed by digit returns `Token::Number` (negative literal); otherwise `Token::Minus`
  - Parser calls `enterFSharpExpr()` at start of `let` bindings, ensuring proper tokenization of list literals like `[x;y;z]`
- Option/Result type system: Parser and AST support for F#-style error handling
  - `Some expr`, `None`, `Ok expr`, `Error expr` parsed as constructor expressions
  - `expr?` postfix operator for error propagation (TryExpr)
  - `try expr with | pattern -> handler` for structured error handling (TryWithExpr)
  - Pattern matching supports `Some x`, `None`, `Ok n`, `Error e` constructor patterns
  - IR generation creates tagged values (temporary encoding until CoreVM has native sum types)
  - Full runtime semantics deferred until CoreVM supports discriminated unions
- Tuple patterns in match expressions: `,` is now tokenized as `Token::Comma` in F# mode, allowing proper parsing of `(a, b)` patterns
- Test infrastructure improvements:
  - `ExecutionResult` refactored to `std::expected<TestExecutionSuccess, TestError>` for proper error handling
  - `TestExecutionSuccess` includes both `exitCode` and `output` for comprehensive test verification
  - `print`/`println` builtins registered in TestRuntime for output capture during test execution
  - `ExprStmt` AST node added to wrap F# expressions as statements (used for `print`/`println` at top level)
  - Parser handles `print`/`println` at statement level by entering F# expression mode
  - `Token::DblQuoteStart` properly recognized as F# primary expression for double-quoted strings
  - `Program::link()` call added to `executeSource()` to enable native function calls in tests
  - `print`/`println` now auto-convert numbers and dynamic values (from pattern matching) to strings via `N2S`
- Shell command expressions: `& command` syntax for embedding shell commands in F# expressions
  - `let output = & git status` captures command output to variable
  - `let diff = & git diff HEAD..master` - special characters like `..` parsed as shell tokens, not F# operators
  - `let lines = & cat file.txt | grep pattern` - shell pipes work within the `&` expression
  - Parser temporarily leaves F# mode when parsing the command after `&`, then re-enters
  - IR generation reuses `SubstitutionExpr` logic: `subst_start()` → execute → `subst_end()` to capture output
- REPL session persistence for F# definitions:
  - `FSharpPersistentState` struct holds function definitions and retained ASTs across REPL prompts
  - `IRGenerator::generate()` accepts optional persistent state: pre-populates function table on entry, stores new definitions on exit
  - `Shell` retains parsed ASTs so that function body pointers remain valid across prompts
  - Supports: function definitions (`let f x = ...`), recursive functions (`let rec`), lambda-bound variables (`let f = fun x -> ...`)
  - Simple value bindings (`let x = 42`) persist via AST re-evaluation: the value expression is re-codegen'd at each prompt so dependencies resolve correctly
  - Limitations: closure captures from previous prompts are not preserved (pure functions only)
  - Auto-generated lambda names (from partial application intermediates) are excluded from persistence
- Mutual recursion (`let rec f ... and g ...`):
  - Parser handles `and` keyword after `let rec` to chain function definitions
  - `AndBinding` AST node stores name, parameters, and body for each `and`-binding
  - IR generation uses dispatch-loop optimization: integer tag selects function body, tail calls update tag and jump back
  - `MutualRecursionContext` tracks per-function param allocas, dispatch tag, and shared result storage
  - Separate from self-recursion path which uses simpler single-function loop
- `let...in` expressions for scoped bindings:
  - `LetInExpr` AST node: binding (name, optional params, value) + body expression
  - Parsed in `parseFSharpPrimary()` when `Token::Let` appears in expression context
  - `in` keyword treated as contextual: excluded from `isFSharpPrimary()` to prevent application parser from consuming it
  - Supports both simple bindings (`let x = 5 in x + 1`) and function bindings (`let f x = x * 2 in f 5`)
  - Nested `let...in` supported naturally via recursive parsing
- Or-patterns and as-patterns in match expressions:
  - Or-patterns (`| 1 | 2 | 3 -> expr`) parsed at match arm level, accumulated into `OrPattern`
  - PatternIRGenerator reloads scrutinee from storage for each alternative (stack resets at block boundaries)
  - As-patterns (`| n as val -> expr`) bind matched value to additional name
- IRGenerator refactored from inheritance to composition with IRBuilder:
  - `IRGenerator` no longer inherits from `CoreVM::IRBuilder`; uses `_builder` member instead
  - `PatternIRGenerator` takes `CoreVM::IRBuilder&` directly, eliminating its dependency on `IRGenerator`
  - Cleaner separation of concerns: IR generation logic vs. IR building API
- Boolean value handling:
  - `BoolLiteralExpr` must use `_builder.getBoolean()` not `_builder.get()` — `bool` implicitly converts to `int64_t`, producing `ConstantInt` with `Number` type instead of `ConstantBoolean` with `Boolean` type
  - `toBool()` uses shell semantics (0=true, non-zero=false), so `Number`-typed booleans get inverted logic
  - `print` handles `Boolean` type via conditional branch IR (`if bool then "true" else "false"`) since no `B2S` VM opcode exists
  - `PatternIRGenerator` also uses `getBoolean()` for bool literals and BXor+BNot for boolean comparison
- If-then-else expressions:
  - `IfExpr` AST node with condition, thenExpr, elseExpr (separate from `IfStmt` for bash-style)
  - IR codegen: alloca for result storage, condBr on condition, then/else blocks store result, merge block loads
  - Result alloca type is deferred until after branch codegen — uses `thenResult->type()` (or `elseResult->type()` if then is a tail call) instead of hardcoded `Void`
  - `then` and `else` added to `isFSharpPrimary()` exclusion list to prevent argument consumption
- Mutable assignment:
  - `BindingInfo { Value*, bool isMutable }` replaces raw `Value*` in `FSharpScope::bindings`
  - `MutAssignStmt` codegen: lookup binding, verify mutability, store new value via `createStore`
- Tuple expressions and patterns:
  - `TupleExpr`: codegen allocates TypedObject (Tuple2/Tuple3), sets slots for each element with chained `ObjSetSlot` (each call updates the `obj` pointer, keeping `ObjAlloc` use-count at 1 for efficient `STACKROT` codegen)
  - `TuplePattern` in PatternIRGenerator: extracts slots via `createObjGetSlot`, chains sub-pattern matches
  - Pre-allocated binding storage (`setBindingStorage()`) allows PatternIRGenerator to store values during pattern matching, avoiding cross-block references
  - Tuple scrutinee reloaded from storage for subsequent slot extractions after block boundaries
  - `fst`/`snd` removed as builtins — user-definable via `let fst t = match t with | (a, _) -> a` (generates equivalent `ObjGetSlot` IR through pattern matching)
- String concatenation:
  - `visit(BinaryExpr)` checks if either operand is `LiteralType::String` for `Add` operator
  - Converts non-string operands via `createN2S()`, then uses `createSAdd()` for concatenation
  - All other operators continue with existing numeric coercion path
- Standard library builtins:
  - `tryGenerateBuiltinCall()` helper consolidates dispatch, following existing `print`/`println` pattern
  - `string_length` uses `createSLen()`, `int_of_string` uses `createS2N()`, `string_of_int` uses `createN2S()`
  - `not` uses `createBNot(toBool(v))`
- Lexer fix for nested parentheses:
  - In F# mode, consecutive `)` characters were merged into `DblRndClose` (shell arithmetic `))`)
  - Fix: only merge `))` when `_arithDepth > 0` or in `$((…))` context
  - Similarly, `((` only produces `DblRndOpen` when `_fsharpDepth == 0`
- Arithmetic assertion relaxation:
  - `isNumberCompatible()` helper accepts `Number`, `Void`, and `Object` types for dynamic values from `ObjGetSlot`
  - All arithmetic and numeric comparison assertions updated to use this helper
- Feature status tracking:
  - `LANGUAGE_STATUS.md` tracks implementation status of all F# features from `LANGUAGE.md`

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

**Implementation Notes:**
- Multiline editing uses Alt+Enter or Shift+Enter to insert newlines (Enter submits)
- Editor region scrolls to keep cursor visible when content exceeds max height
- Selection highlighting uses inverse video (SGR 7/27)
- Display width calculation uses libunicode for proper Unicode handling
- Keybinding system maps key chords to edit actions, enabling future vi mode support
- Default keybindings use modern conventions: Ctrl+C=copy, Ctrl+Y=redo, Ctrl+D=delete char (EOF on empty)
- Shift+movement keys extend selection; Ctrl+D is context-sensitive (EOF vs delete)
- Kitty keyboard protocol support: Full handling of Kitty's CSIu escape sequences including:
  - CapsLock and NumLock modifiers (bits 6-7) for proper capitalization with CapsLock active
  - All special keycodes in Private Use Area (57344-63743): lock keys, F13-F35, keypad, media keys, modifier keys
  - CapsLock XOR Shift behavior: either one (but not both) capitalizes letters, matching standard keyboard behavior
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
- Tab or Ctrl+Space triggers completion menu; Right arrow or End accepts ghost text

**Tasks:**
- [x] Design completion provider interface (`CompletionProvider`, `CompletionItem`, `CompletionContext`)
- [x] Implement context analysis (`CompletionContext.cpp` - uses Lexer to determine context type)
- [x] Implement command name completion (`CommandCompleter.cpp` - builtins + PATH scanning with caching)
- [x] Implement file path completion (`FileCompleter.cpp` - with tilde expansion)
- [x] Implement variable name completion (`VariableCompleter.cpp` - env vars + special vars)
- [x] Implement option/flag completion stub (`OptionCompleter.cpp` - placeholder for future --help parsing)
- [x] Implement history-based suggestions (`HistoryCompleter.cpp` - prefix matching with recency scoring)
- [x] Implement history abstraction (`History` interface, `InMemoryHistory` implementation)
- [x] Implement completer orchestrator (`Completer.cpp` - coordinates providers, generates suggestions)
- [x] Add ghost text support to InputField (`setGhostText()`, `acceptGhostText()`, auto-clear on modification)
- [x] Add completion styles to Theme (`ghostText`, `completionItem`, `completionSelected`, `completionDesc`)
- [x] Design and implement completion popup UI (`CompletionPopup.cpp` - bordered list with scroll indicators)
- [x] Integrate completion with Prompt (Tab/Ctrl+Space triggers, menu navigation, ghost text rendering)
- [x] Add comprehensive completion tests (`Completer_test.cpp`, `CompletionPopup_test.cpp` - 35 tests)

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
- Unhandled keys cause popup to dismiss and pass through to parent (removed `None` action)
- Visibility state is properly synced between `CompletionPopup` and `Component` base class
- Dynamic filtering: typing while popup is visible filters the list in real-time; `updateItems()` preserves selection when the selected item still matches, otherwise selects best match; auto-closes on 0 matches
- Popup positioning: In inline mode (primary screen), always renders below cursor - Screen creates space by emitting newlines to use scrollback buffer; in fullscreen/fixed mode, renders above cursor when not enough space below (< 3 rows)
- Completion menu appears below cursor with Up/Down/Ctrl+J/Ctrl+K/Tab/Shift+Tab navigation, Enter to accept, Escape to dismiss
- Single completion matches are inserted directly without showing menu
- `Environment` class extracted to `Environment.hpp` for cleaner dependency management
- Shell class creates `Completer` with environment and history, connects to Prompt via `setCompleter()`
- Executed commands are added to both prompt history (Up/Down recall) and completion history (suggestions)
- Test utilities in `src/tui/TestHelpers.hpp` for rendering verification (`canvasToString()`, `renderPopup()`, etc.)
- 39 completion-related tests covering Completer, CompletionPopup, and updateItems functionality

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
- Synchronized output: `Screen::flush()` uses DEC mode 2026 (`SyncGuard`) to batch terminal updates and prevent visual tearing
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
- [ ] Implement alias expansion tooltips (placeholder for when aliases are implemented)
- [ ] Implement inline help for commands
- [ ] Implement error tooltips with suggestions
- [ ] Integrate with man pages for command help

**Implementation Notes:**
- `HoverState` class tracks mouse position with 500ms delay before showing tooltip
- `Tooltip` component supports both plain text and markdown content with scrolling
- `StyledText` class provides styled text rendering with markdown parsing (reuses parsing from `MarkdownRenderer`)
- `CommandResolver` determines command type (external, builtin, alias, not found) and provides tooltip text
- Screen overlay system used for tooltip positioning
- Hover callbacks integrated into event loop with poll timeout management
- Tooltip automatically hides when user starts typing
- Inline mode coordinate tracking handles content shifts caused by tooltip/popup rendering:
  - `_mainContentHeight` tracks main content height before overlays for accurate mouse hit-testing
  - `_peakContentHeight` tracks maximum allocated space to avoid redundant newline emission
  - `_totalNewlinesEmitted` tracks cumulative shift for correct coordinate translation
  - `_inlineContentStartRow` recalculated each frame based on terminal size and newlines emitted
  - Ctrl+L (clear screen) calls `releaseCursor()` to reset coordinate tracking and re-query cursor position

### Phase 2.6: Customizable Prompt

**Dependency:** Phase 2.4 (highlighting for prompt elements)

**Tasks:**
- [ ] Design prompt configuration format
- [ ] Implement prompt segment system
- [ ] Implement common segments (cwd, git, time, exit code)
- [ ] Implement VT420 host-writable status line integration
- [ ] Support OSC-8 hyperlinks in prompts
- [ ] Add prompt configuration tests

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

---

## Milestone 4: Windows Support

**Priority:** High (must-have for 1.0)
**Rationale:** Required for broad adoption; must be developed in parallel, not as an afterthought.

### Phase 4.1: Build System

**Dependency:** Milestone 0.2 (platform abstraction design)

**Tasks:**
- [ ] Add Windows CMake preset
- [ ] Configure MSVC and Clang-cl support
- [ ] Set up Windows CI pipeline
- [ ] Handle Windows-specific dependencies

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
- [ ] Documentation complete (user guide, configuration reference)
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
