# Endo Language — F# Feature Implementation Status

This document tracks the implementation status of F# language features as defined in `docs/language/`.

**Legend:** [x] Implemented | [~] Partial | [ ] Not yet implemented

---

## Expressions

- [x] Immutable bindings: `let x = 42`
- [x] Mutable bindings: `let mut x = 0`
- [x] Export bindings: `let export X = expr` — binds value and exports as environment variable (scalar types only)
- [x] Mutation operator: `x <- x + 1`
- [x] Lambda expressions: `fun x -> x * 2`
- [x] Lambda expression sugar: `_ + 1` → `fun __x -> __x + 1`, `_.field ...` → `fun __x -> __x.field ...` etc
- [x] Let-in expressions: `let x = 5 in x + 1`
- [x] If-then-else expressions: `if cond then a else b` (else optional, returns unit)
- [x] Match expressions: `match x with | pattern -> result`
- [x] List expressions: `[1; 2; 3]`
- [x] List ranges: `[1..10]`, `[1..2..10]`, `[10..-1..7]`
- [x] List comprehensions: `[for x in items -> expr]`, `[for x in items when cond -> expr]`
- [x] Record expressions: `{ name = "Alice"; age = 30 }`
- [x] Record update: `{ alice with age = 31 }`
- [x] Tuple expressions: `(1, "hello")` (2 and 3 elements)
- [x] Tuple destructuring in let: `let (x, y) = tuple`
- [x] Record destructuring in let: `let { name; age } = person`
- [x] Block scopes: `{ let inner = 20; inner + outer }`

## Types

### Primitive Types
- [x] `int` — 64-bit signed integer
- [x] `float` — 64-bit floating point (via `std::bit_cast<uint64_t>(double)` in VM stack)
- [x] `str` — UTF-8 string
- [x] `bool` — Boolean
- [x] `unit` — No value (void)

### Compound Types
- [x] Lists: `list<int>` (cons-cell linked list via TypedObject)
- [x] Tuples: `(int, str)` (2 and 3 elements via TypedObject)
- [x] Options: `option<T>` with `Some` and `None`
- [x] Results: `result<T, E>` with `Ok` and `Error`
- [x] Records: `type Person = { name: str; age: int }`
- [x] Discriminated Unions: `type Shape = | Circle of float | Rectangle of float * float`
- [ ] Generic types

### Type Annotations
- [x] Variable annotations: `let count: int = 42`
- [x] Function parameter annotations: `let add (x: int) (y: int): int = x + y`
- [x] Lambda annotations: `fun (x: int) -> x + 1`

### Type Inference
- [x] Hindley-Milner type inference (Algorithm W) as separate pre-pass before IR generation
- [x] Primitive type inference: `int`, `float`, `bool`, `str`, `unit` inferred from usage context
- [x] Operator-driven inference: `x + y` infers `int`, `x +. y` infers `float`, `x ++ y` infers `str`
- [x] Recursive function inference: `let rec fact n = ...` infers `n: int` from body
- [x] Let-polymorphism: `let id x = x` can be used at multiple types
- [~] Complex type inference: list, option, result, function types inferred but not yet applied to compilation (requires handler compilation improvements)

## Functions

- [x] Single-parameter functions: `let double x = x * 2`
- [x] Multi-parameter (curried) functions: `let add x y = x + y`
- [x] Partial application: `let add5 = add 5`
- [x] Lambda expressions: `fun x -> x * 2`
- [x] Multi-parameter lambdas: `fun x y -> x + y`
- [x] Closures (capturing outer scope variables)
- [x] Recursive functions: `let rec gcd a b = ...`
- [x] Mutual recursion: `let rec isEven n = ... and isOdd n = ...`
- [x] Nested recursive functions with multi-statement bodies (indentation-based)
- [x] Tail-call optimization
- [x] Function composition: `>>` and `<<` operators
- [x] Type-annotated functions
- [x] Higher-order functions: passing functions as arguments (`let apply f x = f x`)
- [x] HOF with partial application, closures capturing function refs, and pipelines
- [x] Variadic parameters: `let f ...args = ...` — collects extra arguments into a list
- [x] Splat expression: `...args` in shell commands — expands list into individual command arguments
- [x] Shell aliases via let bindings: `let ll ...args = & exa -l ...args`
- [x] Variadic function invocation at statement level: bare `ll` or `ll somefile` with shell-mode arg parsing

## Lists & Collections

