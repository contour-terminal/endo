# Validation

Shows input validation patterns using `Result` types, `try-with` error recovery, the `?` propagation operator, multi-arm error dispatch, and guarded error handling.

```endo
// Validation patterns using Result types, try-with, and the ? operator

let validate_positive x =
    if x > 0 then Ok x
    else Error x

let show_result r =
    match r with
    | Ok n -> "Ok(positive): " + n
    | Error e -> "Error(not positive): " + e

println (show_result (validate_positive 5))
println (show_result (validate_positive -3))

// try-with recovers from Error by providing a default
let safe_validate x =
    try validate_positive x with
    | Error e -> 0

print "safe_validate 10 = "; println (safe_validate 10)
print "safe_validate -5 = "; println (safe_validate (0 - 5))

// The ? operator propagates errors early
let process x = (validate_positive x)?

let r1 = process 42
let r2 = process (0 - 1)
println (show_result r1)
println (show_result r2)

// Multi-arm try-with for error dispatch
let getErr x = Error x

let multi_check code =
    try getErr code with
    | Error 1 -> 10
    | Error 2 -> 20
    | Error _ -> 0

print "Error 1 = "; println (multi_check 1)
print "Error 2 = "; println (multi_check 2)
print "Error 99 = "; println (multi_check 99)

// Guards in try-with arms
let guard_check code =
    try getErr code with
    | Error e when e > 100 -> 1
    | Error e when e > 10 -> 2
    | Error _ -> 3

print "Error 500 (>100) = "; println (guard_check 500)
print "Error 50 (>10) = "; println (guard_check 50)
print "Error 5 (other) = "; println (guard_check 5)

// Option handling with try-with
let getOpt x = Some x
let r3 = try getOpt 42 with | None -> 0
print "try Some 42 = "; println r3

let getNone x = None
let r4 = try getNone 0 with | None -> 99
print "try None = "; println r4
```

## Key Techniques

- **Validation functions** return `Result` types: `Ok value` for valid input, `Error value` for invalid input. This makes the success/failure contract explicit in the function signature.
- **`try-with` recovery** provides a default value when validation fails, keeping the calling code clean. The error value is available in the `with` arm for inspection.
- **The `?` operator** propagates validation errors upward. If `validate_positive x` returns `Error`, the `?` causes the enclosing function to return that error immediately.
- **Multi-arm error dispatch** matches different error values to different recovery strategies. This is useful when errors carry codes or categories.
- **Guarded error arms** use `when` conditions to classify errors by range or severity rather than exact value.
- **Option handling** works identically to Result handling in `try-with` -- `None` plays the role of `Error` for values that may be absent.
- This pattern -- validate, propagate errors, recover at boundaries -- is the idiomatic way to handle errors in Endo without exceptions or null values.

## See Also

- [Error Handling](../language/error-handling.md)
- [Type System](../language/type-system.md)
- [Pattern Matching](../language/pattern-matching.md)
