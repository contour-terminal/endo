# Modules & Imports

Endo's module system lets you organize functions, types, and values into reusable, namespaced
units. Modules follow F# conventions: PascalCase names, qualified access, and import-once semantics.

### 12.1 Inline Module Definitions

Modules can be defined inline using `module Name = ... end`:

```endo
module Helpers =
    let double (x: int) : int = x * 2
    let triple (x: int) : int = x * 3
end

println (Helpers.double 5)   # => 10
println (Helpers.triple 4)   # => 12
```

All bindings inside the module are public and accessible via qualified access (`Module.function`).
Inline modules persist across REPL prompts.

### 12.2 Multiple Inline Modules

You can define several modules and call across them freely:

```endo
module Math =
    let square (x: int) : int = x * x
    let cube (x: int) : int = x * x * x
end

module Utils =
    let inc (x: int) : int = x + 1
    let dec (x: int) : int = x - 1
end

println (Math.square 5)                  # => 25
println (Math.cube 3)                    # => 27
println (Utils.inc (Math.square 4))      # => 17
```

### 12.3 Opening Modules

`open` loads a module and brings all its public names into the current scope.
Both qualified and unqualified access work after `open`.

```endo
module Math =
    let square (x: int) : int = x * x
    let cube (x: int) : int = x * x * x
end

open Math

println (square 5)        # => 25
println (cube 3)          # => 27
println (Math.square 4)   # => 16
```

#### Selective Open

`open Module with (name1, name2, ...)` imports only the listed names into unqualified scope.
Qualified access to all public members still works.

```endo
module Math =
    let square (x: int) : int = x * x
    let cube (x: int) : int = x * x * x
    let double (x: int) : int = x * 2
end

open Math with (square, cube)

println (square 5)        # => 25
println (cube 3)          # => 27
println (Math.double 4)   # => 8
```

### 12.4 File-Based Modules

A `.endo` file is a module. The filename determines the module name (PascalCase convention).
No module declaration is needed inside the file -- the filename is the module name.

For example, a file named `Math.endo` with the following content defines module `Math`:

```endo
let square (x: int) : int = x * x
let cube (x: int) : int = x * x * x
```

You can then load it with `import Math` and use `Math.square 5`.

**Nested modules** use directory structure. For `import Geometry.Circle`, Endo looks for:

```text
Geometry/
    Circle.endo       # becomes Geometry.Circle
    Rectangle.endo    # becomes Geometry.Rectangle
```

### 12.5 Importing Modules

`import` loads a file-based module and makes its members available via qualified access.
Members are **not** added to the unqualified scope -- you must always use the `Module.name` prefix.

```endo
# file: Math.endo
let square (x: int) : int = x * x
let cube (x: int) : int = x * x * x
```

```endo
import Math

println (Math.square 5)           # => 25
println (Math.cube 3)             # => 27
```

`open` can then bring names into scope (same as with inline modules):

```endo
import Math
open Math
println (square 5)                # => 25
```

### 12.6 Shell Keyword Harmony

`import`, `open`, and `module` are **not** reserved keywords. They are only recognized as module
directives when followed by a PascalCase identifier (starting with an uppercase letter). This means
common shell commands continue to work unchanged:

```endo
import requests
```

The line above is treated as a **shell command** (because `requests` is lowercase), not a module
import. Similarly:

```endo
module load gcc
```

This is a shell command, not an inline module definition (because `load` is lowercase).

The following, however, defines an inline module (because `Helpers` is PascalCase):

```endo
module Helpers =
    let f (x: int) : int = x + 1
end
println (Helpers.f 5)   # => 6
```

### 12.7 Import-Once Semantics

Each module is compiled and its top-level bindings evaluated **exactly once** per session,
regardless of how many times it is `import`ed or `open`ed.

The module cache is keyed by resolved absolute file path, so the same physical file loaded via
different relative paths is still recognized as one module.

### 12.8 Module Resolution

When resolving `import Foo`, Endo searches these locations in order:

| Priority | Location | Example |
|----------|----------|---------|
| 1 | Relative to importing file | `./Foo.endo` |
| 2 | Subdirectory (for nested modules) | `./Foo/Bar.endo` |
| 3 | User modules directory | `~/.config/endo/modules/Foo.endo` |
| 4 | System stdlib | `<prefix>/share/endo/stdlib/Foo.endo` |

For nested modules like `import Geometry.Circle`:

| Priority | Location |
|----------|----------|
| 1 | `./Geometry/Circle.endo` |
| 2 | `~/.config/endo/modules/Geometry/Circle.endo` |
| 3 | `<prefix>/share/endo/stdlib/Geometry/Circle.endo` |

### 12.9 REPL Usage

Modules work interactively in the REPL. Imported modules, opened names, and inline module
definitions all persist across prompts:

```text
$ module Quick =
>     let half (x: int) : int = x / 2
> end
$ Quick.half 10
5
$ open Quick
$ half 20
10
```

### 12.10 Circular Dependency Detection

Endo detects circular dependencies at load time. If module A imports module B, which imports
module A, a clear error is reported:

```text
Error: Circular module dependency: ModA → ModB → ModA
```

### 12.11 Error Messages

| Situation | Error Message |
|-----------|---------------|
| Module not found | `Module 'Foo' not found. Searched: ./Foo.endo, ...` |
| Circular dependency | `Circular module dependency: A → B → A` |
| Unknown member | `Module 'Math' has no member 'nonexistent'` |
| Private member | `'Math.helper' is private and cannot be accessed outside module 'Math'` |
| No module loader | `Module system not available (no module loader configured)` |
| Selective open mismatch | `Module 'Math' has no member 'nonexistent' (in selective open)` |

### 12.12 Interaction with Built-in Modules

Built-in modules (File, Path, DateTime, Size, TimeSpan, etc.) take priority over user-defined
modules of the same name. These are always available without an `import` statement:

```endo
let content = File.readAll "data.txt"
let size = File.size "data.txt"
```

User-defined modules require explicit `import` or inline `module ... end` definitions.

### 12.13 Creating Your Own Modules

To create a reusable module, write a `.endo` file with a PascalCase name and place it
in one of the module search paths (e.g., `~/.config/endo/modules/`).

Here is a self-contained example using inline modules that demonstrates the same patterns
you would use with file-based modules:

```endo
module StringUtils =
    let rec repeat (acc: str) (s: str) (n: int) : str =
        match n with
        | 0 -> acc
        | _ -> repeat (acc + s) s (n - 1)

    let greet (name: str) : str = "Hello, " + name + "!"
end

println (StringUtils.repeat "" "ha" 3)       # => hahaha
println (StringUtils.greet "world")          # => Hello, world!
```

Or bring names into scope with `open`:

```endo
module StringUtils =
    let rec repeat (acc: str) (s: str) (n: int) : str =
        match n with
        | 0 -> acc
        | _ -> repeat (acc + s) s (n - 1)
end

open StringUtils with (repeat)

println (repeat "" "ho" 3)   # => hohoho
```

### 12.14 Summary

| Feature | Syntax | Effect |
|---------|--------|--------|
| Inline module | `module M = ... end` | Define module in current file/REPL |
| Import (file) | `import Math` | Qualified access: `Math.square 5` |
| Open | `open Math` | Unqualified + qualified access |
| Selective open | `open Math with (square)` | Only listed names unqualified |
| Nested import | `import Geometry.Circle` | Load from `Geometry/Circle.endo` |

---
**See also:** [Functions](functions.md) | [Standard Library](standard-library.md) | [Error Handling](error-handling.md)
