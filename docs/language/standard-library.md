# Standard Library Reference

This page documents all built-in functions available in Endo. Functions are grouped
by category. Each example is executable and verified by the documentation test suite.

### Contents

- [15.1 Output](#151-output) -- `print`, `println`
- [15.2 Type Conversion](#152-type-conversion) -- `string_length`, `int_of_string`, `string_of_int`, `string`, `not`
- [15.3 String Operations](#153-string-operations) -- `trim`, `toLower`, `toUpper`, `contains`, `startsWith`, `endsWith`, `replace`, `split`, `join`, `*`
- [15.4 List -- Basic Operations](#154-list-basic-operations) -- `head`, `tail`, `length`, `isEmpty`, `nth`, `last`, `replicate`
- [15.5 List -- Higher-Order Functions](#155-list-higher-order-functions) -- `map`, `filter`, `fold`, `reduce`, `find`, `exists`, `forall`, `each`
- [15.6 List -- Transformations](#156-list-transformations) -- `sort`, `reverse`, `distinct`, `sortBy`, `groupBy`, `take`, `drop`, `zip`, `flatten`
- [15.7 List -- Operators](#157-list-operators) -- `::`, `@`
- [15.8 Option Combinators](#158-option-combinators) -- `Option.map`, `Option.bind`, `Option.defaultValue`
- [15.9 Environment & System](#159-environment-system) -- `env`, `which`
- [15.10 Random](#1510-random) -- `rand`
- [15.11 Number Formatting](#1511-number-formatting) -- `formatNumber`
- [15.12 Composition Examples](#1512-composition-examples)
- [15.13 Size Type](#1513-size-type) -- `Size.fromBytes`, `Size.fromKB`, `Size.fromMB`, `Size.fromGB`, `Size.fromTB`, size literals
- [15.14 DateTime Type](#1514-datetime-type) -- `DateTime.now`, `DateTime.fromEpoch`, `formatDateTime`
- [15.15 TimeSpan Type](#1515-timespan-type) -- `TimeSpan.fromMilliseconds`, `TimeSpan.fromSeconds`, `TimeSpan.fromMinutes`, `TimeSpan.fromHours`, `TimeSpan.fromDays`, `sleep`, `formatTimeSpan`
- [15.16 Shell Data Commands](#1516-shell-data-commands) -- `ls`, `ps`, `jobs`
- [15.17 FileMode Type](#1517-filemode-type) -- `FileMode.fromBits`, `.isReadable`, `.isWritable`, `.isExecutable`, `.owner`, `.group`, `.other`
- [15.18 File Permission Helpers](#1518-file-permission-helpers) -- `formatMode`, `isReadable`, `isWritable`, `isExecutable`
- [15.19 Display Formatting](#1519-display-formatting) -- `toText`, `string`
- [15.20 HTTP](#1520-http) -- `fetch`
- [15.21 Timing](#1521-timing) -- `time`
- [15.22 JSON](#1522-json) -- `Json.query`
- [15.23 Lazy Evaluation](#1523-lazy-evaluation) -- `force`
- [15.24 Lazy Sequences](#1524-lazy-sequences) -- `seq`, `yield`, `yield!`, `toList`
- [15.25 Path](#1525-path) -- `Path.temporary_directory`
- [15.26 File I/O](#1526-file-io) -- `File.open`, `File.close`, `File.readLine`, `File.readAll`, `File.writeAll`, `File.appendAll`, `File.size`, `File.exists`, `File.delete`
- [15.27 Resource Management](#1527-resource-management) -- `let use`, `let manual`

---

## 15.1 Output

#### `print`

**Signature:** `print value`

Prints a value to stdout without a trailing newline.

```endo
print 42   # => 42
```

```endo
print "hello"   # => hello
```

#### `println`

**Signature:** `println value`

Prints a value to stdout followed by a newline.

```endo
println "hello"   # => hello
```

---

## 15.2 Type Conversion

#### `string_length`

**Signature:** `string_length str : int`

Returns the number of characters in a string.

```endo
print (string_length "hello")   # => 5
```

```endo
print (string_length "")   # => 0
```

#### `int_of_string`

**Signature:** `int_of_string str : int`

Converts a string to an integer.

```endo
let n = int_of_string "7"
print (n + 3)   # => 10
```

#### `string_of_int`

**Signature:** `string_of_int n : str`

Converts an integer to its string representation.

```endo
print (string_of_int 42)   # => 42
```

```endo
let r = 42 |> string_of_int |> string_length
print r   # => 2
```

#### `string`

**Signature:** `string value : str`

Universal conversion to string. Works with integers, floats, booleans, and strings (passthrough).

```endo
println (string 42)      # => 42
println (string 3.14)    # => 3.14
println (string true)    # => true
println (string "hi")    # => hi
```

```endo
42 |> string |> println   # => 42
```

#### `not`

**Signature:** `not value : bool`

Boolean negation.

```endo
let x = not true
print x   # => false
```

```endo
let x = not false
print x   # => true
```

---

## 15.3 String Operations

#### `trim`

**Signature:** `trim str : str`

Removes leading and trailing whitespace.

```endo
print (trim "  hello  ")   # => hello
```

```endo
print (trim "   ")
```

```endo
print ("  hello  " |> trim)   # => hello
```

#### `toLower`

**Signature:** `toLower str : str`

Converts a string to lowercase.

```endo
print (toLower "HELLO")   # => hello
```

```endo
print ("HELLO" |> toLower)   # => hello
```

#### `toUpper`

**Signature:** `toUpper str : str`

Converts a string to uppercase.

```endo
print (toUpper "hello")   # => HELLO
```

```endo
print ("hello" |> toUpper)   # => HELLO
```

#### `contains`

**Signature:** `contains substr text : bool`

Returns `true` if `text` contains the substring `substr`.

```endo
print (contains "world" "hello world")   # => true
```

```endo
print (contains "xyz" "hello")   # => false
```

```endo
print ("hello world" |> contains "world")   # => true
```

#### `startsWith`

**Signature:** `startsWith prefix text : bool`

Returns `true` if `text` starts with the given `prefix`.

```endo
print (startsWith "hel" "hello")   # => true
```

```endo
print (startsWith "xyz" "hello")   # => false
```

```endo
print ("hello" |> startsWith "hel")   # => true
```

#### `endsWith`

**Signature:** `endsWith suffix text : bool`

Returns `true` if `text` ends with the given `suffix`.

```endo
print (endsWith "llo" "hello")   # => true
```

```endo
print (endsWith "xyz" "hello")   # => false
```

```endo
print ("hello" |> endsWith "llo")   # => true
```

#### `replace`

**Signature:** `replace old new str : str`

Replaces all occurrences of `old` with `new` in the string.

```endo
print (replace "l" "r" "hello")   # => herro
```

```endo
print (replace "," "" "a,b,c")   # => abc
```

```endo
print ("a,b,c" |> replace "," "-")   # => a-b-c
```

#### `split`

**Signature:** `split separator str : list<str>`

Splits a string by the given separator, returning a list of substrings.

```endo
let parts = split "," "a,b,c"
print (length parts)   # => 3
```

```endo
let parts = "a:b:c" |> split ":"
print (length parts)   # => 3
```

```endo
print ("a:b:c" |> split ":" |> join "-")   # => a-b-c
```

#### `join`

**Signature:** `join separator list : str`

Joins a list of strings with the given separator.

```endo
print (join "-" (split "," "a,b,c"))   # => a-b-c
```

```endo
let r = ["a"; "b"; "c"] |> join "-"
print r   # => a-b-c
```

```endo
print (join "," [])
```

#### String Repetition (`*`)

Repeats a string a given number of times.

```endo
print ("ha" * 3)   # => hahaha
```

```endo
print (3 * "ab")   # => ababab
```

---

## 15.4 List -- Basic Operations

#### `head`

**Signature:** `head list : option<T>`

Returns the first element wrapped in `Some`, or `None` for an empty list.

```endo
let r = match head [1; 2; 3] with
  | Some v -> v
  | None -> -1
print r   # => 1
```

```endo
let r = match head [] with
  | Some v -> v
  | None -> -1
print r   # => -1
```

#### `tail`

**Signature:** `tail list : list<T>`

Returns all elements except the first. Returns `[]` for an empty list.

```endo
print (tail [1; 2; 3])   # => [2; 3]
```

```endo
print (tail [])   # => []
```

```endo
print (tail [1])   # => []
```

#### `length`

**Signature:** `length list : int`

Returns the number of elements in a list.

```endo
print (length [1; 2; 3])   # => 3
```

```endo
print (length [])   # => 0
```

#### `isEmpty`

**Signature:** `isEmpty list : bool`

Returns `true` if the list has no elements.

```endo
print (isEmpty [])   # => true
```

```endo
print (isEmpty [1; 2])   # => false
```

#### `nth`

**Signature:** `nth index list : option<T>`

Returns the element at the given zero-based index, wrapped in `Some`. Returns `None` if out of bounds.

```endo
let r = match nth 1 [10; 20; 30] with
  | Some v -> v
  | None -> -1
print r   # => 20
```

```endo
let r = match nth 5 [10; 20; 30] with
  | Some v -> v
  | None -> -1
print r   # => -1
```

#### `last`

**Signature:** `last list : option<T>`

Returns the last element wrapped in `Some`, or `None` for an empty list.

```endo
let r = match last [1; 2; 3] with
  | Some v -> v
  | None -> -1
print r   # => 3
```

```endo
let r = match last [] with
  | Some v -> v
  | None -> -1
print r   # => -1
```

#### `replicate`

**Signature:** `replicate n value : list<T>`

Creates a list of `n` copies of the given value.

```endo
print (replicate 3 42)   # => [42; 42; 42]
```

```endo
print (replicate 0 1)   # => []
```

---

## 15.5 List -- Higher-Order Functions

#### `map`

**Signature:** `map f list : list<U>`

Applies `f` to each element and returns a new list of results.

```endo
print (map (fun x -> x * 2) [1; 2; 3])   # => [2; 4; 6]
```

```endo
print ([1; 2; 3] |> map (fun x -> x + 1) |> map (fun x -> x * 2))   # => [4; 6; 8]
```

```endo
print (map (_ * 2) [1; 2; 3])   # => [2; 4; 6]
```

#### `filter`

**Signature:** `filter predicate list : list<T>`

Returns a list of elements for which the predicate returns `true`.

```endo
print (filter (fun x -> x % 2 == 0) [1; 2; 3; 4; 5; 6])   # => [2; 4; 6]
```

```endo
print (filter (_ > 2) [1; 2; 3; 4; 5])   # => [3; 4; 5]
```

#### `fold`

**Signature:** `fold initial f list : T`

Reduces a list to a single value by applying `f` to the accumulator and each element, starting from `initial`.

```endo
print (fold 0 (fun acc x -> acc + x) [1; 2; 3; 4; 5])   # => 15
```

```endo
print (fold 1 (fun acc x -> acc * x) [1; 2; 3; 4; 5])   # => 120
```

```endo
print (fold 42 (fun acc x -> acc + x) [])   # => 42
```

#### `reduce`

**Signature:** `reduce f list : option<T>`

Like `fold`, but uses the first element as the initial value. Returns `Some result` or `None` for empty lists.

```endo
let r = reduce (fun a b -> a + b) [1; 2; 3; 4]
print (r ?| 0)   # => 10
```

```endo
let r = reduce (fun a b -> a + b) []
print (r ?| 0)   # => 0
```

#### `find`

**Signature:** `find predicate list : option<T>`

Returns `Some element` for the first element matching the predicate, or `None` if no element matches.

```endo
print (find (fun x -> x > 1) [1; 2; 3] ?| -1)   # => 2
```

```endo
print (find (fun x -> x > 10) [1; 2; 3] ?| -1)   # => -1
```

#### `exists`

**Signature:** `exists predicate list : bool`

Returns `true` if any element satisfies the predicate.

```endo
print (exists (fun x -> x > 3) [1; 2; 3; 4; 5])   # => true
```

```endo
print (exists (fun x -> x > 10) [1; 2; 3])   # => false
```

```endo
print (exists (fun x -> x > 0) [])   # => false
```

#### `forall`

**Signature:** `forall predicate list : bool`

Returns `true` if all elements satisfy the predicate. Returns `true` for empty lists.

```endo
print (forall (fun x -> x > 0) [1; 2; 3])   # => true
```

```endo
print (forall (fun x -> x > 2) [1; 2; 3])   # => false
```

```endo
print (forall (fun x -> x > 0) [])   # => true
```

#### `each`

**Signature:** `each f list : unit`

Applies `f` to each element for side effects. Returns unit.

```endo
each (fun x -> print x) [1; 2; 3]   # => 123
```

```endo
let _ = [1; 2; 3] |> each print   # => 123
```

---

## 15.6 List -- Transformations

#### `sort`

**Signature:** `sort list : list<int>`

Sorts a list of integers in ascending order.

```endo
print (sort [3; 1; 2])   # => [1; 2; 3]
```

```endo
print (sort [3; -1; 0; -5; 2])   # => [-5; -1; 0; 2; 3]
```

```endo
print (sort [])   # => []
```

#### `reverse`

**Signature:** `reverse list : list<T>`

Returns a new list with elements in reverse order.

```endo
print (reverse [1; 2; 3])   # => [3; 2; 1]
```

```endo
print (reverse [])   # => []
```

#### `distinct`

**Signature:** `distinct list : list<int>`

Removes duplicate elements, preserving first-seen order.

```endo
print (distinct [1; 2; 3; 2; 1])   # => [1; 2; 3]
```

```endo
print (distinct [5; 5; 5; 5])   # => [5]
```

#### `sortBy`

**Signature:** `sortBy keyFn list : list<T>`

Sorts a list by a key function. Uses stable sort (preserves relative order for equal keys).

```endo
print (sortBy (fun x -> x) [3; 1; 2])   # => [1; 2; 3]
```

```endo
print (sortBy (fun x -> 0 - x) [3; 1; 2])   # => [3; 2; 1]
```

#### `groupBy`

**Signature:** `groupBy keyFn list : list<(key, list<T>)>`

Groups elements by a key function. Returns a list of `(key, elements)` tuples.

```endo
print (groupBy (fun x -> x % 2) [1; 2; 3; 4; 5])   # => [(1, [1; 3; 5]); (0, [2; 4])]
```

```endo
print (groupBy (fun x -> x) [])   # => []
```

#### `take`

**Signature:** `take n list : list<T>`

Returns the first `n` elements. If the list has fewer than `n` elements, returns the whole list.

```endo
print (take 3 [1; 2; 3; 4; 5])   # => [1; 2; 3]
```

```endo
print (take 0 [1; 2; 3])   # => []
```

```endo
print (take 5 [1; 2; 3])   # => [1; 2; 3]
```

#### `drop`

**Signature:** `drop n list : list<T>`

Returns all elements after the first `n`.

```endo
print (drop 3 [1; 2; 3; 4; 5])   # => [4; 5]
```

```endo
print (drop 0 [1; 2; 3])   # => [1; 2; 3]
```

```endo
print (drop 3 [1; 2; 3])   # => []
```

#### `zip`

**Signature:** `zip listA listB : list<(A, B)>`

Combines two lists into a list of tuples. Stops at the shorter list.

```endo
print (zip [1; 2; 3] [4; 5; 6])   # => [(1, 4); (2, 5); (3, 6)]
```

```endo
print (zip [1; 2] [4; 5; 6])   # => [(1, 4); (2, 5)]
```

```endo
print (zip [] [1; 2; 3])   # => []
```

#### `flatten`

**Signature:** `flatten listOfLists : list<T>`

Flattens a list of lists into a single list.

```endo
print (flatten [[1; 2]; [3; 4]; [5; 6]])   # => [1; 2; 3; 4; 5; 6]
```

```endo
print (flatten [])   # => []
```

```endo
print (flatten [[]; [1; 2]; []])   # => [1; 2]
```

---

## 15.7 List -- Operators

#### `::` (cons)

Prepends an element to a list. Right-associative.

```endo
print (1 :: [])   # => [1]
```

```endo
print (1 :: 2 :: 3 :: [])   # => [1; 2; 3]
```

```endo
print (0 :: [1; 2; 3])   # => [0; 1; 2; 3]
```

#### `@` (concat)

Concatenates two lists.

```endo
print ([1; 2] @ [3; 4])   # => [1; 2; 3; 4]
```

```endo
print ([] @ [1; 2])   # => [1; 2]
```

```endo
print ([] @ [])   # => []
```

---

## 15.8 Option Combinators

#### `Option.map`

**Signature:** `Option.map f option : option<U>`

Applies `f` to the inner value of `Some`, or returns `None` unchanged.
Can also be used as method-style: `opt.map f` or in pipelines.

```endo
print (Option.map (fun x -> x * 2) (Some 21) ?| 0)   # => 42
```

```endo
print (Option.map (fun x -> x * 2) None ?| 0)   # => 0
```

```endo
print (Some 21 |> Option.map (fun x -> x * 2) |> Option.defaultValue 0)   # => 42
```

#### `Option.bind`

**Signature:** `Option.bind f option : option<U>`

Applies `f` (which itself returns an option) to the inner value of `Some`. Returns `None` if the input is `None` or if `f` returns `None`.
Can also be used as method-style: `opt.bind f`.

```endo
let half (x: int) = if x % 2 == 0 then Some (x / 2) else None
print (Option.bind half (Some 10) ?| 0)   # => 5
```

```endo
let half (x: int) = if x % 2 == 0 then Some (x / 2) else None
print (Option.bind half None ?| 0)   # => 0
```

```endo
let half (x: int) = if x % 2 == 0 then Some (x / 2) else None
print (Option.bind half (Some 3) ?| 0)   # => 0
```

#### `Option.defaultValue`

**Signature:** `Option.defaultValue default option : T`

Returns the inner value of `Some`, or the given default for `None`.
Can also be used as method-style: `opt.defaultValue def` or in pipelines.

```endo
print (Option.defaultValue 0 (Some 42))   # => 42
```

```endo
print (Option.defaultValue 0 None)   # => 0
```

```endo
print (Some 42 |> Option.defaultValue 0)   # => 42
```

```endo
print (None |> Option.defaultValue 99)   # => 99
```

---

## 15.9 Environment & System

#### `env`

**Signature:** `env name : option<str>`

Looks up an environment variable. Returns `Some value` if the variable is set, `None` otherwise.

<!-- endo-no-check -->
```endo
match env "HOME" with
| Some h -> println $"Home: {h}"
| None   -> println "HOME not set"

# With the ? operator
let user = (env "USER")?
println user
```

#### `which`

**Signature:** `which name : option<str>`

Searches `$PATH` for a program. Returns `Some path` if found, `None` otherwise.

<!-- endo-no-check -->
```endo
match which "git" with
| Some p -> println $"git is at {p}"
| None   -> println "git not found"

# With default value
print (which "git" ?| "/default")
```

---

## 15.10 Random

#### `rand`

**Signature:** `rand : int` or `rand low high : int`

With no arguments, returns a random positive integer.
With two arguments, returns a random integer in the inclusive range `[low, high]`.
Calling `rand` with one argument is a compile-time error.

<!-- endo-no-check -->
```endo
# Random positive integer
let x = rand
println x

# Random integer between 1 and 100
let y = rand 1 100
println y
```

---

## 15.11 Number Formatting

#### `formatNumber`

**Signatures:**
- `formatNumber separator number : string` — uses the given separator
- `formatNumber number : string` — uses the user's locale thousand separator

Formats an integer with thousand separators for readability.
In the 2-argument form, `separator` is a string inserted every 3 digits from the right.
In the 1-argument form, the thousand separator is determined by the user's system locale.
Negative numbers are handled correctly (the sign is preserved).

```endo
print (formatNumber "," 1234567)       # => 1,234,567
```

```endo
print (formatNumber "." 1234567)       # => 1.234.567
```

```endo
print (formatNumber " " 1000000)       # => 1 000 000
```

```endo
print (formatNumber "," 42)            # => 42
```

```endo
print (formatNumber "," -9876543)      # => -9,876,543
```

Works in pipelines too:

```endo
print (1234567 |> formatNumber ",")    # => 1,234,567
```

---

## 15.12 Composition Examples

These examples combine multiple standard library functions.

```endo
print ([3; 1; 2; 1; 3] |> sort |> distinct)   # => [1; 2; 3]
```

```endo
print ([5; 3; 8; 1; 4] |> filter (fun x -> x > 2) |> sort)   # => [3; 4; 5; 8]
```

```endo
print ([1; 2; 3] |> map (fun x -> x * 2) |> reverse)   # => [6; 4; 2]
```

```endo
print ([1; 2; 3; 4; 5] |> take 3 |> map (fun x -> x * 10))   # => [10; 20; 30]
```

```endo
print ([1; 2; 3; 4; 5] |> drop 2 |> fold 0 (fun a x -> a + x))   # => 12
```

---

## 15.13 Size Type

The `Size` type represents byte quantities with human-readable display formatting.

**Record fields:** `bytes: int`

#### Size Literals

Size values can be written directly using numeric suffixes:

```endo
print 42B     # => 42 B
```

```endo
print 1KB     # => 1 KB
```

```endo
print 5MB     # => 5 MB
```

```endo
print 2GB     # => 2 GB
```

```endo
print 1TB     # => 1 TB
```

Float values are supported (except for `B`, which requires whole numbers):

```endo
print 3.5KB   # => 3.5 KB
```

#### `Size.fromBytes`

**Signature:** `Size.fromBytes n : Size`

Creates a Size from a raw byte count.

```endo
print (Size.fromBytes 1024)   # => 1 KB
```

#### `Size.fromKB`

**Signature:** `Size.fromKB n : Size`

Creates a Size from kilobytes (n × 1024 bytes).

```endo
print (Size.fromKB 5)   # => 5 KB
```

#### `Size.fromMB`

**Signature:** `Size.fromMB n : Size`

Creates a Size from megabytes (n × 1024² bytes).

```endo
print (Size.fromMB 1)   # => 1 MB
```

#### `Size.fromGB`

**Signature:** `Size.fromGB n : Size`

Creates a Size from gigabytes (n × 1024³ bytes).

```endo
print (Size.fromGB 1)   # => 1 GB
```

#### `Size.fromTB`

**Signature:** `Size.fromTB n : Size`

Creates a Size from terabytes (n × 1024⁴ bytes).

```endo
print (Size.fromTB 1)   # => 1 TB
```

#### Field Access

Access the raw byte count via the `.bytes` field:

```endo
print (1KB).bytes   # => 1024
```

#### Size Comparison

Size values support comparison operators:

```endo
print (1KB < 1MB)   # => true
```

```endo
print (1KB == Size.fromBytes 1024)   # => true
```

---

## 15.14 DateTime Type

The `DateTime` type represents a point in time (UTC) with named field access.

**Record fields:** `year: int`, `month: int`, `day: int`, `hour: int`, `minute: int`, `second: int`, `epoch: int`

#### `DateTime.now`

**Signature:** `DateTime.now : DateTime`

Returns the current UTC date and time.

<!-- endo-no-check -->
```endo
let dt = DateTime.now
println dt              # 2024-06-15 14:30:00
println dt.year         # 2024
println dt.month        # 6
```

#### `DateTime.fromEpoch`

**Signature:** `DateTime.fromEpoch epoch : DateTime`

Converts a Unix epoch timestamp to a DateTime record.

<!-- endo-no-check -->
```endo
let dt = DateTime.fromEpoch 1700000000
println dt.year         # 2023
println dt.month        # 11
println dt.day          # 14
```

#### `formatDateTime`

**Signature:** `formatDateTime epoch : str`

Formats a Unix epoch timestamp as `YYYY-MM-DD HH:MM:SS`.

<!-- endo-no-check -->
```endo
print (formatDateTime 1700000000)   # 2023-11-14 22:13:20
```

---

## 15.15 TimeSpan Type

The `TimeSpan` type represents a duration of time with millisecond precision and human-readable display formatting.

**Record fields:** `milliseconds: int`

#### TimeSpan Literals

TimeSpan values can be written directly using numeric suffixes:

```endo
print 100ms   # => 100ms
```

```endo
print 5s      # => 5s
```

```endo
print 2min    # => 2m
```

```endo
print 1h      # => 1h
```

Float values are supported (except for `ms`, which requires whole numbers):

```endo
print 1.5h    # => 1h 30m
```

```endo
print 2.5s    # => 2s 500ms
```

#### `TimeSpan.fromMilliseconds`

**Signature:** `TimeSpan.fromMilliseconds n : TimeSpan`

Creates a TimeSpan from a raw millisecond count.

```endo
print (TimeSpan.fromMilliseconds 1500)   # => 1s 500ms
```

#### `TimeSpan.fromSeconds`

**Signature:** `TimeSpan.fromSeconds n : TimeSpan`

Creates a TimeSpan from seconds (n × 1000 ms).

```endo
print (TimeSpan.fromSeconds 5)   # => 5s
```

#### `TimeSpan.fromMinutes`

**Signature:** `TimeSpan.fromMinutes n : TimeSpan`

Creates a TimeSpan from minutes (n × 60000 ms).

```endo
print (TimeSpan.fromMinutes 2)   # => 2m
```

#### `TimeSpan.fromHours`

**Signature:** `TimeSpan.fromHours n : TimeSpan`

Creates a TimeSpan from hours (n × 3600000 ms).

```endo
print (TimeSpan.fromHours 1)   # => 1h
```

#### `TimeSpan.fromDays`

**Signature:** `TimeSpan.fromDays n : TimeSpan`

Creates a TimeSpan from days (n × 86400000 ms).

```endo
print (TimeSpan.fromDays 3)   # => 3d
```

#### Field Access

```endo
let t = TimeSpan.fromSeconds 5
print t.milliseconds   # => 5000
```

#### `sleep`

**Signature:** `sleep ts : unit`

Pauses execution for the given TimeSpan duration.

<!-- endo-no-check -->
```endo
sleep 100ms
1s |> sleep
```

#### `formatTimeSpan`

**Signature:** `formatTimeSpan ts : str`

Formats a TimeSpan as a human-readable duration string.

```endo
print (formatTimeSpan (TimeSpan.fromSeconds 90))   # => 1m 30s
```

---

## 15.16 Shell Data Commands

These built-in commands return structured records that support dot access, pattern matching,
and pipeline operations.

#### `ls`

**Signature:** `ls : list<FileInfo>` or `ls path : list<FileInfo>`

Lists directory contents as structured records.

**FileInfo fields:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | `str` | File name |
| `size` | `Size` | File size |
| `mode` | `FileMode` | Unix file permissions |
| `mtime` | `DateTime` | Last modification time |
| `isDir` | `bool` | Whether entry is a directory |

<!-- endo-no-check -->
```endo
# List all file names
ls |> map _.name

# Find large files (> 1 MB)
ls |> filter (_.size.bytes > 1048576) |> map _.name

# Sort by modification time
ls |> sortBy _.mtime.epoch

# Filter directories only
ls |> filter _.isDir |> map _.name

# Access nested fields
match head ls with
| Some f -> println $"{f.name}: {f.size}"
| None   -> println "empty directory"
```

#### `ps`

**Signature:** `ps : list<ProcessInfo>`

Lists running processes as structured records.

**ProcessInfo fields:**

| Field | Type | Description |
|-------|------|-------------|
| `pid` | `int` | Process ID |
| `ppid` | `int` | Parent process ID |
| `user` | `str` | Owning user |
| `cpu` | `float` | CPU usage percentage |
| `mem` | `float` | Memory usage percentage |
| `command` | `str` | Command name |

<!-- endo-no-check -->
```endo
# Filter by CPU usage
ps |> filter (_.cpu > 5.0) |> sortBy _.cpu

# Find processes by name
ps |> filter (_.command |> contains "firefox")

# Count processes per user
ps |> groupBy _.user |> map (fun (u, procs) -> (u, length procs))
```

#### `jobs`

**Signature:** `jobs : list<JobInfo>`

Lists background jobs as structured records.

**JobInfo fields:**

| Field | Type | Description |
|-------|------|-------------|
| `id` | `int` | Job number |
| `state` | `str` | Job state (e.g. "Running", "Stopped") |
| `command` | `str` | Command string |
| `pid` | `int` | Process ID |

<!-- endo-no-check -->
```endo
# List running background jobs
jobs |> filter (_.state == "Running") |> map _.command
```

---

## 15.17 FileMode Type

The `FileMode` type represents Unix file permissions with human-readable display.

| Field | Type | Description |
|-------|------|-------------|
| `bits` | `int` | Raw permission bits |

**Computed properties:**

| Property | Type | Description |
|----------|------|-------------|
| `isReadable` | `bool` | Any read permission bit set |
| `isWritable` | `bool` | Any write permission bit set |
| `isExecutable` | `bool` | Any execute permission bit set |
| `owner` | `int` | Owner digit (0-7) |
| `group` | `int` | Group digit (0-7) |
| `other` | `int` | Other digit (0-7) |

#### `FileMode.fromBits`

**Signature:** `FileMode.fromBits n : FileMode`

Creates a FileMode from raw Unix permission bits.

<!-- endo-no-check -->
```endo
let m = FileMode.fromBits 493
print m              # rwxr-xr-x
print m.isExecutable # true
print m.owner        # 7
```

---

## 15.18 File Permission Helpers

These functions operate on Unix file modes (accept both `FileMode` objects and raw `int` permission bits).

#### `formatMode`

**Signature:** `formatMode mode : str`

Formats a Unix file mode as a `rwxrwxrwx` permission string.

<!-- endo-no-check -->
```endo
ls |> map (fun f -> (f.name, formatMode f.mode))
```

#### `isReadable`

**Signature:** `isReadable mode : bool`

Tests if any read permission bit is set.

<!-- endo-no-check -->
```endo
ls |> filter (fun f -> isReadable f.mode) |> map _.name
```

#### `isWritable`

**Signature:** `isWritable mode : bool`

Tests if any write permission bit is set.

<!-- endo-no-check -->
```endo
ls |> filter (fun f -> isWritable f.mode) |> map _.name
```

#### `isExecutable`

**Signature:** `isExecutable mode : bool`

Tests if any execute permission bit is set.

<!-- endo-no-check -->
```endo
ls |> filter (fun f -> isExecutable f.mode) |> map _.name
```

---

## 15.19 Display Formatting

#### `toText`

**Signature:** `toText value : str`

Converts any value to its string representation. Useful for converting structured records
to plain text.

<!-- endo-no-check -->
```endo
ls |> map toText |> each println
```

#### `string`

**Signature:** `string value : str`

Universal conversion to string. Works with integers, floats, booleans, and strings.

```endo
print (string 42)      # => 42
```

```endo
print (string true)    # => true
```

---

## 15.20 HTTP

#### `fetch`

**Signature:** `fetch url : result<str, str>`

Fetches content from a URL. Returns `Ok body` on success, `Error message` on failure.

<!-- endo-no-check -->
```endo
match fetch "https://example.com" with
| Ok body  -> println body
| Error e  -> println $"Failed: {e}"
```

---

## 15.21 Timing

#### `time`

**Signature:** `time { body } -> TimeSpan`

Measures the wall-clock execution time of a computation expression.
Returns a `TimeSpan` that can be inspected, formatted, or piped.

```endo
let t = time { [1; 2; 3] |> map (_ * 2) }
print (formatTimeSpan t)
```

<!-- endo-no-check -->
```endo
time { sleep (TimeSpan.fromMilliseconds 100) }
```

## 15.22 JSON

#### `Json.query`

**Signature:** `Json.query path json -> list<string>`

Extracts values from a JSON string using a dotted path with `[]` for array iteration.
Returns a `list<string>` of all matching leaf values (numeric/boolean values converted to strings).
On parse error or missing key, returns an empty list.

**Path syntax:**
- `.key` — access an object property
- `[]` — iterate array elements
- Chained: `.key1[].key2` — access `key1` (array), iterate elements, access `key2` from each

```endo
let json = "{\"name\": \"hello\"}"
let r = Json.query ".name" json
r |> each println
```

```endo
let json = "{\"items\":[{\"name\":\"alpha\"},{\"name\":\"beta\"}]}"
let r = Json.query ".items[].name" json
r |> each println
```

Pipeline usage:

```endo
let json = "{\"name\": \"hello\"}"
json |> Json.query ".name" |> each println
```

List append pattern (useful for merging results from multiple JSON files):

<!-- endo-no-check -->
```endo
let r = Json.query ".presets[].name" json1 @ Json.query ".presets[].name" json2
r |> each println
```

## 15.23 Lazy Evaluation

#### `force`

**Signature:** `force lazy<'T> : 'T`

Forces evaluation of a lazy value. On first call, evaluates the deferred
expression and caches the result. Subsequent calls return the cached value.

```endo
let x = lazy (1 + 2)
println (force x)
```

```endo
let x = lazy 42
x |> force |> println
```

## 15.24 Lazy Sequences

Lazy sequences build on lazy evaluation to create sequences whose elements are computed on demand.

#### `seq { yield ...; yield! ... }`

**Syntax:** `seq { yield expr; yield! seq_expr }`

Creates a lazy sequence. `yield` produces a single element; `yield!` splices another sequence.

```endo
let s = seq { yield 1; yield 2; yield 3 }
s |> toList |> println
```

#### `toList`

**Signature:** `toList seq<'a> : list<'a>`

Forces a lazy sequence into an eagerly-evaluated list.

```endo
let s = seq { yield 1; yield 2; yield 3 }
s |> toList |> println
```

## 15.25 Path

The `Path` module provides cross-platform filesystem path utilities.

#### `Path.temporary_directory`

**Signature:** `Path.temporary_directory -> str`

Returns the platform's temporary directory path (e.g. `/tmp` on Linux, `C:\Users\...\AppData\Local\Temp` on Windows).

```endo
let tmp = Path.temporary_directory
println tmp
```

## 15.26 File I/O

The `File` module provides file operations. Functions that can fail return `result<T, str>`.

#### `File.open`

**Signature:** `File.open path mode : result<FileHandle, str>`

Opens a file. Mode can be `"r"` (read), `"w"` (write/truncate), `"a"` (append), or `"rw"` (read/write).

#### `File.close`

**Signature:** `File.close fd : unit`

Closes a file handle.

#### `File.readLine`

**Signature:** `File.readLine fd : option<str>`

Reads one line from the file. Returns `None` at end of file.

#### `File.readAll`

**Signature:** `File.readAll path : result<str, str>`

Reads the entire file as a string.

#### `File.writeAll`

**Signature:** `File.writeAll path content : result<unit, str>`

Writes a string to a file, creating or truncating it.

#### `File.appendAll`

**Signature:** `File.appendAll path content : result<unit, str>`

Appends a string to a file.

#### `File.size`

**Signature:** `File.size path : result<int, str>`

Returns the file size in bytes.

#### `File.exists`

**Signature:** `File.exists path : bool`

Returns `true` if the file exists.

```endo
println (File.exists "/etc/hostname")
```

#### `File.delete`

**Signature:** `File.delete path : result<unit, str>`

Deletes a file.

## 15.27 Resource Management

#### `let use`

**Syntax:** `let use name = expr`

Binds a disposable resource and automatically calls its dispose function at scope exit (LIFO order).
Types with a registered dispose function (such as `FileHandle`) require either `use` or `manual`.

```endo
let use fd = File.open "data.txt" "r"
# fd is automatically closed when the scope exits
```

#### `let manual`

**Syntax:** `let manual name = expr`

Binds a disposable resource without automatic cleanup. The user is responsible for calling the
dispose function manually. Use when the resource lifetime extends beyond the current scope.

---
**See also:** [Lists & Collections](lists-and-collections.md) | [Operators & Pipelines](operators-and-pipelines.md) | [Error Handling](error-handling.md) | [Lazy Evaluation](lazy-evaluation.md)
