# Endo WebAssembly Compilation Roadmap

## Executive Summary

This document outlines the plan to add WebAssembly (WASM) compilation support to Endo. The goal is to enable compiling Endo scripts to `.wasm` files that can run in browsers, edge computing environments, or any WASM runtime.

**Target Invocation:**
```bash
endo -o hello.wasm hello.endo     # Compile to WebAssembly
endo -o hello.wat hello.endo      # Compile to WebAssembly Text format (for debugging)
```

## Design Goals

1. **Dual-mode support:**
   - **Portable mode** - Pure computation scripts for browsers/edge (no shell operations)
   - **WASI mode** - Full shell functionality using WASI for system calls

2. **Direct WASM emission** - Generate WASM binary directly using a library (binaryen) rather than going through LLVM

3. **Leverage existing IR** - Consume `IRProgram` from `IRGenerator`, the same IR used by `TargetCodeGenerator`

4. **Maintain separation of concerns** - Keep the WASM backend independent of the CoreVM interpreter

---

## Architecture Overview

```
Source Code (.endo)
       |
       v
    Lexer
       |
       v
    Parser
       |
       v
      AST
       |
       v
  IRGenerator
       |
       v
   IRProgram (SSA-form IR)
       |
       +-------------------------+
       v                         v
TargetCodeGenerator         WasmCodeGenerator (NEW)
       |                         |
       v                         v
 CoreVM Bytecode              .wasm / .wat
       |
       v
   Runner (VM)
```

### Key Components

| Existing Component | Purpose | WASM Relevance |
|-------------------|---------|----------------|
| `IRProgram` | SSA-form intermediate representation | **Input to WASM generator** |
| `IRHandler` | Function-level IR container | Maps to WASM functions |
| `BasicBlock` | Control flow unit | Maps to WASM structured control flow |
| `Instr` hierarchy | Individual IR instructions | Maps to WASM instructions |
| `NativeCallback` | Native function bindings | Maps to WASM imports (WASI) |

---

## Phase 1: Foundation

**Goal:** Establish infrastructure for WASM code generation.

### 1.1 CLI Extension

**Location:** `src/shell/main.cpp`

Add command-line handling for `-o` output flag:

```cpp
struct ParsedArgs {
    // ... existing fields ...
    std::string_view outputFile;  // NEW: output file path
    enum class OutputFormat { Execute, Wasm, Wat } outputFormat = Execute;
};
```

**Tasks:**
- [ ] Add `-o, --output <FILE>` argument parsing
- [ ] Detect output format from file extension (`.wasm`, `.wat`)
- [ ] Add `--emit-wat` flag for explicit WAT output
- [ ] Skip execution when `-o` is specified; compile instead

### 1.2 Binaryen Integration

**Location:** `src/CoreVM/wasm/` (new directory)

