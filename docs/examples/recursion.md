# Recursion

Explores recursive functions in Endo, including tail-call optimized recursion, mutual recursion with `and`, and classic algorithms like factorial, Fibonacci, and GCD.

```endo
// Recursive functions with tail-call optimization

// Factorial with accumulator
let rec factorial n acc =
    match n with
    | 0 -> acc
    | _ -> factorial (n - 1) (n * acc)

print "5! = "; println (factorial 5 1)
print "10! = "; println (factorial 10 1)

// Fibonacci with two accumulators
let rec fib n a b =
    match n with
    | 0 -> a
    | _ -> fib (n - 1) b (a + b)

print "fib(10) = "; println (fib 10 0 1)
print "fib(20) = "; println (fib 20 0 1)

// Greatest common divisor (Euclidean algorithm)
let rec gcd a b =
    match b with
    | 0 -> a
    | _ -> gcd b (a % b)

print "gcd(48, 18) = "; println (gcd 48 18)
print "gcd(100, 75) = "; println (gcd 100 75)

// Sum 1..n
let rec sum n acc =
    match n with
    | 0 -> acc
    | _ -> sum (n - 1) (acc + n)

print "sum(1..100) = "; println (sum 100 0)

// Countdown to zero (tests deep tail recursion)
let rec countdown n =
    match n with
    | 0 -> 0
    | _ -> countdown (n - 1)

print "countdown(1000) = "; println (countdown 1000)

// Mutual recursion: `and` links two functions into a joint definition
let rec isEven n =
    match n with
    | 0 -> 1
    | _ -> isOdd (n - 1)
and isOdd n =
    match n with
    | 0 -> 0
    | _ -> isEven (n - 1)

print "isEven(4) = "; println (isEven 4)
print "isEven(7) = "; println (isEven 7)
print "isOdd(5) = "; println (isOdd 5)
print "isOdd(8) = "; println (isOdd 8)

// Recursion with if-then-else instead of match
let rec fact n acc =
    if n <= 1 then acc
    else fact (n - 1) (acc * n)

print "fact(6) = "; println (fact 6 1)

// Pipeline into a recursive function
print "countdown via pipe = "; println (10 |> countdown)
```

## Key Techniques

- **`let rec`** declares a recursive function. Without `rec`, the function name is not in scope within its own body.
- **Accumulator pattern** transforms naive recursion into tail recursion. Instead of building up a chain of deferred operations, results are accumulated in an extra parameter (e.g., `factorial n acc` instead of `n * factorial (n-1)`).
- **Tail-call optimization** -- when the recursive call is in tail position (the very last operation), Endo optimizes it into a loop, preventing stack overflow even for deep recursion (demonstrated by `countdown 1000`).
- **Mutual recursion** uses `and` to link two or more functions that call each other. Both `isEven` and `isOdd` are defined together and can reference each other freely.
- **Pattern matching in recursion** is the idiomatic way to define base cases (`| 0 -> acc`) and recursive cases (`| _ -> recurse ...`).
- **`if-then-else`** can be used instead of match for simple two-branch recursion, as shown in the `fact` function.
- Recursive functions are first-class values and work with **pipelines** just like any other function.

## See Also

- [Functions](../language/functions.md)
- [Pattern Matching](../language/pattern-matching.md)
- [Control Flow](../language/control-flow.md)
