---
title: Endo Shell
description: A modern, cross-platform shell where functional programming meets everyday productivity.
---

<div class="hero" markdown>

# **Endo**

## Stop parsing. Start piping.

A modern, cross-platform shell where functional programming meets everyday productivity.

*The shell you always wanted -- F#-inspired, Bash-convenient, everywhere.*

[Get Started](getting-started.md){ .md-button .md-button--primary }
[Language Reference](language/index.md){ .md-button }

</div>

---

## Why Endo?

Shells haven't evolved. You're still gluing strings together, guessing at exit codes, and
writing brittle pipelines. **Endo changes that.** It brings the expressive power of functional
programming to your terminal -- without sacrificing the quick-and-dirty convenience you rely
on every day.

<div class="grid cards" markdown>

-   :material-function-variant: **Functional Core**

    ---

    F#-inspired language with pipe operators, pattern matching, immutable-by-default bindings,
    and first-class functions built into the foundation.

-   :material-bash: **Bash Compatible**

    ---

    Run commands, redirect output, glob files, chain with `&&` and `||`. If your muscle memory
    speaks Bash, Endo understands.

-   :material-pipe: **Structured Pipelines**

    ---

    Stop parsing `grep | awk | sed` chains. Endo pipelines pass typed records between stages,
    so data stays intact from source to sink.

-   :material-laptop: **Cross-Platform**

    ---

    Native support for Linux, macOS, and Windows. Write scripts once, run them everywhere --
    no compatibility layers, no emulation.

-   :material-tab: **Intelligent Completions**

    ---

    Context-aware tab completions powered by the type system. Endo knows what a command expects
    before you finish typing it.

-   :material-alert-circle-check: **Sane Error Handling**

    ---

    No more silent failures. Result and Option types give you explicit, composable error
    handling without the ceremony.

</div>

---

## A Quick Taste

**Familiar commands, elevated syntax:**

```endo
# It's still a shell -- run anything
ls -la
git status && echo "All clean"

# F#-style bindings and string interpolation
let name = "world"
echo $"Hello, {name}!"
```

**Shell output meets functional pipelines:**

```endo
# Pipe shell command output straight into F# transforms
ps aux | lines |> filter (contains _ "nginx") |> length
|> fun n -> echo $"Found {n} nginx processes"

# Process git history with functional pipelines
git log --oneline | lines |> take 5 |> each println
```

**Functional data processing:**

```endo
# Placeholder lambdas keep pipelines concise
[10; 25; 3; 42; 7] |> filter (_ > 10) |> map (_ * 2)   # [50; 6; 84]

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
let rec sum lst =
    match lst with
    | [] -> 0
    | head :: tail -> head + sum tail

print (sum [1; 2; 3; 4; 5])         # 15
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

Licensed under the Apache License 2.0. See [LICENSE](https://github.com/christianparpart/endo/blob/master/LICENSE) for details.
