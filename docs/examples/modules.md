# Modules

Organizing code into reusable modules with `import`, `open`, and inline `module` definitions.

```endo
// Inline module definitions
module Math =
    let square (x: int) : int = x * x
    let cube (x: int) : int = x * x * x
end

module Utils =
    let double (x: int) : int = x * 2
    let inc (x: int) : int = x + 1
end

// Qualified access: Module.function
println (Math.square 5)
println (Math.cube 3)
println (Utils.double 10)

// Chaining calls across modules
println (Utils.inc (Math.square 4))
```

## Key Techniques

- **Inline modules** are defined with `module Name = ... end`. The module name must be PascalCase.
- **Qualified access** uses `Module.function` syntax to call functions from a specific module. This avoids name collisions between modules.
- **File-based modules** work the same way: a file named `Math.endo` becomes module `Math`. Use `import Math` to load it, then access members with `Math.square 5`.
- **`open`** brings all module names into the current scope so you can call `square 5` directly without the `Math.` prefix. `open Math with (square)` selectively imports only listed names.
- **Import-once**: Each module is loaded and evaluated exactly once per session, no matter how many times it is imported or opened.
- **Shell harmony**: `import`, `open`, and `module` only act as module keywords when followed by a PascalCase name. `import requests` or `open file.txt` are still treated as shell commands.

## See Also

- [Modules & Imports](../language/modules-and-imports.md)
- [Functions](../language/functions.md)
- [Variables & Bindings](../language/variables-and-bindings.md)
