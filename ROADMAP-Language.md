# Endo Language — F# Feature Implementation Status

This document tracks the implementation status of F# language features as defined in `LANGUAGE.md`.

**Legend:** [x] Implemented | [~] Partial | [ ] Not yet implemented

---

## Expressions

- [x] Immutable bindings: `let x = 42`
- [x] Mutable bindings: `let mut x = 0`
- [x] Mutation operator: `x <- x + 1`
- [x] Lambda expressions: `fun x -> x * 2`
- [x] Lambda expression sugar: `_ + 1` → `fun __x -> __x + 1`, or `_.field ...` → `fun __x -> __x.field ...` etc
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
- [x] Tail-call optimization
- [x] Function composition: `>>` and `<<` operators
- [x] Type-annotated functions
- [x] Higher-order functions: passing functions as arguments (`let apply f x = f x`)
- [x] HOF with partial application, closures capturing function refs, and pipelines

## Lists & Collections

- [x] List literal construction: `[1; 2; 3]`
- [x] List ranges: `[1..10]`, `[1..2..10]`, `[10..-1..7]`
- [x] Cons operator: `::` (right-associative, `1 :: 2 :: []`)
- [x] List concatenation: `@` (`[1; 2] @ [3; 4]`)
- [x] List comprehensions: `[for x in items -> expr]`, with optional `when` filter
- [ ] Standard list operations (`map`, `filter`, `fold`, `each`, etc.)

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

### String
- [x] Concatenation: `"hello" + " world"`
- [x] Mixed type concatenation: `"count: " + 42`
- [x] F#-style interpolated strings: `$"Hello, {name}"`
- [x] Repetition: `"ha" * 3`

### Pipe Operators
- [x] Forward pipe: `|>` (data |> func)
- [x] Shell pipe: `|` (cmd1 | cmd2)

### Composition
- [x] Forward: `>>`
- [x] Backward: `<<`

### List
- [x] Cons: `::` (right-associative)
- [x] Concatenation: `@`

### Special
- [x] Error propagation: `?`
- [ ] Optional chaining: `?.`
- [ ] Option default: `?|`

## Error Handling

- [x] Result type: `Ok value`, `Error msg`
- [x] Option type: `Some value`, `None`
- [x] Error propagation: `expr?` (with auto-wrapping for type-consistent returns)
- [x] Pattern matching on `?`-returning functions: `match (f x) with | Ok n -> n | Error e -> e`
- [x] `?` inside `let-in` expressions: `let f x = let v = (g x)? in v * 2`
- [x] Try-with expression: `try expr with | Error e -> handler`
- [x] Pattern matching on errors
- [x] Try-finally: `try ... finally cleanup`
- [ ] Option combinators: `.map()`, `.bind()`, `.defaultValue()`

## Control Flow

- [x] If-then-else expressions (F# style, returns value)
- [x] If-then-elif-else-fi statements (bash style)
- [x] Match expressions
- [x] While loops
- [x] For-in loops
- [x] Break and continue
- [ ] For loops with destructuring: `for (name, value) in entries do ... done`

## Standard Library Builtins

- [x] `print` — print without newline
- [x] `println` — print with newline
- [x] `string_length` — length of string
- [x] `int_of_string` — string to integer conversion
- [x] `string_of_int` — integer to string conversion
- [x] `not` — boolean negation
- [x] `env` — returns `option<str>` for environment variables (`Some value` if set, `None` if not)
- [x] `head`, `tail`, `length`, `isEmpty` — list operations
- [ ] `map`, `filter`, `fold`, `reduce` — higher-order list functions
- [ ] `sort`, `reverse`, `distinct` — list transformations
- [ ] `fetch` — HTTP GET request, returns `result<str, str>`
- [ ] `Json.parse`, `Json.stringify` — JSON serialization/deserialization
- [ ] `String.split`, `String.join`, `String.trim` — string operations
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

## Modules & Imports

- [ ] `import` statements
- [ ] `from ... import` statements
- [ ] Module-qualified access: `List.map`, `String.split`
- [ ] Module creation and exports

## Lexer / Parser

- [x] Context-sensitive tokenization (F# mode vs shell mode)
- [x] F# operator tokens: `+`, `-`, `*`, `/`, `%`, `**`, `|>`, `->`, `<-`
- [x] Nested parentheses in F# expressions (fixed: `))` no longer merges to `DblRndClose`)
- [x] `in`, `then`, `else` excluded from `isFSharpPrimary()` to prevent argument consumption
- [x] Comma tokenization in F# mode for tuples
- [x] Negative number literals: `-42`
- [x] Float literals with decimal: `3.14` (distinct `Float` type with arithmetic, comparisons, and promotion)
- [x] Hexadecimal: `0xFF`
- [x] Octal: `0o755`
- [x] Binary: `0b1010`
- [x] Scientific notation: `1e10`
- [x] Comments: `#`, `//`, `(* ... *)`

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
- [x] Fix `createAllocaInEntryBlock`: new `insertAfterAllocas()` method maintains alloca-prefix invariant (fixes 2-param recursive functions with object pattern matching)

### Phase 3 — List Standard Library (depends on Phase 2)
- [x] Basic: `head`, `tail`, `length`, `isEmpty` — native callbacks returning Option/List/int/bool
- [ ] Higher-order: `map`, `filter`, `fold`, `reduce` — IR-level codegen loops invoking function arguments
- [ ] Transformations: `sort`, `reverse`, `distinct`
- [x] `ListComprehensionExpr` codegen: forward iteration + optional filter + reverse for correct order
- [ ] Utility: `zip`, `flatten`, `groupBy`, `take`, `drop`, `find`, `exists`, `forall`

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
- [ ] Optional chaining `?.` — desugar to match on Option
- [ ] Option default `?|` — desugar to match with default value
- [ ] Option combinators: `Option.map`, `Option.bind`, `Option.defaultValue` as builtins
- [ ] For loop destructuring: `for (name, value) in entries do ... done`

### Phase 7 — String and File Standard Library (depends on Phase 2 for list returns)
- [ ] String: `split`, `join`, `trim`, `contains`, `startsWith`, `endsWith`, `toLower`, `toUpper`, `replace`
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
