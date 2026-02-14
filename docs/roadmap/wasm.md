---
title: WebAssembly Roadmap
description: Roadmap for WebAssembly compilation support in Endo.
---

# WebAssembly Compilation Roadmap

This document outlines the plan to add WebAssembly (WASM) compilation support to Endo.
The goal is to enable compiling Endo scripts to `.wasm` files that can run in browsers,
edge computing environments, or any WASM runtime.

**Target invocation:**

```bash
endo -o hello.wasm hello.endo     # Compile to WebAssembly
endo -o hello.wat hello.endo      # Compile to WebAssembly Text format (for debugging)
```

!!! info "Status: Not Yet Started"
    This roadmap describes planned work. No implementation has begun yet.

## Design Goals

1. **Dual-mode support:**
    - **Portable mode** -- Pure computation scripts for browsers/edge (no shell operations)
    - **WASI mode** -- Full shell functionality using WASI for system calls

2. **Direct WASM emission** -- Generate WASM binary directly using Binaryen rather than
   going through LLVM

3. **Leverage existing IR** -- Consume `IRProgram` from `IRGenerator`, the same IR used by
   `TargetCodeGenerator`

4. **Maintain separation of concerns** -- Keep the WASM backend independent of the CoreVM
   interpreter

## Architecture Overview

```
Source Code (.endo)
       |
       v
    Lexer --> Parser --> AST --> IRGenerator
                                    |
                                    v
                               IRProgram (SSA-form IR)
                                    |
                    +---------------+----------------+
                    v                                v
          TargetCodeGenerator                WasmCodeGenerator (NEW)
                    |                                |
                    v                                v
           CoreVM Bytecode                     .wasm / .wat
                    |
                    v
              Runner (VM)
```

The `WasmCodeGenerator` consumes the same `IRProgram` as the existing `TargetCodeGenerator`,
implementing the `InstructionVisitor` interface to translate IR instructions to WASM
equivalents.

## Phase 1: Foundation

### 1.1 CLI Extension

- [ ] Add `-o, --output <FILE>` argument parsing
- [ ] Detect output format from file extension (`.wasm`, `.wat`)
- [ ] Add `--emit-wat` flag for explicit WAT output
- [ ] Skip execution when `-o` is specified; compile instead

### 1.2 Binaryen Integration

- [ ] Add Binaryen as a CMake dependency (FetchContent or submodule)
- [ ] Create `WasmModule` wrapper class
- [ ] Create helper functions for common WASM patterns
- [ ] Add build configuration for WASM target

### 1.3 WasmCodeGenerator Skeleton

- [ ] Create code generator implementing `InstructionVisitor`
- [ ] Binary output (`generate()`) and text output (`generateWat()`)
- [ ] Configurable: WASI enable/disable, debug info, optimization level

## Phase 2: Core Language Mapping

### 2.1 Type Mapping

| CoreVM LiteralType | WASM Type | Notes |
|-------------------|-----------|-------|
| `Void` | (none) | No return value |
| `Boolean` | `i32` | 0 = false, 1 = true |
| `Number` | `i64` | 64-bit integer |
| `String` | `i32` | Pointer to linear memory |
| `Handler` | `funcref` | Function reference |

### 2.2 Instruction Mapping

**Arithmetic:**

| CoreVM | WASM |
|--------|------|
| `IAdd` | `i64.add` |
| `ISub` | `i64.sub` |
| `IMul` | `i64.mul` |
| `IDiv` | `i64.div_s` |
| `IRem` | `i64.rem_s` |
| `IPow` | Runtime function call |
| `INeg` | `i64.sub(0, x)` |

**Comparison:**

| CoreVM | WASM |
|--------|------|
| `ICmpEQ` | `i64.eq` |
| `ICmpNE` | `i64.ne` |
| `ICmpLT` | `i64.lt_s` |
| `ICmpGT` | `i64.gt_s` |
| `ICmpLE` | `i64.le_s` |
| `ICmpGE` | `i64.ge_s` |

**Control Flow:**

| CoreVM | WASM |
|--------|------|
| `Br` | `br` |
| `CondBr` | `br_if` / `if-else` |
| `Ret` | `return` |
| `Match` | `br_table` |

### 2.3 Control Flow Structuring

WASM uses structured control flow (no arbitrary gotos). The SSA-form IR with BasicBlocks
needs conversion via Binaryen's built-in Relooper or a dominator-based approach.

- [ ] Implement arithmetic, comparison, and boolean instruction visitors
- [ ] Implement control flow mapping
- [ ] Add unit tests for instruction mapping

## Phase 3: Memory Management

