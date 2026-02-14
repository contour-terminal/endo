# Temperature

A practical example showing temperature conversion functions, pipeline usage, classification with if-then-else chains, and function composition via let-in bindings.

```endo
// Temperature conversion and classification

let celsius_to_fahrenheit c = c * 9 / 5 + 32
let fahrenheit_to_celsius f = (f - 32) * 5 / 9

print "0C = "; print (celsius_to_fahrenheit 0); println "F"
print "100C = "; print (celsius_to_fahrenheit 100); println "F"
print "32F = "; print (fahrenheit_to_celsius 32); println "C"
print "212F = "; print (fahrenheit_to_celsius 212); println "C"

// Pipeline: pass value through conversion
print "37C = "; print (37 |> celsius_to_fahrenheit); println "F"

// Classify temperature into human-readable categories
let classify_temp c =
    if c < 0 then "freezing"
    else if c < 15 then "cold"
    else if c < 25 then "comfortable"
    else if c < 35 then "warm"
    else "hot"

print "Classify -5C: "; println (classify_temp (0 - 5))
print "Classify 10C: "; println (classify_temp 10)
print "Classify 22C: "; println (classify_temp 22)
print "Classify 30C: "; println (classify_temp 30)
print "Classify 40C: "; println (classify_temp 40)

// Compose conversion + classification into a description
let describe_temp c =
    let f = celsius_to_fahrenheit c in
    let cat = classify_temp c in
    string_of_int c + "C (" + string_of_int f + "F) is " + cat

println (describe_temp 20)
println (describe_temp (0 - 10))
```

## Key Techniques

- **Pure conversion functions** (`celsius_to_fahrenheit`, `fahrenheit_to_celsius`) take a value and return a transformed value with no side effects.
- **Pipelines** (`37 |> celsius_to_fahrenheit`) pass a value directly into a function, making the data flow direction explicit.
- **Chained if-then-else** provides multi-branch classification. Each branch checks a condition and returns a string label.
- **Let-in composition** in `describe_temp` computes intermediate values (`f` and `cat`) in scoped bindings, then combines them into a final string. This keeps the function body clean and readable.
- **`string_of_int`** converts integer values to strings for concatenation in the description builder.
- This example demonstrates how small, focused functions can be composed together to build richer behavior -- a core principle of functional programming.

## See Also

- [Functions](../language/functions.md)
- [Control Flow](../language/control-flow.md)
- [Operators & Pipelines](../language/operators-and-pipelines.md)
