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
- [15.11 Composition Examples](#1511-composition-examples)

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

#### `contains`

**Signature:** `contains haystack needle : bool`

Returns `true` if `haystack` contains the substring `needle`.

```endo
print (contains "hello world" "world")   # => true
```

```endo
print (contains "hello" "xyz")   # => false
```

#### `startsWith`

**Signature:** `startsWith str prefix : bool`

Returns `true` if the string starts with the given prefix.

```endo
print (startsWith "hello" "hel")   # => true
```

```endo
print (startsWith "hello" "xyz")   # => false
```

#### `endsWith`

**Signature:** `endsWith str suffix : bool`

Returns `true` if the string ends with the given suffix.

```endo
print (endsWith "hello" "llo")   # => true
```

```endo
print (endsWith "hello" "xyz")   # => false
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

#### `split`

**Signature:** `split separator str : list<str>`

Splits a string by the given separator, returning a list of substrings.

```endo
let parts = split "," "a,b,c"
print (length parts)   # => 3
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

## 15.11 Composition Examples

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
**See also:** [Lists & Collections](lists-and-collections.md) | [Operators & Pipelines](operators-and-pipelines.md) | [Error Handling](error-handling.md)
