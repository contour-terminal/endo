---
title: Language Features Roadmap
description: Implementation status of F# language features in Endo.
---

# Language Features Roadmap

This document tracks the implementation status of F# language features in Endo as defined
in the language specification.

**Legend:** [x] Implemented | [~] Partial | [ ] Not yet implemented

---

## Expressions

- [x] Immutable bindings: `let x = 42`
- [x] Mutable bindings: `let mut x = 0`
- [x] Export bindings: `let export X = expr` -- binds value and exports as environment variable
- [x] Mutation operator: `x <- x + 1`
- [x] Lambda expressions: `fun x -> x * 2`
- [x] Lambda expression sugar: `_ + 1`, `_.field` etc.
- [x] Let-in expressions: `let x = 5 in x + 1`
- [x] If-then-else expressions: `if cond then a else b`
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

- [x] `int` -- 64-bit signed integer
- [x] `float` -- 64-bit floating point
- [x] `str` -- UTF-8 string
- [x] `bool` -- Boolean
- [x] `unit` -- No value (void)

### Compound Types

- [x] Lists: `list<int>` (cons-cell linked list)
- [x] Tuples: `(int, str)` (2 and 3 elements)
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

- [x] Hindley-Milner type inference (Algorithm W) as separate pre-pass
- [x] Primitive type inference: `int`, `float`, `bool`, `str`, `unit` inferred from usage
- [x] Operator-driven inference: `x + y` infers `int`, `x +. y` infers `float`
- [x] Recursive function inference
- [x] Let-polymorphism: `let id x = x` can be used at multiple types
- [~] Complex type inference: list, option, result, function types inferred but not yet applied to compilation

## Functions

- [x] Single-parameter functions: `let double x = x * 2`
- [x] Multi-parameter (curried) functions: `let add x y = x + y`
- [x] Partial application: `let add5 = add 5`
- [x] Lambda expressions: `fun x -> x * 2`
- [x] Multi-parameter lambdas: `fun x y -> x + y`
- [x] Closures (capturing outer scope variables)
- [x] Recursive functions: `let rec gcd a b = ...`
- [x] Mutual recursion: `let rec isEven n = ... and isOdd n = ...`
- [x] Tail-call optimization
- [x] Function composition: `>>` and `<<` operators
- [x] Type-annotated functions
- [x] Higher-order functions: passing functions as arguments
- [x] Variadic parameters: `let f ...args = ...`
- [x] Splat expression: `...args` in shell commands
- [x] Shell aliases via let bindings: `let ll ...args = & exa -l ...args`

## Lists and Collections

- [x] List literal construction: `[1; 2; 3]`
- [x] List ranges: `[1..10]`, `[1..2..10]`, `[10..-1..7]`
- [x] Character ranges: `['a'..'z']`, `['A'..'Z']`, `['0'..'9']`
- [x] Cons operator: `::` (right-associative)
- [x] List concatenation: `@`
- [x] List comprehensions with optional `when` filter
- [x] Standard operations: `map`, `filter`, `fold`, `reduce`, `reverse`
- [x] Indexed access: `nth`, `last` (return `option<T>`)
- [x] Construction: `replicate`
- [x] Utility operations: `find`, `exists`, `forall`, `take`, `drop`, `zip`, `flatten`
- [x] Compile-time type checking for heterogeneous list literals

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
- [x] Nested list patterns

## Operators

### Arithmetic

- [x] `+`, `-`, `*`, `/`, `%`, `**`
- [x] Unary negation: `-x`

### Comparison

- [x] `==`, `!=`, `<`, `<=`, `>`, `>=`
- [x] Dynamic comparison for values from pattern matching

### Logical

- [x] `&&` (AND, short-circuit), `||` (OR, short-circuit), `!` (NOT)

### String

- [x] Concatenation: `"hello" + " world"`
- [x] Mixed type concatenation: `"count: " + 42`
- [x] Interpolated strings: `$"Hello, {name}"`
- [x] Repetition: `"ha" * 3`

### Pipe Operators

- [x] Forward pipe: `|>` (value to function)
- [x] Shell pipe: `|` (process stdout to stdin)
- [x] Structured pipeline: shell command `|>` F# pipeline with output recognition

### Composition

- [x] Forward: `>>`, Backward: `<<`

### List

- [x] Cons: `::` (right-associative), Concatenation: `@`

### Special

- [x] Error propagation: `?`
- [x] Optional chaining: `?.`
- [x] Option default: `?|`

## Error Handling

- [x] Result type: `Ok value`, `Error msg`
- [x] Option type: `Some value`, `None`
- [x] Error propagation with auto-wrapping: `expr?`
- [x] Try-with expression: `try expr with | Error e -> handler`
- [x] Try-finally: `try ... finally cleanup`
- [x] Option combinators: `Option.map`, `Option.bind`, `Option.defaultValue`
- [x] Compile-time error for unwrapped Option/Result in binary operations

## Control Flow

