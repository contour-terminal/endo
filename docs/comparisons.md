---
title: Endo vs Other Shells
description: How Endo compares to Bash, Fish, Nushell, Elvish, and Zsh.
---

# How Endo Compares

Endo combines **Bash-compatible command execution** with **F#-inspired functional
programming**. This page compares it with other shells to help you decide if it fits
your workflow.

## Quick Comparison

| Feature | Endo | Bash | Fish | Nushell | Elvish |
|---------|------|------|------|---------|--------|
| Bash compatibility | :material-check-bold: High | :material-check-bold: Native | :material-close: Own syntax | :material-close: Own syntax | :material-close: Own syntax |
| Structured data | :material-check-bold: Typed records | :material-close: Text | :material-close: Text | :material-check-bold: Tables | :material-approximately-equal: Values |
| Type system | :material-check-bold: Hindley-Milner | :material-close: | :material-close: | :material-approximately-equal: Dynamic | :material-approximately-equal: Dynamic |
| Pattern matching | :material-check-bold: Full ADTs | :material-approximately-equal: case/glob | :material-close: | :material-approximately-equal: match | :material-close: |
| LSP support | :material-check-bold: Full | :material-approximately-equal: External | :material-close: | :material-approximately-equal: Basic | :material-approximately-equal: Built-in |
| Tab completion | :material-check-bold: Type-aware | :material-approximately-equal: Basic | :material-check-bold: Rich | :material-check-bold: Rich | :material-approximately-equal: Basic |
| Syntax highlighting | :material-check-bold: Semantic | :material-close: | :material-check-bold: | :material-check-bold: | :material-check-bold: |
| Windows | :material-check-bold: Native | :material-approximately-equal: WSL/Git Bash | :material-approximately-equal: WSL | :material-check-bold: | :material-approximately-equal: Experimental |
| Debugging | :material-check-bold: Full DAP | :material-approximately-equal: bashdb + DAP | :material-close: | :material-close: | :material-close: |
| Maturity | :material-new-box: 0.1.0 | :material-check-bold: 35+ years | :material-check-bold: 21+ years | :material-approximately-equal: ~7 years | :material-approximately-equal: ~8 years |

## Endo vs Nushell

Nushell is the closest comparison. Both shells embrace structured data pipelines over
plain text. The key differences:

**Where Endo differs:**

- **Bash compatibility.** Endo runs existing shell commands with familiar syntax --
  pipes, redirects, `&&`/`||`, glob expansion, and job control all work as expected.
  Nushell uses its own command syntax that requires relearning common operations.
- **F# syntax.** Endo's functional layer uses F# syntax (familiar to ML, OCaml, Haskell
  developers), not a custom DSL. If you know F#, you already know Endo's expression language.
- **Algebraic data types.** Full records, discriminated unions, Option/Result types with
  pattern matching and exhaustiveness checking.
- **Full LSP server.** Hover, go-to-definition, find references, rename, semantic tokens,
  signature help, code actions, formatting -- not just basic completions.
- **Type inference.** Hindley-Milner inference (Algorithm W) that stays out of your way
  until you need it.
- **Integrated debugger.** Full Debug Adapter Protocol (DAP) server with breakpoints,
  stepping, variable inspection, watch expressions, conditional breakpoints, and
  disassembly -- works directly with VS Code and any DAP-compatible editor. Nushell has
  no debugging support.

**Where Nushell excels:**

- **Larger community and ecosystem.** More plugins, themes, and documentation.
- **More data formats.** Native support for CSV, JSON, TOML, YAML, SQLite, and more.
- **More mature.** Several years of production use and bug fixes.

## Endo vs Fish

Fish is known for its excellent interactive experience. Both shells prioritize user
experience, but they take different approaches:

**Where Endo differs:**

- **Structured pipelines.** Endo's `|>` pipe carries typed values, not text. Commands like
  `ls` return records that you can filter, map, and sort with type safety.
- **Pattern matching.** Match expressions with guards, destructuring, and algebraic types.
- **F# expressions.** Let bindings, lambdas, currying, partial application, and composition
  operators at your prompt.
- **LSP integration.** Full IDE features in your editor for `.endo` scripts.
- **Integrated debugger.** Full DAP server for breakpoints, stepping, and variable
  inspection. Fish only offers a basic `breakpoint` command with no step debugger or DAP
  support.

**Where Fish excels:**

- **Mature ecosystem.** Thousands of plugins (Fisher, Oh My Fish) and community themes.
- **Wider platform support.** Tested extensively on all platforms.
- **Larger community.** More Stack Overflow answers, tutorials, and blog posts.
- **Simpler learning curve.** Fish's syntax is easy to pick up without functional
  programming knowledge.

## Endo vs Bash / Zsh

Bash is the default shell on most systems. Zsh adds interactive improvements. Endo builds
on their foundation:

**Where Endo differs:**

- **Functional programming layer.** F# expressions, pattern matching, algebraic types,
  and typed pipelines -- without leaving the shell.
- **Structured output.** Builtins like `ls` and `ps` return typed records instead of
  formatted text.
- **IDE features.** Full LSP server, semantic syntax highlighting, and source formatting.
- **Explicit error handling.** Result/Option types with `?` propagation instead of exit codes.
- **Type inference.** Automatic type deduction with optional annotations.
- **Integrated debugger.** DAP server with conditional breakpoints, watch
  expressions, disassembly, and expression evaluation. Bash has bashdb and a VS Code DAP
  extension, but Endo's debugger is built-in and requires no external tools.

**Where Bash/Zsh excel:**

- **Universal availability.** Bash is pre-installed on virtually every Unix system.
- **Massive script ecosystem.** Decades of scripts, tutorials, and tooling.
- **Battle-tested.** Extremely well-understood behavior in production environments.
- **Zero learning curve** for anyone who has used a terminal before.

## When to Choose Endo

Endo is a great fit if you:

- Want IDE features at the command line — hover tooltips, real-time diagnostics,
  semantic syntax highlighting, and go-to-definition
- Want functional programming at your prompt without leaving shell mode
- Prefer typed pipelines over text-parsing chains
- Need an integrated script debugger with breakpoints, stepping, and variable inspection
- Are comfortable with F#, OCaml, Haskell, or similar syntax
- Want a single shell that works on Linux, macOS, and Windows

## When to Choose Something Else

- **Need maximum script compatibility?** Stick with Bash or Zsh.
- **Want the largest community and plugin ecosystem?** Choose Fish or Zsh.
- **Need the most data format support?** Choose Nushell.
- **Want the most battle-tested option?** Bash has 35+ years of production use.
- **Prefer minimal learning curve?** Fish is friendlier for non-programmers.

Endo is new (0.1.0). It is well-tested (1,500+ integration tests) and suitable for daily
use, but it does not yet have the ecosystem depth of more established shells.
