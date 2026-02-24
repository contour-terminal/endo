# Lexical Elements

### 2.1 Keywords

```
let       mut       fun       type      match     with
when      if        then      else      elif      end
for       in        do        while     try       finally
return    break     continue  export    true      false
Ok        Error     Some      None      rec       and
of        as        global
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
They are formed by an integer followed by a unit suffix:

```endo
42_B        # 42 bytes
1_KB        # 1 kilobyte  (1024 bytes)
5_MB        # 5 megabytes (5 * 1024^2 bytes)
2_GB        # 2 gigabytes (2 * 1024^3 bytes)
1_TB        # 1 terabyte  (1 * 1024^4 bytes)
```

Size values display in the most appropriate unit (e.g., `1536` bytes displays as `1.5 KB`).
The raw byte count is accessible via the `.bytes` field:

```endo
let s = 10_MB
print s           # 10 MB
print s.bytes     # 10485760
```

Size values support comparison operators:

<!-- endo-no-check -->
```endo
1_KB < 1_MB       # true
1_KB == Size.fromBytes 1024  # true
```

---
**See also:** [Type System](type-system.md) | [Grammar](grammar.md) | [Philosophy & Goals](index.md)