- [x] If-then-elif-else expressions (F# style, returns value)
- [x] Match expressions
- [x] While loops
- [x] For-in loops
- [x] Break and continue
- [x] For loops with destructuring: `for (name, value) in entries do ... done`

## Standard Library Builtins

- [x] `print`, `println` -- output with/without newline
- [x] `string_length`, `int_of_string`, `string_of_int`, `not`
- [x] `env` -- returns `option<str>` for environment variables
- [x] `head`, `tail`, `length`, `isEmpty` -- list operations
- [x] `map`, `filter`, `fold`, `reduce` -- higher-order list functions
- [x] `find`, `exists`, `forall`, `take`, `drop`, `zip`, `flatten`
- [x] `sort`, `reverse`, `distinct`, `sortBy`, `groupBy`
- [x] `nth`, `last`, `replicate`
- [x] `split`, `join`, `trim`, `contains`, `startsWith`, `endsWith`, `toLower`, `toUpper`, `replace`
- [ ] `fetch` -- HTTP GET returning `result<str, str>`
- [ ] `Json.parse`, `Json.stringify` -- JSON serialization
- [ ] `File.read`, `File.write`, `File.list` -- file operations
- [ ] `Path.join`, `Path.extension`, `Path.basename` -- path operations

## Shell Integration

- [x] Shell command expressions: `& git status`
- [x] Command substitution: `$(cmd)`, `` `cmd` ``
- [x] Variable substitution: `$VAR`, `${VAR}`
- [x] String interpolation in double-quoted strings
- [x] Process substitution: `<(cmd)`, `>(cmd)`
- [x] Redirections: `>`, `>>`, `<`, `2>&1`, `<<<`
- [x] Job management: `&`, `jobs`, `fg`, `bg`
- [x] Context-aware shell commands: capture mode in expression context, normal I/O at statement level

## REPL

- [x] Persist function definitions across prompts
- [x] Persist recursive and mutual-recursive functions
- [x] Persist simple value bindings
- [x] Persist closure captures from previous prompts

---

## Implementation Phases

### Phase 1 -- Foundation Completions (Complete)

- [x] Unit type `()`
- [x] Tuple destructuring in `let`
- [x] Function composition `>>` and `<<`
- [x] String repetition `"ha" * 3`
- [x] Block scopes `{ let x = 1; x + 2 }`

### Phase 2 -- List Runtime (Complete)

- [x] Cons-cell linked list representation
- [x] IR generation for list literals, cons, concat, ranges, and comprehensions
- [x] Pattern matching for lists
- [x] List printing via recursive formatting

### Phase 3 -- List Standard Library (Complete)

- [x] Basic: `head`, `tail`, `length`, `isEmpty`
- [x] Higher-order: `map`, `filter`, `fold`, `reduce`
- [x] Transformations: `sort`, `reverse`, `distinct`
- [x] Utility: `zip`, `flatten`, `take`, `drop`, `find`, `exists`, `forall`
- [x] Key-based: `sortBy`, `groupBy`
- [x] Indexed: `nth`, `last`
- [x] Character ranges: `['a'..'z']`

### Phase 4 -- Records (Complete)

- [x] Type definitions, record literals, field access, record update, pattern matching

### Phase 5 -- Discriminated Unions (Complete)

- [x] Union type definitions, constructors, pattern matching, multi-slot payloads

### Phase 6 -- Remaining Operators and Features

- [x] Optional chaining `?.`, Option default `?|`, Option combinators
- [x] For loop destructuring
- [ ] Numeric literal suffixes (byte sizes, durations)

### Phase 6.3a -- Output Recognition Files (Complete)

- [x] YAML definition format, output definition registry
- [x] JSON and fields parsers
- [x] Pipeline integration
- [x] Bundled definitions for `docker ps`, `docker images`, `git log`, `git status`

### Phase 6.4 -- Bare Expression Evaluation and Table Display (Complete)

- [x] Bare expression evaluation at shell prompt
- [x] `display_result` builtin for runtime value display
- [x] Table rendering for lists of records
- [x] `toText` builtin for plain text conversion

### Phase 7 -- String and File Standard Library

- [x] String: `split`, `join`, `trim`, `contains`, `startsWith`, `endsWith`, `toLower`, `toUpper`, `replace`
- [ ] File: `File.read`, `File.write`, `File.list`
- [ ] Path: `Path.join`, `Path.extension`, `Path.basename`

### Phase 8 -- Module System

- [ ] `import` statements and module loading
- [ ] Module-qualified access: `List.map`, `String.split`

### Phase 9 -- Generic Types

- [ ] Type variable introduction (`'a` syntax)
- [ ] Generic type definitions
- [ ] Monomorphization or type erasure at codegen

## Lexer and Parser

- [x] Context-sensitive tokenization (F# mode vs shell mode)
- [x] Syntax highlighting with statement-level mode tracking
- [x] F# operator tokens, nested parentheses, comma tokenization
- [x] Numeric literals: decimal, hex (`0xFF`), octal (`0o755`), binary (`0b1010`), scientific (`1e10`), float (`3.14`)
- [x] Comments: `#`, `//`, `(* ... *)`
- [x] `true`/`false` as native boolean token literals
- [ ] Numeric literal suffixes: `1kb`, `1mb`, `1gb`, `1tb`, `1ms`, `1s`, `1min`, `1h`

## Completion System

- [x] Shared completion infrastructure in `endo-language`
- [x] Context analyzer, candidate generators, orchestrator
- [x] Shell completion adapter with fuzzy scoring
- [x] LSP `textDocument/completion` support
- [x] Record-aware dot-access completion
- [x] Variable-specific record field completion

## Modules and Imports

- [ ] `import` / `from ... import` statements
- [ ] Module-qualified access
- [ ] Module creation and exports
