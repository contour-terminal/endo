# Lexical Elements

### 2.1 Keywords

```
let       mut       fun       type      match     with
when      if        then      else      elif      end
for       in        do        while     try       finally
return    break     continue  export    true      false
Ok        Error     Some      None      rec       and
of        as        global    lazy
```

### 2.2 Reserved Operators

```
|>        |         ->        <-        =>        ::
&&        ||        ==        !=        <=        >=
<         >         +         -         *         /        %
**        >>        <<        @         #         ?        !
```

### 2.3 Delimiters

```
( )       [ ]       { }       ;         ,         .
```

### 2.4 Comments

```endo
# Single line comment (shell style)
// Single line comment (C style)
(*
   Multi-line comment
   F# style
*)
```

### 2.5 String Literals

<!-- endo-no-check -->
```endo
# Double-quoted strings (with interpolation)
"Hello, $name"
"Value: ${expression}"
"Command output: $(whoami)"
"Arithmetic: $((1 + 2))"

# Single-quoted strings (literal, no interpolation)
'No $interpolation here'
'Literal backslash: \'

# Escape sequences in double-quoted strings
"\n"      # Newline
"\t"      # Tab
"\\"      # Backslash
"\$"      # Literal dollar sign
"\""      # Literal double quote

# F#-style interpolated strings (expression holes with {expr})
$"Hello, {name}"
$"Sum is {3 + 4}"
$"a={a}, b={b}"
$"result: {f 5}"
$"val: {if x > 0 then "positive" else "negative"}"

# Escaped braces in F#-style interpolated strings
$"{{literal braces}}"   # produces: {literal braces}
```

### 2.6 Numeric Literals

```endo
# Integers
42
-17
0xFF        # Hexadecimal
0o755       # Octal
0b1010      # Binary

# Floating point
3.14
-0.5
1e10
2.5e-3
```

### 2.7 Size Literals

Size literals represent byte counts and produce a `Size` value.
They are formed by an integer or float followed by a unit suffix:

```endo
42B         # 42 bytes
1KB         # 1 kilobyte  (1024 bytes)
5MB         # 5 megabytes (5 * 1024^2 bytes)
2GB         # 2 gigabytes (2 * 1024^3 bytes)
1TB         # 1 terabyte  (1 * 1024^4 bytes)
```

Float values are supported for all units except `B` (which requires whole numbers):

```endo
3.5KB       # 3584 bytes (3.5 * 1024)
1.5MB       # 1572864 bytes (1.5 * 1024^2)
1.0B        # 1 byte (whole float OK)
```

Size values display in the most appropriate unit (e.g., `1536` bytes displays as `1.5 KB`).
The raw byte count is accessible via the `.bytes` field:

```endo
let s = 10MB
print s           # 10 MB
print s.bytes     # 10485760
```

Size values support comparison operators:

<!-- endo-no-check -->
```endo
1KB < 1MB       # true
1KB == Size.fromBytes 1024  # true
```

### 2.8 TimeSpan Literals

TimeSpan literals represent durations and produce a `TimeSpan` value.
They are formed by an integer or float followed by a unit suffix:

```endo
100ms       # 100 milliseconds
5s          # 5 seconds (5000 ms)
2min        # 2 minutes (120000 ms)
1h          # 1 hour (3600000 ms)
```

Float values are supported for all units except `ms` (which requires whole numbers):

```endo
1.5h        # 1h 30m (5400000 ms)
2.5s        # 2s 500ms (2500 ms)
1.0ms       # 1ms (whole float OK)
```

The raw millisecond count is accessible via the `.milliseconds` field:

```endo
let t = 5s
print t               # 5s
print t.milliseconds  # 5000
```

TimeSpan values support comparison operators:

<!-- endo-no-check -->
```endo
1s < 1min   # true
```

---
**See also:** [Type System](type-system.md) | [Grammar](grammar.md) | [Philosophy & Goals](index.md)
