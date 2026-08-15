# WebAssembly Compilation

Endo can compile scripts to WebAssembly instead of executing them:

```bash
endo -o hello.wasm hello.endo    # compile to a WASI command module
endo -o hello.wat hello.endo     # compile to WebAssembly text format (for inspection)
```

The output is a self-contained [WASI](https://wasi.dev/) Preview 1 command
module: it exports `_start` and `memory`, imports only `fd_write` and
`proc_exit`, and runs under any WASI runtime:

```bash
$ echo 'println "hello, world"' > hello.endo
$ endo -o hello.wasm hello.endo
$ wasmtime hello.wasm
hello, world
```

Exit codes follow shell semantics: an explicit `exit N`, a failing command
status, or the script's final status all propagate to the WASM process exit
code, so `wasmtime script.wasm; echo $?` matches `endo script.endo; echo $?`.

## Options

| Option | Effect |
|--------|--------|
| `-o, --output FILE` | Compile to `FILE`; the format is derived from the extension (`.wasm`, `.wat`) |
| `-O` | Run binaryen's optimizer over the generated module (smaller and faster output) |
| `--wasm-no-tail-call` | Lower tail calls as plain calls for runtimes without the tail-call proposal (deep recursion may overflow the stack) |

## Supported language features

The backend compiles the pure-computation core of the language:

- Integers, floats, booleans and strings, including string interpolation
- Arithmetic, comparisons, boolean logic (division by zero is a runtime
  error, as in the interpreter)
- `if`/`else`, loops and pattern matching (literals, constructors, cons
  patterns, wildcards, string matches)
- User-defined functions, including `let rec`; tail calls compile to the
  WASM tail-call instruction, so deep recursion runs in constant stack space
- Option, Result, tuples and lists, with the same display formatting as the
  interpreter (`[1; 2; 3]`, `Some 42`, `("a", 5, true)`)
- Higher-order pipelines (`map`, `filter`, `fold`, `reverse`, `take`, ...)
  and the list builtins `length`, `head`, `tail`, `nth`, `isEmpty`, `@`
- `print`/`println` via WASI `fd_write`; `exit` via `proc_exit`

Unsupported constructs are reported as **compile-time errors** with source
locations — all of them in one run, not just the first:

```
$ echo 'ls -l' > script.endo
$ endo -o script.wasm script.endo
script.endo:1:1: type error: cannot compile to WebAssembly: builtin 'internal.setup_redirects(I)V' is not supported
```

Currently unsupported: shell command execution and pipes, file I/O, regular
expressions, closures as first-class values, lazy sequences, `fetch`,
environment variables, and most of the string/JSON/path standard library.
See `ROADMAP-WASM.md` in the repository for the full status.

## Known differences from the interpreter

The compiled module reimplements the runtime inside WebAssembly; a few edge
cases intentionally differ:

- Runtime errors print `endo: runtime error: <message>` to stderr without
  source locations, and always exit with code 1.
- Memory is bump-allocated and never freed. This is fine for scripts, but a
  long-running hot loop that allocates (string concatenation, list building)
  grows memory monotonically.
- `string -> int` conversion parses permissively into 64 bits (the
  interpreter's `std::stoi` is 32-bit and throwing).
- Float `**` requires an integral exponent; float remainder may differ in
  the last bit for extreme magnitudes.
- `INT64_MIN / -1` yields `INT64_MIN` instead of aborting.
- Record values print as `<object>` instead of their field listing.
- Printing a *dynamically typed* integer whose value collides with a valid
  heap address can misformat (the interpreter tracks allocations exactly;
  the WASM runtime uses a header heuristic). Statically typed values are
  unaffected.

## Testing

E2E tests live in `tests/wasm/` and use the `# mode: wasm` directive: each
test compiles the script and runs it under `wasmtime`, comparing output and
exit code. They are skipped when `wasmtime` is not installed (set
`ENDO_WASMTIME` to point at a specific binary).

## Build requirements

The WASM backend requires the [binaryen](https://github.com/WebAssembly/binaryen)
library at build time (`dnf install binaryen` / `apt install binaryen`).
Without it, endo builds normally and `-o` reports that the backend is
unavailable. The backend is also disabled for static builds (binaryen ships
as a shared library only).

Because binaryen is a shared library, it must remain installed at runtime as
well. Some distributions (Fedora among them) place `libbinaryen.so` in a
private directory such as `/usr/lib64/binaryen`, which the dynamic loader does
not search; the build records that directory in the installed executable's
RPATH so that `endo` starts without any `LD_LIBRARY_PATH` setup.
