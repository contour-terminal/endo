# Crush Language Design

**This file should sketch some example syntax that we'd like to consider supporting.**

This document is used to bootstrap and and formulate the idea of how the Crush's shell syntax might look like.

## POSIX / Bash-style control flow

```sh
if condition; then block; else block2; fi
if condition; then block; fi
while condition; do block; done

if condition
then
    block
fi

while condition
do
    block
done

condition && block || block2
condition || block
```

## C-style control flow

```sh
if (condition) { block } else { block2 }
while (condition) { block }
```

## Shell Pipes

```sh
# linking processes
ps afx | grep --color $USERNAME

# linking control flow / builtin commands with processes
while read N; do echo "N: $N"; done <README.md | sort | uniq
while read N <README.md; do echo "N: $N"; done | sort | uniq
```

## declare variables in local scope

```sh
let VAR VALUE               # define immutable local variable
let mut VAR VALUE           # define mutable local variable
let global VAR VALUE        # define global variable (regardless of current scope)
```

`mut` and `global` are simply modifiers (directives) and thus not reserved keywords like `let`.

## export variables to child processes

```sh
export NAME
```

## functions

```sh
# Define function f, receiving 3 string parameters, and returning one string
let f (a, b, c): str =
    echo "a: $a, b: $b, c: $c"
    return "($a,$b,$c)"

let y = f "foo" 3.1415 $VAR      # prints and returns something into $y
echo $y                          # prints "(foo,3.1415,VALUE)"

let g a b: str =
    echo "a is: $a, and b is: $b"
    if a = b then
        return "same"
    else
        return "not same"

let has_var name: bool =
    return (getenv $name) != ""

# single-line function definition
let has_var name: bool = (getenv $name) != ""
```
