---
title: Endo Shell
description: A cross-platform shell with F#-inspired functional programming.
---

<div class="hero" markdown>

# **Endo**

A cross-platform shell with F#-inspired functional programming.

[Get Started](getting-started.md){ .md-button .md-button--primary }
[Language Reference](language/index.md){ .md-button }

</div>

---

!!! warning "Early Development"

    Endo is under active development. The language, builtins, and APIs may change.
    Feedback and contributions are very welcome!

## What Is Endo?

Endo is an interactive shell and scripting language that combines familiar command-line
conventions with ideas from functional programming — primarily F#. It runs natively on
Linux, macOS, and Windows.

<div class="grid cards" markdown>

-   :material-function-variant: **F#-Inspired Language**

    ---

    Pipe operators, pattern matching, immutable-by-default bindings, and first-class
    functions form the core of the language.

-   :material-bash: **Familiar Shell Conventions**

    ---

    Run commands, redirect output, glob files, chain with `&&` and `||`. Everyday shell
    usage works the way you'd expect.

-   :material-pipe: **Structured Pipelines**

    ---

    Pipelines pass typed records between stages, so data stays intact from source to sink.

-   :material-laptop: **Cross-Platform**

    ---

    Runs natively on Linux, macOS, and Windows from the same source — scripts are portable
    without compatibility layers.

-   :material-tab: **Context-Aware Completions**

    ---

    Tab completions are informed by the type system, offering relevant suggestions based on
    what a command or function expects.

-   :material-alert-circle-check: **Explicit Error Handling**

    ---

    Result types (`Ok`/`Error`) and option types (`Some`/`None`) make error paths visible
    and composable.

</div>

---

## A Quick Taste

**Basic usage:**

```endo
# It's still a shell -- run anything
ls -la
git status && echo "All clean"

# F#-style bindings and string interpolation
let name = "world"
println $"Hello, {name}!"
```

**Shell commands in functional pipelines:**

<!-- endo-no-check -->
```endo
# Structured builtins return typed records — no text parsing needed
ps |> filter (_.command |> contains "endo") |> length
|> fun n -> println $"Found {n} endo processes"

# git log returns structured commit data
git log |> take 5 |> each (fun c -> println c.message)
```

**Functional data processing:**

<!-- endo-no-check -->
```endo
# Placeholder lambdas keep pipelines concise
[10; 25; 3; 42; 7] |> filter (_ > 10) |> map (_ * 2)   # [50; 84]

# Curried functions and partial application
let add x y = x + y
let add10 = add 10
[1; 2; 3] |> map add10              # [11; 12; 13]

# Function composition
let double = _ * 2
let inc = _ + 1
let doubleThenInc = double >> inc
print (doubleThenInc 5)              # 11
```

**Pattern matching at your prompt:**

```endo
# Option types for safe value handling
match (env "EDITOR") with
| Some editor -> print $"Using {editor}"
| None        -> print "No editor set"

# Result types for explicit error handling
let safeDiv x y =
    if y == 0 then Error "division by zero"
    else Ok (x / y)

match safeDiv 10 0 with
| Ok n    -> print $"Result: {n}"
| Error e -> print $"Failed: {e}"
```

**Lists, ranges, and comprehensions:**

```endo
# Ranges and comprehensions
let squares = [for x in [1..10] -> x * x]
let evens = [for x in [1..20] when x % 2 == 0 -> x]

# Recursive processing with pattern matching
let rec sum acc lst =
    match lst with
    | [] -> acc
    | head :: tail -> sum (acc + head) tail

print (sum 0 [1; 2; 3; 4; 5])       # 15
```

---

## Quick Links

- [Getting Started](getting-started.md) -- Build from source and write your first Endo script
- [Language Reference](language/index.md) -- Complete language documentation
- [Shell Features](shell/index.md) -- Interactive shell capabilities
- [Examples](examples/index.md) -- Real-world code examples
- [FAQ](FAQ.md) -- Frequently asked questions
- [Roadmap](roadmap/index.md) -- Development plans and progress

---

## License

Licensed under the Apache License 2.0. See [LICENSE](https://github.com/contour-terminal/endo/blob/master/LICENSE) for details.
