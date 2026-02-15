# Union Fields

Demonstrates Endo's named union fields: defining variants with `of name: type` syntax, constructing values, dot-notation field access, pretty printing, and backward compatibility with unnamed fields.

```endo
// Discriminated unions with named fields and dot-access

// --- Basic named field ---

type Shape =
    | Circle of radius: int
    | Point

let c = Circle 42
print "c = "; println c
print "c.radius = "; println c.radius

let p = Point
print "p = "; println p

// --- Multi-field variant ---

type Rect =
    | Rect of w: int * h: int

let r = Rect (10, 20)
print "r = "; println r
print "r.w = "; println r.w
print "r.h = "; println r.h

// --- Mixed variants (named, unnamed, and unit) ---

type Foo =
    | A of x: int
    | B of int
    | C

let a = A 10
let b = B 99
let fc = C

print "a = "; println a
print "b = "; println b
print "fc = "; println fc

// --- Dot-access in expressions ---

let area = r.w * r.h
print "area = "; println area

let doubled = Circle (c.radius * 2)
print "doubled.radius = "; println doubled.radius

// --- Pattern matching still works ---

let describe_shape s =
    match s with
    | Circle radius -> $"circle with radius {radius}"
    | Point -> "a point"

println (describe_shape c)
println (describe_shape p)

// --- Practical example: area computation ---

type Figure =
    | Sq of side: int
    | Tri of base: int * height: int

let fig_area f =
    match f with
    | Sq s -> s * s
    | Tri (b, h) -> b * h / 2

let sq = Sq 5
let tri = Tri (6, 4)
print "sq area = "; println (fig_area sq)
print "tri area = "; println (fig_area tri)

// Dot-access for readability
print "sq.side = "; println sq.side
print "tri.base = "; println tri.base
print "tri.height = "; println tri.height
```

## Key Techniques

- **Named field syntax** uses `| Constructor of name: type` to give fields descriptive names within union variants.
- **Multi-field variants** combine named fields with tuple syntax: `| Rect of w: int * h: int`.
- **Dot-notation access** (`value.field`) reads individual fields directly, without pattern matching.
- **Pretty printing** distinguishes named fields (`Circle(radius: 42)`), unnamed fields (`Circle 42`), and unit variants (`Point`).
- **Mixed variants** allow named, unnamed, and unit variants in the same union type.
- **Backward compatibility** is preserved: pattern matching and unnamed fields work exactly as before.

## See Also

- [Type System](../language/type-system.md)
- [Pattern Matching](../language/pattern-matching.md)
- [Records](records.md)
