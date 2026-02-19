# Properties

Computed properties provide variable-like syntax backed by custom get/set logic.

## Read-Only Property

```endo
let mut _count = 0
let Count with get () = _count
print Count
# => 0
```

## Read-Write Property

```endo
let mut _value = 10
let Value with
    get () = _value
    and set (v) = _value <- v
print Value
Value <- 42
print Value
# => 1042
```

## Multi-Line Getter

Property bodies support multiple expressions, just like function bodies.
Indent the body further than the `let` keyword.

```endo
let mut _x = 10
let Double with
    get () =
        let v = _x
        v * 2
print Double
# => 20
```

## Multi-Line Setter

```endo
let mut _a = 0
let mut _b = 0
let SetBoth with
    set (v) =
        _a <- v
        _b <- v * 2
SetBoth <- 5
print _a
print _b
# => 510
```

## Multi-Line Getter and Setter

```endo
let mut _val = 0
let Val with
    get () =
        let r = _val
        r
    and set (v) =
        _val <- v
Val <- 99
print Val
# => 99
```

## `with` on Next Line

The `with` keyword can appear on the line after the property name:

```endo
let mut _x = 0
let X
    with get () = _x
    and set (v) = _x <- v
X <- 77
print X
# => 77
```

## Conditional Getter

```endo
let mut _x = 0
let Abs with
    get () =
        if _x >= 0 then _x
        else 0 - _x
_x <- 0 - 7
print Abs
# => 7
```

## Built-In Dot Properties

Endo provides built-in dot properties on common types:

```endo
# List length
let xs = [1; 2; 3]
println xs.length
# => 3

# Option queries
let x = Some 42
println x.isSome
println x.isNone
# => true
# => false

# Tuple access
let t = (10, 20)
println t.0
println t.1
# => 10
# => 20
```

## Key Techniques

- **Single-line** bodies go directly after `=`: `get () = expr`
- **Multi-line** bodies start on the next line, indented past the `let` keyword
- Use `and` to combine get and set accessors in either order
- Write-only properties omit the getter; read-only properties omit the setter
- Properties are accessed like variables: read with the name, write with `<-`

## See Also

- [Variables & Bindings](../language/variables-and-bindings.md) — property syntax reference (section 4.3.1)
- [Type System](../language/type-system.md) — built-in dot properties on tuples, options, results
- [Lists & Collections](../language/lists-and-collections.md) — list and string dot properties
