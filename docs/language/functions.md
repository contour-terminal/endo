# Functions

### 5.1 Named Functions (Curried)

Functions in endo are curried by default, meaning multi-parameter functions are actually chains of single-parameter functions.

<!-- endo-no-check -->
```endo
# Simple single-parameter function
let double x = x * 2
let greet name = println $"Hello, {name}"

# Multi-parameter functions (curried)
let add x y = x + y
let multiply x y z = x * y * z

# Call with all arguments
let sum = add 3 5             # 8
let product = multiply 2 3 4  # 24

# With type annotations
let add (x: int) (y: int): int = x + y
let format (template: str) (value: int): str = $"{template}: {value}"

# Partial application (supply fewer arguments)
let add5 = add 5              # add5: int -> int
let result = add5 10          # 15

let greetFormal = format "Dear"
let msg = greetFormal 42      # "Dear: 42"

# Partial application is powerful for pipelines
let multiplyBy n = multiply n
let times10 = multiplyBy 10

[1; 2; 3] |> map (add 1)      # [2; 3; 4]
[1; 2; 3] |> map times10      # [10; 20; 30]
```

### 5.1.1 Unit Parameter

Functions that take no meaningful input use the unit parameter `()` to indicate they are called for their side effects.

```endo
# Side-effecting function with unit parameter
let greet () = print "hello"
greet ()
```

```endo
# Unit parameter with return type annotation
let answer (): int = 42
print (answer ())
```

```endo
# Unit parameter with closures
let x = 10
let getX () = x
print (getX ())
```

```endo
# Unit parameter in lambdas
let f = fun () -> 42
print (f ())
```

Unit parameters can be mixed with regular parameters — the `()` simply occupies one parameter position:

<!-- endo-no-check -->
```endo
let greetAndAdd () (x: int) = x + 1
let result = greetAndAdd () 5     # 6
```

### 5.2 Multi-line Functions

<!-- endo-no-check -->
```endo
# Indentation-based body
let factorial n =
    match n with
    | 0 -> 1
    | 1 -> 1
    | n -> n * factorial (n - 1)

# Brace-based body
let fibonacci n = {
    let mut a = 0
    let mut b = 1
    for _ in 1..n do
        let temp = a
        a <- b
        b <- temp + b
    end
    a
}

# Mixed shell and functional
let findLargeFiles dir minSize =
    find $dir -size +$"{minSize}M"
    |> lines
    |> filter (fun f -> test -f $f)
    |> map (fun f -> { path = f; size = stat -c%s $f })

# Multiple statements in function body
let processAndLog input =
    let processed = transform input
    log $"Processed: {processed}"
    let validated = validate processed
    log $"Validated: {validated}"
    validated
```

### 5.3 Lambda Expressions

Anonymous functions for inline use.

<!-- endo-no-check -->
```endo
# Basic lambda syntax: fun params -> body
let double = fun x -> x * 2
let add = fun x y -> x + y

# Lambdas in higher-order functions
[1; 2; 3] |> map (fun x -> x * 2)              # [2; 4; 6]
[1; 2; 3; 4] |> filter (fun x -> x % 2 == 0)   # [2; 4]
[1; 2; 3] |> fold 0 (fun acc x -> acc + x)     # 6

# Multi-line lambda with braces
let process = fun x -> {
    let temp = x * 2
    let adjusted = temp + 1
    adjusted
}

# Type-annotated lambda
let typedFn: (int -> int) = fun x -> x * 2
let annotatedLambda = fun (x: int) (y: int) -> x + y

# Lambdas capturing outer scope
let multiplier = 10
let scale = fun x -> x * multiplier    # Captures 'multiplier'

# Nested lambdas (currying manually)
let curriedAdd = fun x -> fun y -> x + y
let add5 = curriedAdd 5
```

### 5.4 Placeholder Lambda Sugar (`_`)

The `_` token in expression position (not pattern position) creates an implicit single-parameter lambda. This is purely a parser-level desugaring — no changes to IR or runtime.

<!-- endo-no-check -->
```endo
# Field accessor
_.pid                         # -> fun __x -> __x.pid

# Predicate
_.name == "endo"              # -> fun __x -> __x.name == "endo"

# Comparison
_.cpu > 50.0                  # -> fun __x -> __x.cpu > 50.0

# Arithmetic
_ + 1                         # -> fun __x -> __x + 1

# In pipelines (most common use)
ps |> filter (_.name == "endo") |> map _.pid
ps |> sortBy _.cpu |> groupBy _.user

# Multiple _ refers to the same parameter
_ + _                         # -> fun __x -> __x + __x
```

**Rule:** Any expression containing `_` in expression position (outside of pattern context such as `match` arms or `let` destructuring) creates an implicit lambda. The `_` becomes the single parameter. This sugar works anywhere a function value is expected.

### 5.5 Recursive Functions

<!-- endo-no-check -->
```endo
# The 'rec' keyword enables recursion
let rec gcd a b =
    match b with
    | 0 -> a
    | _ -> gcd b (a % b)

let rec sumList acc lst =
    match lst with
    | [] -> acc
    | head :: tail -> sumList (acc + head) tail

# Mutual recursion with 'and'
let rec isEven n =
    match n with
    | 0 -> true
    | n -> isOdd (n - 1)
and isOdd n =
    match n with
    | 0 -> false
    | n -> isEven (n - 1)

# Tail-recursive with accumulator (efficient)
let factorial n =
    let rec loop acc n =
        match n with
        | 0 -> acc
        | n -> loop (acc * n) (n - 1)
    loop 1 n

# Tree traversal
type Tree<T> =
    | Leaf of T
    | Node of Tree<T> * Tree<T>

let rec sumTree tree =
    match tree with
    | Leaf n -> n
    | Node (left, right) -> sumTree left + sumTree right
```

> **Note:** Endo supports both tail-recursive and non-tail-recursive functions.
> Non-tail calls like `n * factorial (n - 1)` work correctly — the compiler
> uses type inference to determine parameter types and compiles recursive
> functions with proper call stack support.

### 5.6 Function Composition

<!-- endo-no-check -->
```endo
# Forward composition operator >>
let doubleAndAdd1 = double >> (add 1)
let result = doubleAndAdd1 5           # (5*2)+1 = 11

# Backward composition operator <<
let add1AndDouble = (add 1) << double
let result = add1AndDouble 5           # (5+1)*2 = 12

# Building pipelines with composition
let processNumbers =
    filter isPositive
    >> map double
    >> fold 0 add

let result = processNumbers [(-1); 2; (-3); 4; 5]    # 22

# Point-free style
let normalizeAndCount =
    trim
    >> toLower
    >> words
    >> length

let wordCount = normalizeAndCount "  Hello World  "  # 2
```

---
**See also:** [Variables & Bindings](variables-and-bindings.md) | [Operators & Pipelines](operators-and-pipelines.md) | [Pattern Matching](pattern-matching.md)
