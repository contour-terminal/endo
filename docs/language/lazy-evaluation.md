# Lazy Evaluation

Lazy evaluation allows deferring computation until the result is actually needed. An expression wrapped in `lazy` is not evaluated immediately — instead, it creates a _thunk_ (a deferred computation). The thunk is only evaluated when `force` is called on it, and the result is cached (memoized) so subsequent `force` calls return the cached value without re-evaluation.

## 12.1 Overview

| Concept | Description |
|---------|-------------|
| `lazy expr` | Creates a deferred computation |
| `force v` | Evaluates the thunk and caches the result |
| Memoization | Once forced, the result is cached permanently |
| Captures | Variables from enclosing scope are captured at creation time |

Use cases:
- **Expensive computations** that may not always be needed
- **Conditional evaluation** — avoid computing values that are only used in certain branches
- **Caching** — compute once, use many times

## 12.2 Creating Lazy Values

```endo
# Simple lazy value
let x = lazy 42

# Lazy computation
let expensive = lazy (computeHeavyResult data)

# Lazy values capture variables from enclosing scope
let base = 100
let multiplier = 3
let computed = lazy (base * multiplier)
```

The expression after `lazy` is not evaluated at binding time. It is stored as a thunk along with any captured variables from the enclosing scope.

## 12.3 Forcing Lazy Values

Use `force` to evaluate a lazy value:

```endo
# Direct force
let x = lazy (1 + 2)
println (force x)          # => 3

# Pipeline force
let x = lazy 42
x |> force |> println      # => 42
```

## 12.4 Memoization Semantics

The first `force` evaluates the deferred expression and caches the result. Subsequent `force` calls return the cached value without re-evaluation:

```endo
let x = lazy (1 + 2)
let a = force x          # Evaluates the expression → 3
let b = force x          # Returns cached result → 3 (no re-evaluation)
# a == b == 3
```

## 12.5 Captures and Closures

Variables referenced in the lazy body are captured at creation time:

```endo
let a = 10
let b = 20
let x = lazy (a + b)
println (force x)           # => 30
```

Captured values are copies taken at the point where `lazy` is evaluated. Later changes to the original bindings do not affect the lazy value.

## 12.6 Practical Examples

```endo
# Avoid expensive computation when not needed
let config = lazy (parseConfig (cat "config.yml"))
if needsConfig then
    let c = force config
    applyConfig c

# Multiple lazy values with conditional forcing
let data = lazy (fetchRemoteData url)
let cachedData = lazy (readCache cacheFile)

let result =
    if cacheExists cacheFile
    then force cachedData
    else force data
```

---

**See also:** [Variables & Bindings](variables-and-bindings.md) | [Functions](functions.md) | [Error Handling](error-handling.md)