- [x] List literal construction: `[1; 2; 3]` (including multi-line)
- [x] List ranges: `[1..10]`, `[1..2..10]`, `[10..-1..7]`
- [x] Character ranges: `['a'..'z']`, `['A'..'Z']`, `['0'..'9']`
- [x] Cons operator: `::` (right-associative, `1 :: 2 :: []`)
- [x] List concatenation: `@` (`[1; 2] @ [3; 4]`)
- [x] List comprehensions: `[for x in items -> expr]`, with optional `when` filter
- [x] Standard list operations: `map`, `filter`, `fold`, `reduce`, `reverse`
- [x] Indexed access: `nth`, `last` — return `option<T>`
- [x] Construction: `replicate` — create list of N copies of a value
- [x] Utility list operations: `find`, `exists`, `forall`, `take`, `drop`, `zip`, `flatten`
- [x] Remaining list operations (`each`, etc.)
- [x] List element literal type tracking for correct string printing in HOFs
- [x] List/tuple elements with block-creating expressions (env, if-then-else) — store-per-element fix
- [x] Compile-time type checking for heterogeneous list literals (prevents runtime crash)

## Pattern Matching

### Basic Patterns
- [x] Literal patterns: `| 0 -> "zero"`, `| "hello" -> ...`
- [x] Variable binding: `| n -> n + 1`
- [x] Wildcard: `| _ -> "default"`
- [x] Boolean patterns: `| true -> ... | false -> ...`

### Compound Patterns
- [x] Tuple patterns: `| (a, b) -> a + b`
- [x] List patterns: `| [] -> ... | [x] -> ... | head :: tail -> ...`
- [x] Record patterns: `| { name; age } -> ...`
- [x] Constructor patterns (Option): `| Some x -> ... | None -> ...`
- [x] Constructor patterns (Result): `| Ok v -> ... | Error e -> ...`

