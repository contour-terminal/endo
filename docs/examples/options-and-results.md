# Options and Results

Covers Endo's approach to safe value handling using `Option` types (`Some`/`None`), `Result` types (`Ok`/`Error`), the `?` propagation operator, and `try-with` error recovery.

```endo
// Option and Result types for safe value handling

// Option: Some wraps a value, None represents absence
let some_val = Some 42
let no_val = None

let get_or_default opt =
    match opt with
    | Some n -> n
    | None -> 0

print "Some 42 -> "; println (get_or_default some_val)
print "None -> "; println (get_or_default no_val)

// Result: Ok wraps success, Error wraps failure
let ok_val = Ok 100
let err_val = Error 404

let unwrap_result r =
    match r with
    | Ok n -> n
    | Error e -> e

print "Ok 100 -> "; println (unwrap_result ok_val)
print "Error 404 -> "; println (unwrap_result err_val)

// The ? operator propagates errors (unwraps Ok/Some, returns Error/None)
let unwrap opt = opt?
let r1 = unwrap (Some 42)
print "unwrap Some 42 = "; println r1

let unwrap_res res = res?
let r2 = unwrap_res (Ok 77)
print "unwrap Ok 77 = "; println r2

// try-with for error recovery
let getValue x = Ok x
let safe = try getValue 42 with | Error e -> 0
print "try Ok 42 = "; println safe

let getErr x = Error x
let handled = try getErr 99 with | Error e -> e
print "try Error 99 = "; println handled

// Multi-arm try-with for specific error codes
let r3 = try getErr 1 with
    | Error 1 -> 10
    | Error 2 -> 20
    | Error _ -> 0
print "Error 1 matched = "; println r3

let r4 = try getErr 2 with
    | Error 1 -> 10
    | Error 2 -> 20
    | Error _ -> 0
print "Error 2 matched = "; println r4

let r5 = try getErr 99 with
    | Error 1 -> 10
    | Error 2 -> 20
    | Error _ -> 0
print "Error 99 fallback = "; println r5

// Guards in try-with arms
let r6 = try getErr 5 with
    | Error e when e > 3 -> 100
    | Error _ -> 0
print "Error 5 (>3 guard) = "; println r6

// Option handling with try-with
let getOpt x = Some x
let r7 = try getOpt 42 with | None -> 0
print "try Some 42 = "; println r7

let getNone x = None
let r8 = try getNone 0 with | None -> 99
print "try None = "; println r8
```

## Real-World Example: `which` Builtin

The `which` builtin returns `Option<string>` in F# mode, making it a natural fit for
option handling patterns:

```endo
// Check if a tool is available and get its path
match which "git" with
| Some path -> println $"git found at: {path}"
| None -> println "git is not installed"

// Provide a fallback with the ?| operator
let editor = which "nvim" ?| "/usr/bin/vi"
println editor
```

## Key Techniques

- **`Option` type** represents values that may or may not exist. `Some value` wraps a present value; `None` represents absence. This eliminates null pointer errors by forcing explicit handling.
- **`Result` type** represents operations that may succeed or fail. `Ok value` wraps a success; `Error value` wraps a failure with an error payload.
- **Pattern matching** on `Option` and `Result` ensures both cases are handled. Match arms destructure the wrappers to extract inner values.
- **The `?` operator** provides concise error propagation. Applied to an `Ok` or `Some`, it unwraps the inner value. Applied to an `Error` or `None`, it immediately returns from the enclosing function with that error.
- **`try-with` expressions** provide structured error recovery. The `try` block evaluates an expression, and if it produces an `Error` (or `None`), the `with` arms handle it.
- **Multi-arm `try-with`** matches specific error values, enabling dispatch on error codes or error types.
- **Guards in `try-with`** use `when` conditions to refine error matching, just like in regular match expressions.

## See Also

- [Error Handling](../language/error-handling.md)
- [Type System](../language/type-system.md)
- [Pattern Matching](../language/pattern-matching.md)
