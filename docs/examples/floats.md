# Floats

Working with floating-point numbers in Endo: arithmetic, mixed-type operations, comparisons, and string coercion.

```endo
// Floating-point arithmetic and mixed-type operations

let pi = 3.14
println pi

// Basic arithmetic
print "1.5 + 2.5 = "; println (1.5 + 2.5)
print "5.0 - 2.5 = "; println (5.0 - 2.5)
print "3.0 * 2.5 = "; println (3.0 * 2.5)
print "7.0 / 2.0 = "; println (7.0 / 2.0)
print "7.5 % 2.0 = "; println (7.5 % 2.0)

// Exponentiation
print "2.0 ** 3.0 = "; println (2.0 ** 3.0)

// Negation
print "-3.14 = "; println (-3.14)

// Mixed int/float arithmetic (auto-promotes to float)
print "1 + 2.5 = "; println (1 + 2.5)
print "2.5 + 1 = "; println (2.5 + 1)

// Comparisons
print "2.5 < 3.0 ? "; println (if 2.5 < 3.0 then "yes" else "no")
print "3.0 > 2.5 ? "; println (if 3.0 > 2.5 then "yes" else "no")
print "2.5 == 2.5 ? "; println (if 2.5 == 2.5 then "yes" else "no")

// Float-to-string coercion in concatenation
println ("pi=" + 3.14)

// Functions with float arguments
let double x = x * 2.0
print "double 3.5 = "; println (double 3.5)

// Circle area
let radius = 5.0
let area = 3.14159 * radius * radius
print "area = "; println area
```

## Key Techniques

- **Float literals** use a decimal point (`3.14`, `2.0`). At least one digit must appear on each side of the decimal point.
- **All arithmetic operators** (`+`, `-`, `*`, `/`, `%`, `**`) work with floats, producing float results.
- **Mixed int/float arithmetic** automatically promotes the integer operand to float. The order does not matter -- `1 + 2.5` and `2.5 + 1` both produce a float result.
- **Comparisons** (`<`, `>`, `==`, `!=`, `<=`, `>=`) work with float operands and return boolean values.
- **String coercion** automatically converts floats to strings when concatenated with the `+` operator.
- **Negation** works with float literals (`-3.14`) and float variables.
- Functions taking float arguments work identically to integer functions -- Endo's type inference handles the distinction transparently.

## See Also

- [Type System](../language/type-system.md)
- [Lexical Elements](../language/lexical-elements.md)
- [Operators & Pipelines](../language/operators-and-pipelines.md)