### Advanced Patterns
- [x] Or-patterns: `| 1 | 2 | 3 -> "small"`
- [x] As-patterns: `| n as val -> ...`
- [x] Guards (when clauses): `| x when x > 0 -> "positive"`
- [x] Nested record patterns
- [x] Nested list patterns (recursive cons matching with accumulator-style recursion)
- [x] Bare tuple syntax in match expressions: `match a, b with | x, y -> ...` (F#-style, no parens required)

## Operators

### Arithmetic
- [x] `+`, `-`, `*`, `/`, `%`, `**`
- [x] Unary negation: `-x`

### Comparison
- [x] `==`, `!=`, `<`, `<=`, `>`, `>=`
- [x] Dynamic comparison for values from pattern matching (VCMPEQ etc.)

### Logical
- [x] `&&` (AND, short-circuit)
- [x] `||` (OR, short-circuit)
- [x] `!` (NOT)
- [x] `toBool` handles Float (`FCmpEQ(v, 0.0)`) and String (`SCmpEQ(v, "")`) types (fixes SIGABRT crash in HOF predicates during diagnostics)

### String
- [x] Concatenation: `"hello" + " world"`
- [x] Mixed type concatenation: `"count: " + 42`
- [x] F#-style interpolated strings: `$"Hello, {name}"`
- [x] Repetition: `"ha" * 3`

### Pipe Operators
- [x] Forward pipe: `|>` (data |> func)
- [x] Shell pipe: `|` (cmd1 | cmd2)
- [x] Structured pipeline: shell command `|>` F# pipeline with output recognition files
- [x] Variable binding pipeline: `let x = ...; x |> func` (single and multi-prompt)

### Composition
- [x] Forward: `>>`
- [x] Backward: `<<`

### List
- [x] Cons: `::` (right-associative)
- [x] Concatenation: `@`

### Special
- [x] Error propagation: `?`
- [x] Optional chaining: `?.`
- [x] Option default: `?|`

## Error Handling

- [x] Result type: `Ok value`, `Error msg`
- [x] Option type: `Some value`, `None`
- [x] Error propagation: `expr?` (with auto-wrapping for type-consistent returns)
- [x] Pattern matching on `?`-returning functions: `match (f x) with | Ok n -> n | Error e -> e`
- [x] `?` inside `let-in` expressions: `let f x = let v = (g x)? in v * 2`
- [x] Try-with expression: `try expr with | Error e -> handler`
- [x] Pattern matching on errors
- [x] Try-finally: `try ... finally cleanup`
- [x] Option combinators: `Option.map`, `Option.bind`, `Option.defaultValue`
- [x] Compile-time error for unwrapped Option/Result in binary operations (with suggestion in shell hover + LSP)
- [x] Source location tracking on F# expression AST nodes for accurate diagnostic ranges

## Control Flow

- [x] If-then-else expressions (F# style, returns value)
- [x] If-then-elif-else-fi statements (bash style)
- [x] Match expressions
- [x] While loops
- [x] For-in loops
- [x] Break and continue
- [x] For loops with destructuring: `for (name, value) in entries do ... end`
- [x] Bare range expressions: `1..10` and `1..2..10` (standalone, not only inside `[...]`)

## Standard Library Builtins

- [x] `print` — print without newline
- [x] `println` — print with newline
- [x] `string_length` — length of string
- [x] `int_of_string` — string to integer conversion
- [x] `string_of_int` — integer to string conversion
- [x] `string` — universal to-string conversion (int, float, bool, string passthrough)
- [x] `not` — boolean negation
- [x] `env` — returns `option<str>` for environment variables (`Some value` if set, `None` if not)
- [x] `which` — returns `option<str>` for program lookup (`Some path` if found in `$PATH`, `None` if not)
- [x] `head`, `tail`, `length`, `isEmpty` — list operations
- [x] `map`, `filter`, `fold`, `reduce` — higher-order list functions (IR-level codegen)
- [x] `find`, `exists`, `forall`, `take`, `drop`, `zip`, `flatten` — list utility functions
- [x] `sort`, `reverse`, `distinct` — list transformations
- [x] `sortBy`, `groupBy` — key-based list sorting and grouping (hybrid IR + native)
- [x] `nth`, `last` — indexed list access returning `option<T>`
- [x] `replicate` — create list of N copies of a value
- [ ] `fetch` — HTTP GET request, returns `result<str, str>`
- [ ] `Json.parse`, `Json.stringify` — JSON serialization/deserialization
- [x] `split`, `join`, `trim`, `contains`, `startsWith`, `endsWith`, `toLower`, `toUpper`, `replace` — string operations
- [x] `rand` — random integer generation (`rand` → random positive int; `rand A B` → random int in [A, B])
- [x] `formatNumber` — insert thousand separators (`formatNumber "," 1234567` → `"1,234,567"`; 1-arg locale-aware overload; pipelines supported)
- [x] Standard library reference documentation with validated code examples (`docs/language/standard-library.md`)
- [ ] `File.read`, `File.write`, `File.list` — file operations
- [ ] `Path.join`, `Path.extension`, `Path.basename` — path operations

## Shell Integration

- [x] Shell command expressions: `& git status`
- [x] Command substitution: `$(cmd)`
- [x] Variable substitution: `$VAR`, `${VAR}`
- [x] String interpolation in double-quoted strings
- [x] Process substitution: `<(cmd)`, `>(cmd)`
- [x] Redirections: `>`, `>>`, `<`, `2>&1`
- [x] Here-strings: `<<<`
- [x] Job management: `&`, `jobs`, `fg`, `bg`
- [x] Statement-level `& cmd`: shell-first execution bypassing F# bindings
- [x] Context-aware shell commands: capture mode in expression context, normal I/O at statement level
- [x] `exec` keyword: dynamic command execution with F# expression arguments and OS-level pipe support
- [x] Fix `exec` with pattern-matched tuple variables: `ensureString()` bypasses `convertToString` N2S corruption for Object/Void-typed strings from `ObjGetSlot`
- [x] Fix `TuplePattern` with `ConstructorPattern` sub-patterns: create scrutinee storage allocas so `ConstructorPattern` can reload across block boundaries
- [x] Shell word splitting: adjacent tokens without whitespace form a single word (e.g., `echo $LINES:$COLUMNS` outputs `35:127` not `35 : 127`)

## Completion System

- [x] Shared completion infrastructure in `endo-language` (context analysis, candidate generators, orchestrator)
- [x] `CompletionContextAnalyzer`: lexer-based cursor context detection (command, argument, variable, filepath, dot-access, etc.)
- [x] Candidate generators: keywords, builtins, shell keywords, constructors, dot-access (Option methods + record fields), symbols
- [x] `computeCompletions()` shared orchestrator callable by both shell and LSP
- [x] Shell completion adapter with fuzzy scoring (`applyFuzzyScoring()`)
- [x] LSP `textDocument/completion` support with trigger characters `.` and `$`
- [x] Record-aware dot-access completion: `RecordFieldInfo` carries field name + type, descriptions show `"field: typeName"`
- [x] Variable-specific record field completion: `alice.` completes only `Person` fields when variable type is known
- [x] `collectRecordInfo()` extracts record types and variable-type associations from source for LSP
- [x] Record-aware hover: hovering over record variable shows detected type name (e.g., `Person`) and type definition
- [x] Standard library function autocompletion: 40 functions (type conversion, string ops, list ops, HOFs, transforms, env/system) in both shell prompt and LSP

## Modules & Imports

- [ ] `import` statements
- [ ] `from ... import` statements
- [ ] Module-qualified access: `List.map`, `String.split`
- [ ] Module creation and exports

## Lexer / Parser

- [x] Context-sensitive tokenization (F# mode vs shell mode)
- [x] Context-aware syntax highlighting: statement-level mode tracking for shell vs F# tokenization
- [x] Shell builtin highlighting (`cd`, `export`, `echo`, etc.) as distinct `Function` category
- [x] Shell path arguments preserved as single tokens (e.g., `cd projects/endo` not split by `/`)
- [x] F# operator tokens: `+`, `-`, `*`, `/`, `%`, `**`, `|>`, `->`, `<-`
- [x] Nested parentheses in F# expressions (fixed: `))` no longer merges to `DblRndClose`)
- [x] `in`, `then`, `else` excluded from `isFSharpPrimary()` to prevent argument consumption
- [x] `let-in` expressions inside multi-line function bodies: `parseFSharpExprSequence` converts statement-level `let` to `LetInExpr` when `in` follows
- [x] Comma tokenization in F# mode for tuples
- [x] Negative number literals: `-42`
- [x] Float literals with decimal: `3.14` (distinct `Float` type with arithmetic, comparisons, and promotion)
- [x] Hexadecimal: `0xFF`
- [x] Octal: `0o755`
- [x] Binary: `0b1010`
- [x] Scientific notation: `1e10`
- [ ] Numeric literal suffixes: `1kb`, `1mb`, `1gb`, `1tb` (byte sizes); `1ms`, `1s`, `1min`, `1h` (durations)
- [x] Comments: `#`, `//`, `(* ... *)`
- [x] `true`/`false` as native boolean token literals (`Token::True`/`Token::False`), removing shell builtin variants
- [x] Shell mode: digit-leading tokens with non-digit suffixes lexed as single `Identifier` (e.g., git SHAs `3a4b5c6`, filenames `3.txt`)

## REPL

- [x] Persist function definitions across REPL prompts
- [x] Persist recursive and mutual-recursive functions
- [x] Persist simple value bindings (`let x = 42`)
- [x] Persist closure captures from previous prompts (for type-annotated functions)

---

## Implementation Roadmap

Phased plan for implementing remaining F# language features, ordered by dependencies and value.
Consult this section to determine what to work on next.

### Phase 1 — Foundation Completions (no new runtime types needed) ✅
- [x] Unit type `()` — parser recognition + trivial codegen
- [x] Tuple destructuring in `let`: `let (x, y) = tuple` — wire existing TuplePattern into let-binding LHS
- [x] Function composition `>>` and `<<` — desugar to lambdas
- [x] String repetition `"ha" * 3` — new native callback + BinaryExpr detection
- [x] Block scopes `{ let x = 1; x + 2 }` — parser + scope push/pop in IRGenerator

### Phase 2 — List Runtime (highest value, unlocks most downstream features) ✅
- [x] Register `List` type in TypeRegistry as cons-cell sum type (Nil tag=0, Cons tag=1 with 2 slots: head, tail) using `BuiltinTypeId::List = 5`
- [x] `ListExpr` codegen: build linked list right-to-left via OALLOC/OSETTAG/OSETSLOT chaining
- [x] `ListRangeExpr` codegen: loop building cons cells with `(i - start) * step >= 0` condition
- [x] `ListPattern` and `ConsPattern` in PatternIRGenerator: tag check + slot extraction
- [x] `::` (cons) operator codegen: right-associative binary op creating Cons cells (`ConsExpr`)
- [x] `@` (list concat) operator: `list_concat` native callback with cons-cell copying
- [x] List print support in `convertToString()` via `list_to_string` / `object_to_string` native callbacks
- [x] Record print support in `convertToString()` Number branch: dispatch non-List typed objects to `object_to_string` (fixes `each println` on record lists)
- [x] Fix `createAllocaInEntryBlock`: new `insertAfterAllocas()` method maintains alloca-prefix invariant (fixes 2-param recursive functions with object pattern matching)

### Phase 3 — List Standard Library (depends on Phase 2)
- [x] Basic: `head`, `tail`, `length`, `isEmpty` — native callbacks returning Option/List/int/bool
- [x] Higher-order: `map`, `filter`, `fold`, `reduce` — IR-level codegen loops invoking function arguments
- [x] Transformations: `sort`, `reverse`, `distinct` — list transformations
- [x] `ListComprehensionExpr` codegen: forward iteration + optional filter + reverse for correct order
- [x] Utility: `zip`, `flatten`, `take`, `drop`, `find`, `exists`, `forall`
- [x] Key-based: `sortBy`, `groupBy` — hybrid IR loop (key extraction) + native callbacks (sort/group)
- [x] Indexed access: `nth`, `last` — return `option<T>`, yield to user-defined functions of same name
- [x] Construction: `replicate` — create list of N copies of a value
- [x] Character ranges: `['a'..'z']` via `list_char_range` native callback

### Phase 4 — Records (parallel with Phase 2/3) ✅
- [x] Type definitions: `type Person = { name: str; age: int }` — new AST node + parser + TypeRegistry product type
- [x] Record literals: `{ name = "Alice"; age = 30 }` — new AST node + OALLOC/OSETSLOT codegen
- [x] Field access: `person.name` — new FieldAccessExpr + OGETSLOT via field-name-to-slot lookup
- [x] Record update: `{ person with age = 31 }` — copy slots + overwrite
- [x] Record pattern matching: wire existing RecordPattern AST to PatternIRGenerator

### Phase 5 — Custom Discriminated Unions (depends on Phase 4 for type def parsing)
- [x] Union type definitions: `type Shape = | Circle of float | Rectangle of float * float | Point`
- [x] User-defined constructor expressions: register variant names as constructors in scope
- [x] Pattern matching on user-defined constructors: extend ConstructorPattern beyond hardcoded Option/Result
- [x] Multi-slot payloads: `Rectangle of int * int` stores each field in separate object slots
- [x] Unit constructors: `Point` with no payload (tag-only matching)
- [x] CustomSumType on IRProgram + TypeRegistry::registerSumType(unique_ptr) for pre-assigned IDs

### Phase 6 — Remaining Operators and Small Features
- [x] Optional chaining `?.` — desugar to match on Option
- [x] Option default `?|` — desugar to match with default value
- [x] Option combinators: `Option.map`, `Option.bind`, `Option.defaultValue` (module-qualified + method-style + pipeline)
- [x] For loop destructuring: `for (name, value) in entries do ... end`
- [x] Loop-closing keyword changed from `done` to `end`
- [x] Bare range expressions: `1..10`, `1..2..10` as standalone expressions
- [x] Optional else in if-expressions: `if cond then expr` returns unit when false
- [x] Mutable assignment as expression (`MutAssignExpr`): `x <- 42` usable in expression context
- [ ] Numeric literal suffixes: byte sizes (`kb`, `mb`, `gb`, `tb`) and durations (`ms`, `s`, `min`, `h`) resolved at compile time

### Phase 6.3a — Output Recognition Files
- [x] YAML definition file format (`command.endo-output.yml`) with JSON and fields parser types
- [x] Output definition registry with variant matching by command arguments and priority
- [x] JSON output parser (NDJSON lines and JSON array formats)
- [x] Delimited-fields output parser (e.g., NUL-separated, space-separated with max_fields)
- [x] `StructuredPipelineSourceExpr` AST node bridging shell commands to F# pipelines
- [x] Pipeline partial application for `contains`, `startsWith`, `endsWith`
- [x] String comparison in F# expressions via SCmpXX instructions
- [x] Shell integration: load definitions, register callbacks, spawn commands
- [x] Bundled definitions for `docker ps`, `docker images`, `git log`, `git status`

### Phase 6.3b — Container Type Tag Slots (runtime element type propagation) ✅
- [x] Extra type tag slot per container: List slot 2, Option slot 1, Result slot 1, Tuple2 slot 2, Tuple3 slot 3
- [x] TypeRegistry slotCount increases for all container types
- [x] Pack/unpack helpers for tuple type tags (`packTypeTag`/`unpackTypeTag`)
- [x] Runner factory methods: `makeNilList`, `makeConsCell`, `makeSomeOption`, `makeNoneOption`, `makeOkResult`, `makeErrorResult`
- [x] IR emit helpers: `emitNilList`, `emitListCons`, `emitSomeOption`, `emitNoneOption`, `emitOkResult`, `emitErrorResult`, `emitTuple2`, `emitTuple3`
- [x] All raw ObjAlloc patterns replaced with emit helpers (List ~20, Option ~13, Result ~2, Tuple ~5 sites)
- [x] `slotValueToString` helper dispatches on `LiteralType` for proper string/bool/float formatting
- [x] `valueToString` reads type tag slots for all container types (fixes `println ['a', 'b', 'c']` printing raw pointers)
- [x] Fix `convertToString` Object branch: check typed objects before `getInnerType()` (innerType describes payload, not container)

### Phase 6.4 — Bare Expression Evaluation & Table Display
- [x] Bare expression evaluation at shell prompt: `42`, `Some 42`, `(1, 2)`, `Ok 5`, `[1; 2; 3]`, etc.
- [x] `display_result` builtin for runtime value display dispatch
- [x] Table rendering for lists of records: Bordered (Unicode box-drawing), Compact, Plain styles
- [x] Auto-style selection: Bordered with color for terminals, Plain for pipes/non-terminal
- [x] `print`/`println` remain unaffected (always plain text for scripting)
- [x] Trailing `|>` pipeline support on bare expressions
- [x] Suppress spurious "0" from parenthesized unit-producing expressions (e.g., `(println "hi")`)
- [x] Suppress spurious "0" from pipelines ending in unit-producing HOFs (e.g., `list |> each println`)

### Phase 6.5 — Dot Property Syntax ✅
- [x] Built-in dot properties on collection types: `xs.length`, `xs.isEmpty`, `xs.head`, `xs.tail`, `xs.last`, `opt.isSome`, `opt.isNone`, `res.isOk`, `res.isError`, `t.fst`, `t.snd`, `t.trd`, `t.0`, `t.1`, `t.2`, `s.length`
- [x] Module-level computed properties with `get`/`set` accessors: `let Name with get () = expr and set (v) = expr`
- [x] Multi-line property accessor bodies (indentation-based, like function bodies)
- [x] `with` keyword on next line after property name
- [x] Function-as-method dot access: `obj.funcName` resolves to `funcName(obj)` when first parameter type matches (field names take priority)
- [x] Tuple objectTypeId annotation: `visit(TupleExpr)` now annotates results for dot property propagation

### Phase 6.6 — Builtin Properties ✅
- [x] `NativeProperty` class in CoreVM with getter/setter callbacks
- [x] `Runtime::registerProperty()` auto-creates getter+setter `NativeCallback` entries (no new opcodes)
- [x] IRGenerator resolves properties in `IdentifierExpr` (getter) and `MutAssignStmt`/`MutAssignExpr` (setter)
- [x] Convert all single-value `set_*` builtins to read/write properties (e.g., `set_agent_provider` → `agent_provider`)
- [x] Property write syntax: `agent_provider <- "claude"`, read syntax: `print agent_provider`
- [x] LSP, completions, hover info, diagnostics updated for new property names

### Phase 7 — String and File Standard Library (depends on Phase 2 for list returns)
- [x] String: `split`, `join`, `trim`, `contains`, `startsWith`, `endsWith`, `toLower`, `toUpper`, `replace`
- [ ] File: `File.read`, `File.write`, `File.list` returning Result types
- [ ] Path: `Path.join`, `Path.extension`, `Path.basename`

### Phase 8 — Module System
- [ ] `import "path"`, `import "path" as alias`, `from "path" import (names)` parsing
- [ ] Module loading: parse imported file, link IR, namespace scoping
- [ ] Module-qualified access: `List.map`, `String.split`

### Phase 9 — Generic Types
- [ ] Type variable introduction in annotations (`'a` syntax)
- [ ] Generic type definitions: `type Tree<'a> = Leaf of 'a | Node of Tree<'a> * Tree<'a>`
- [ ] Monomorphization or type erasure at codegen time

### Phase 10 — Planned Language Enhancements ✅
- [x] Placeholder lambdas: `_ > 10` desugaring to `fun x -> x > 10` (concise pipelines like `filter (_ > 10)`)
- [x] Unit parameter in function definitions: `let f () = 42` for side-effecting functions
- [x] Named union fields: `Circle of radius: float` for self-documenting discriminated unions
- [x] Non-tail recursion support: complex-typed parameters now compile via UCALL, enabling non-tail recursive calls
- [x] `$(...)` command substitution in F# expression context: `let user = $(whoami)` bridging shell and F#
- [x] Nested list comprehensions: `[for x in xs -> for y in ys -> (x, y)]`
