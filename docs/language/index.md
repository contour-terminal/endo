# Philosophy & Goals

The Endo Language Reference documents the complete specification of the Endo programming language — a cross-platform shell that combines F#-inspired functional programming with bash-compatible shell scripting.

### Core Principles

- **F#-inspired functional shell** - Bring functional programming ergonomics to shell scripting
- **Bash convenience** - Familiar syntax for common operations, easy transition for existing shell users
- **Type inference by default** - Types are automatically deduced; annotations are optional for documentation or disambiguation
- **Unified command model** - Functions and external commands share invocation syntax
- **Expression-oriented** - Most constructs return values and can be composed

### Non-Goals

- Full POSIX compliance (we prioritize compatibility over strict compliance)
- Complete F# feature parity (practical shell focus takes precedence)
- Replacing bash entirely (interoperability is key)

### Design Rationale

Endo recognizes that shell scripting and functional programming serve complementary needs:

| Shell Strengths | Functional Strengths |
|-----------------|---------------------|
| Process orchestration | Data transformation |
| System interaction | Complex logic |
| Quick one-liners | Type safety |
| Tool composition | Refactoring confidence |

Endo combines both, letting you choose the right style for each task.

## Language Reference Pages

- [Lexical Elements](lexical-elements.md)
- [Type System](type-system.md)
- [Variables & Bindings](variables-and-bindings.md)
- [Functions](functions.md)
- [Lists & Collections](lists-and-collections.md)
- [Pattern Matching](pattern-matching.md)
- [Operators & Pipelines](operators-and-pipelines.md)
- [Control Flow](control-flow.md)
- [Command Execution](command-execution.md)
- [Error Handling](error-handling.md)
- [Lazy Evaluation](lazy-evaluation.md)
- [Modules & Imports](modules-and-imports.md)
- [Interoperability: F# Style vs Bash Style](interoperability.md)
- [Standard Library Reference](standard-library.md)
- [Implementation Notes](implementation-notes.md)
- [EBNF Grammar](grammar.md)

---
**See also:** [Lexical Elements](lexical-elements.md) | [Type System](type-system.md) | [Interoperability](interoperability.md)