### 3.1 Memory Layout

```
Linear Memory:
  0x0000 - 0x0FFF : Reserved (null pointer guard)
  0x1000 - 0x1FFF : Global data (constants, static strings)
  0x2000 - ...    : Stack (grows upward)
  ...    - 0xFFFF : Heap (grows downward via allocator)
```

### 3.2 String Representation

Strings stored as length-prefixed UTF-8 (no null terminator).

### 3.3 Runtime Support Functions

Memory management (`alloc`, `free`), string operations (`concat`, `length`, `compare`,
`substring`), type conversions (`int_to_string`, `string_to_int`), and math helpers
(`int_pow`).

- [ ] Implement memory allocator
- [ ] Implement string creation and manipulation
- [ ] Implement string comparison functions
- [ ] Add constant pool memory

## Phase 4: WASI Integration

Map shell operations to WebAssembly System Interface functions:

| Shell Operation | WASI Function(s) |
|----------------|------------------|
| Environment variables | `environ_sizes_get`, `environ_get` |
| File read/write | `fd_read`, `fd_write` |
| File open/close | `path_open`, `fd_close` |
| Current directory | `fd_prestat_get`, `fd_prestat_dir_name` |
| Exit | `proc_exit` |
| Arguments | `args_sizes_get`, `args_get` |
| Time | `clock_time_get` |

For browser execution without WASI, provide JavaScript import stubs that map to
`console.log`, `prompt()`, etc.

- [ ] Create WASI import declarations
- [ ] Implement builtin-to-WASI mapping
- [ ] Create JavaScript polyfill for browser mode

## Phase 5: Process Execution (Advanced)

WebAssembly has fundamental limitations for shell operations:

- No `fork()`/`exec()` -- cannot spawn child processes natively
- No pipes between processes
- No job control (signals, process groups)

For WASI runtimes that support it (wasmtime with preview2), process execution uses
`wasi:cli/run` and `wasi:io/streams`. Unsupported environments emit clear runtime errors.

- [ ] Identify all process-related IR patterns
- [ ] Implement WASI-based process execution
- [ ] Implement graceful fallbacks
- [ ] Add `--no-shell-ops` / `--portable` flags

## Phase 6: Optimization

### IR Optimization Passes

Run existing CoreVM passes before WASM generation: empty block elimination, unused
instruction elimination, constant folding, linear branch elimination, block merging.

### WASM-Specific Optimizations

Use Binaryen's optimizer:

```bash
endo -O0 -o script.wasm script.endo  # No optimization
endo -Os -o script.wasm script.endo  # Optimize for size
endo -O2 -o script.wasm script.endo  # Optimize for speed
```

## Phase 7: Testing and Validation

### Unit Tests

Test arithmetic, string operations, control flow, memory management, and WASI calls.

### Integration Tests

Compile to WASM and run with `wasmtime`, verifying identical output to the interpreter:

```bash
endo -o test.wasm test.endo
wasmtime test.wasm    # Should produce same output as: endo test.endo
```

### Validation

Use `wasm-validate` to ensure generated WASM is well-formed.

## Milestones

| Milestone | Target | Status |
|-----------|--------|--------|
| W1: Basic WASM Generation | `let x = 1 + 2 * 3` compiles to valid WASM | Not started |
| W2: Control Flow and Functions | FizzBuzz compiles to WASM | Not started |
| W3: Full Language Support | All pure-computation features work | Not started |
| W4: WASI Integration | Simple shell scripts work in wasmtime | Not started |
| W5: Production Ready | Optimized, tested, documented | Not started |

## Dependencies

| Dependency | Purpose | Minimum Version |
|-----------|---------|-----------------|
| Binaryen | WASM generation and optimization | >= 119 |
| wabt (optional) | WAT output, validation | >= 1.0.34 |
| wasmtime (test) | Integration testing | >= 20.0 |

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Binaryen API complexity | Medium | Start with simple subset, expand incrementally |
| Control flow structuring | High | Use Binaryen's built-in Relooper |
| String performance in WASM | Medium | Optimize common patterns |
| WASI coverage gaps | High | Document limitations clearly |
| Process execution impossibility | High | Clear error messages, portable mode |

## Success Criteria

1. **Correctness:** WASM output produces identical results to interpreter for supported features
2. **Portability:** Generated WASM runs in wasmtime, Node.js, and browsers (portable mode)
3. **Performance:** WASM execution is at least as fast as interpreted execution
4. **Code size:** Generated WASM is reasonably compact (< 10x source size)
5. **Error handling:** Clear compile-time errors for unsupported features