Integrate the [Binaryen](https://github.com/WebAssembly/binaryen) library for WASM generation:

**Tasks:**
- [ ] Add binaryen as a CMake dependency (FetchContent or submodule)
- [ ] Create `WasmModule` wrapper class around `wasm::Module`
- [ ] Create helper functions for common WASM patterns
- [ ] Add build configuration for WASM target

**CMake Integration:**
```cmake
# src/CoreVM/wasm/CMakeLists.txt
FetchContent_Declare(
  binaryen
  GIT_REPOSITORY https://github.com/WebAssembly/binaryen.git
  GIT_TAG version_119  # or latest stable
)
FetchContent_MakeAvailable(binaryen)
```

### 1.3 WasmCodeGenerator Skeleton

**Location:** `src/CoreVM/wasm/WasmCodeGenerator.hpp/cpp`

Create the code generator following the existing `TargetCodeGenerator` pattern:

```cpp
class WasmCodeGenerator : public InstructionVisitor {
public:
    struct Options {
        bool enableWasi = true;       // Enable WASI imports
        bool debugInfo = false;       // Include debug info
        bool optimizationLevel = 0;   // 0=none, 1=size, 2=speed
    };

    explicit WasmCodeGenerator(Options options = {});
    
    std::vector<uint8_t> generate(IRProgram* program);  // Returns WASM binary
    std::string generateWat(IRProgram* program);        // Returns WAT text
    
protected:
    // InstructionVisitor overrides
    void visit(AllocaInstr& instr) override;
    void visit(LoadInstr& instr) override;
    void visit(StoreInstr& instr) override;
    // ... all other instruction types ...
    
private:
    wasm::Module _module;
    Options _options;
};
```

---

## Phase 2: Core Language Mapping

**Goal:** Map CoreVM IR types and instructions to WebAssembly equivalents.

### 2.1 Type Mapping

| CoreVM LiteralType | WASM Type | Notes |
|-------------------|-----------|-------|
| `Void` | (none) | No return value |
| `Boolean` | `i32` | 0 = false, 1 = true |
| `Number` | `i64` | 64-bit integer |
| `String` | `i32` | Pointer to linear memory |
| `IPAddress` | `i32` | Pointer to struct in memory |
| `Cidr` | `i32` | Pointer to struct in memory |
| `RegExp` | `i32` | Handle to runtime object |
| `Handler` | `funcref` | Function reference |
| `IntArray` | `i32` | Pointer to array in memory |
| `StringArray` | `i32` | Pointer to array of pointers |

### 2.2 Instruction Mapping

#### Arithmetic Operations
| CoreVM Instruction | WASM Instruction |
|-------------------|------------------|
| `IAddInstr` | `i64.add` |
| `ISubInstr` | `i64.sub` |
| `IMulInstr` | `i64.mul` |
| `IDivInstr` | `i64.div_s` |
| `IRemInstr` | `i64.rem_s` |
| `IPowInstr` | (runtime function call) |
| `IShlInstr` | `i64.shl` |
| `IShrInstr` | `i64.shr_s` |
| `IAndInstr` | `i64.and` |
| `IOrInstr` | `i64.or` |
| `IXorInstr` | `i64.xor` |
| `INegInstr` | `i64.sub(0, x)` |
| `INotInstr` | `i64.xor(x, -1)` |

#### Comparison Operations
| CoreVM Instruction | WASM Instruction |
|-------------------|------------------|
| `ICmpEQInstr` | `i64.eq` |
| `ICmpNEInstr` | `i64.ne` |
| `ICmpLTInstr` | `i64.lt_s` |
| `ICmpGTInstr` | `i64.gt_s` |
| `ICmpLEInstr` | `i64.le_s` |
| `ICmpGEInstr` | `i64.ge_s` |

#### Boolean Operations
| CoreVM Instruction | WASM Instruction |
|-------------------|------------------|
| `BAndInstr` | `i32.and` |
| `BOrInstr` | `i32.or` |
| `BXorInstr` | `i32.xor` |
| `BNotInstr` | `i32.eqz` |

#### Control Flow
| CoreVM Instruction | WASM Construct |
|-------------------|----------------|
| `BrInstr` | `br` |
| `CondBrInstr` | `br_if` / `if-else` |
| `RetInstr` | `return` |
| `MatchInstr` | `br_table` |

**Tasks:**
- [ ] Implement arithmetic instruction visitors
- [ ] Implement comparison instruction visitors  
- [ ] Implement boolean instruction visitors
- [ ] Implement control flow mapping
- [ ] Add unit tests for instruction mapping

### 2.3 Control Flow Structuring

WASM uses structured control flow (no arbitrary gotos). The SSA-form IR with BasicBlocks needs to be converted:

**Algorithm:** Relooper or Stackifier
- Use binaryen's built-in CFG-to-structured-control-flow converter
- Or implement a simple dominator-based approach for basic cases

```cpp
// Example: Converting IR control flow to WASM structured control flow
void WasmCodeGenerator::generateFunction(IRHandler* handler) {
    // Option 1: Use binaryen's RelooperBuilder
    auto relooper = wasm::RelooperBuilder(_module);
    
    // Map BasicBlocks to Relooper blocks
    std::map<BasicBlock*, wasm::Relooper::Block*> blockMap;
    for (auto* bb : handler->basicBlocks()) {
        auto* block = relooper.addBlock(generateBlockCode(bb));
        blockMap[bb] = block;
    }
    
    // Add branches
    for (auto* bb : handler->basicBlocks()) {
        // ... add branch edges ...
    }
    
    // Render to structured control flow
    auto* body = relooper.render(blockMap[handler->getEntryBlock()]);
}
```

---

## Phase 3: Memory Management

**Goal:** Implement linear memory layout for complex types.

### 3.1 Memory Layout

```
+-------------------------------------------------------------+
| Linear Memory                                                |
+-------------------------------------------------------------+
| 0x0000 - 0x0FFF : Reserved (null pointer guard)             |
+-------------------------------------------------------------+
| 0x1000 - 0x1FFF : Global data (constants, static strings)   |
+-------------------------------------------------------------+
| 0x2000 - ...    : Stack (grows upward)                      |
+-------------------------------------------------------------+
| ...    - 0xFFFF : Heap (grows downward via allocator)       |
+-------------------------------------------------------------+
```

### 3.2 String Representation

Strings stored as length-prefixed UTF-8:
```
+----------+--------------------------------+
| i32 len  | UTF-8 bytes (no null term)     |
+----------+--------------------------------+
```

**Tasks:**
- [ ] Implement memory allocator (bump allocator for simple cases)
- [ ] Implement string creation and manipulation functions
- [ ] Implement string comparison functions
- [ ] Add memory for constant pool (string literals, regex patterns)

### 3.3 Runtime Support Functions

Create a WASM runtime library with helper functions:

```cpp
// Functions to be compiled into the WASM module
namespace WasmRuntime {
    // Memory management
    i32 alloc(i32 size);
    void free(i32 ptr);
    
    // String operations  
    i32 string_concat(i32 str1, i32 str2);
    i32 string_length(i32 str);
    i32 string_compare(i32 str1, i32 str2);
    i32 string_substring(i32 str, i32 start, i32 len);
    
    // Integer <-> String conversion
    i32 int_to_string(i64 value);
    i64 string_to_int(i32 str);
    
    // Power function (no native i64.pow in WASM)
    i64 int_pow(i64 base, i64 exp);
    
    // Regex (if supported)
    i32 regex_match(i32 pattern, i32 str);
    i32 regex_group(i32 match, i32 groupId);
}
```

---

## Phase 4: WASI Integration

**Goal:** Enable shell operations via WebAssembly System Interface.

### 4.1 WASI Imports

Map shell operations to WASI functions:

| Shell Operation | WASI Function(s) |
|----------------|------------------|
| Environment variables | `environ_sizes_get`, `environ_get` |
| File read | `fd_read` |
| File write | `fd_write` |
| File open | `path_open` |
| File close | `fd_close` |
| Current directory | `fd_prestat_get`, `fd_prestat_dir_name` |
| Exit | `proc_exit` |
| Arguments | `args_sizes_get`, `args_get` |
| Random | `random_get` |
| Time | `clock_time_get` |

### 4.2 Native Callback Mapping

Map `NativeCallback` to WASI imports or embedded functions:

```cpp
// Builtin to WASI mapping
struct WasiMapping {
    std::string callbackSignature;
    std::string wasiFunction;
    // or:
    std::function<void(WasmCodeGenerator&, CallInstr&)> generator;
};

// Example mappings
const std::vector<WasiMapping> wasiMappings = {
    {"exit(Number)", "proc_exit"},
    {"echo(String)", embedded_echo},  // Uses fd_write internally
    {"env_get(String): String", embedded_env_get},
    {"env_set(String, String)", embedded_env_set},
};
```

### 4.3 Portable Mode Fallbacks

For browser execution without WASI, provide JavaScript imports:

```javascript
// JavaScript import object for browser
const imports = {
    endo: {
        print: (strPtr) => { console.log(readString(memory, strPtr)); },
        read_line: () => { return writeString(memory, prompt()); },
        env_get: (keyPtr) => { /* no-op or throw */ },
    }
};
```

**Tasks:**
- [ ] Create WASI import declarations
- [ ] Implement builtin-to-WASI mapping table
- [ ] Generate proper WASI function calls
- [ ] Create JavaScript polyfill for browser mode
- [ ] Add WASI vs portable mode detection

---

## Phase 5: Process Execution (Advanced WASI)

**Goal:** Support external command execution where possible.

### 5.1 Limitations

WebAssembly has fundamental limitations for shell operations:
- **No `fork()`/`exec()`** - Cannot spawn child processes natively
- **No pipes between processes** - Process isolation is inherent
- **No job control** - No signals, no process groups

### 5.2 Workarounds

For WASI runtimes that support it (e.g., wasmtime with preview2):

| Shell Feature | WASI Preview 2 Approach |
|--------------|------------------------|
| External commands | `wasi:cli/run` interface |
| Pipes | `wasi:io/streams` |
| File redirection | `wasi:filesystem` + fd manipulation |

For unsupported environments:
- Emit runtime error with clear message
- Provide compile-time flag `--no-shell-ops` for pure computation mode

### 5.3 Compile-Time Detection

```cpp
class WasmCodeGenerator {
    void visit(HandlerCallInstr& call) override {
        if (isProcessExecution(call)) {
            if (_options.enableShellOps) {
                emitProcessExecution(call);  // Via WASI
            } else {
                emitError("External command execution not supported in portable mode");
            }
        }
    }
};
```

**Tasks:**
- [ ] Identify all process-related IR patterns
- [ ] Implement WASI-based process execution (where supported)
- [ ] Implement graceful fallback for unsupported operations
- [ ] Add `--no-shell-ops` / `--portable` flags

---

## Phase 6: Optimization

**Goal:** Generate efficient WASM code.

### 6.1 IR Optimization Passes

Run existing CoreVM optimization passes before WASM generation:
- `emptyBlockElimination`
- `eliminateUnusedInstr`
- `eliminateLinearBr`
- `foldConstantCondBr`
- `mergeSameBlocks`

### 6.2 WASM-Specific Optimizations

Use binaryen's optimizer:

```cpp
std::vector<uint8_t> WasmCodeGenerator::generate(IRProgram* program) {
    // Generate initial WASM
    generateModule(program);
    
    // Apply binaryen optimizations
    if (_options.optimizationLevel > 0) {
        wasm::PassRunner passRunner(&_module);
        if (_options.optimizationLevel == 1) {
            passRunner.addDefaultOptimizationPasses();  // Size-focused
        } else {
            passRunner.addDefaultGlobalOptimizationPasses();  // Speed-focused
        }
        passRunner.run();
    }
    
    // Serialize to binary
    return wasm::ModuleWriter().writeToBuffer(_module);
}
```

### 6.3 Optimization Flags

```bash
endo -O0 -o script.wasm script.endo  # No optimization
endo -Os -o script.wasm script.endo  # Optimize for size
endo -O2 -o script.wasm script.endo  # Optimize for speed
```

---

## Phase 7: Testing and Validation

### 7.1 Unit Tests

**Location:** `src/CoreVM/wasm/WasmCodeGenerator_test.cpp`

Test categories:
- Arithmetic operations
- String operations
- Control flow
- Memory management
- WASI function calls

### 7.2 Integration Tests

Run compiled WASM with reference runtime:

```bash
# Test script
endo -o test.wasm test.endo
wasmtime test.wasm  # Should produce same output as:
endo test.endo
```

### 7.3 Validation

Use `wasm-validate` from wabt to ensure generated WASM is valid:

```cpp
TEST(WasmCodeGenerator, ValidOutput) {
    auto wasm = generator.generate(program);
    EXPECT_TRUE(wasm::isValidModule(wasm));
}
```

---

## File Structure

```
src/CoreVM/wasm/
├── CMakeLists.txt           # WASM target build config
├── WasmCodeGenerator.hpp    # Main code generator class
├── WasmCodeGenerator.cpp    # Implementation
├── WasmRuntime.hpp          # Runtime support function declarations
├── WasmRuntime.cpp          # Runtime support implementations
├── WasmTypes.hpp            # Type mapping helpers
├── WasiImports.hpp          # WASI import definitions
├── WasiImports.cpp          # WASI mapping implementation
└── WasmCodeGenerator_test.cpp  # Unit tests
```

---

## Dependencies

| Dependency | Purpose | Version |
|-----------|---------|---------|
| binaryen | WASM generation and optimization | >= 119 |
| wabt (optional) | WAT output, validation | >= 1.0.34 |
| wasmtime (test) | Integration testing | >= 20.0 |

---

## Milestones

### Milestone W1: Basic WASM Generation
- [ ] Phase 1: CLI and binaryen integration
- [ ] Phase 2.1-2.2: Arithmetic and comparison operations
- [ ] Generate valid WASM for simple arithmetic expressions

**Target:** `let x = 1 + 2 * 3` compiles to valid WASM

### Milestone W2: Control Flow and Functions
- [ ] Phase 2.3: Control flow structuring
- [ ] Phase 3.1-3.2: Memory layout and strings
- [ ] Support if/else, while loops, function definitions

**Target:** FizzBuzz compiles to WASM

### Milestone W3: Full Language Support
- [ ] Phase 3.3: Runtime support functions
- [ ] String operations, regex (if feasible)
- [ ] All F# expression features

**Target:** All pure-computation Endo features work

### Milestone W4: WASI Integration
- [ ] Phase 4: WASI imports
- [ ] Phase 5: Process execution (limited)
- [ ] Environment variables, file I/O, exit codes

**Target:** Simple shell scripts work in wasmtime

### Milestone W5: Production Ready
- [ ] Phase 6: Optimizations
- [ ] Phase 7: Comprehensive testing
- [ ] Documentation and examples

**Target:** Production-quality WASM output

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Binaryen API complexity | Medium | Start with simple subset, expand |
| Control flow structuring | High | Use binaryen's relooper |
| String performance | Medium | Optimize common patterns |
| WASI coverage gaps | High | Document limitations clearly |
| Process execution impossibility | High | Clear error messages, portable mode |

---

## Success Criteria

1. **Correctness:** WASM output produces identical results to interpreter for supported features
2. **Portability:** Generated WASM runs in wasmtime, Node.js, and browsers (portable mode)
3. **Performance:** WASM execution is at least as fast as interpreted execution
4. **Code size:** Generated WASM is reasonably compact (< 10x source size for typical scripts)
5. **Error handling:** Clear compile-time errors for unsupported features

---

## Future Considerations

- **WASM GC proposal** - When standardized, could improve string/object handling
- **WASM Component Model** - Better interop with host environments
- **Source maps** - Debug info mapping WASM to Endo source
- **Streaming compilation** - For large scripts
- **AOT optimization** - Profile-guided optimization from interpreter traces

---

*This roadmap is a living document. Updates will occur as development progresses.*
