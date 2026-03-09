# Memory Model

Endo uses a hybrid memory management strategy: **deterministic reference counting** for most heap objects, with a backup **mark-and-sweep garbage collector** for cycle detection when mutable references are introduced.

## Value Types

Primitive values — `int`, `float`, `bool`, and `unit` — live directly on the VM stack as 64-bit slots. They require no heap allocation and have zero overhead for creation or destruction.

| Type    | Representation                          |
|---------|-----------------------------------------|
| `int`   | 64-bit signed integer                   |
| `float` | 64-bit double via `std::bit_cast`       |
| `bool`  | 64-bit integer (0 = false, 1 = true)    |
| `unit`  | 64-bit integer (0)                      |
| `str`   | Pointer to arena-allocated `CoreString` |

## Heap Objects

Compound values — lists, tuples, options, results, records, and discriminated unions — are represented as `TypedObject` instances allocated on the heap. Each object has:

- A **type descriptor** pointer (identifies the type, slot count, variant info)
- A **reference count** (for deterministic lifetime management)
- A **variant tag** (for sum types like Option, Result)
- **Slots** (an array of 64-bit values holding fields/elements)

```
┌──────────────────────────────────┐
│ TypedObject                      │
├──────────────────────────────────┤
│ type: TypeDescriptor*    (8 B)   │
│ refCount: atomic<uint32> (4 B)   │
│ tag: uint8_t             (1 B)   │
│ padding: uint8_t[3]      (3 B)   │
│ slots[0..N]: uint64_t    (8 B ea)│
└──────────────────────────────────┘
```

Objects are allocated via `Runner::allocObject()` and tracked in the runner's object pool.

## Reference Counting

Endo uses **scope-based reference counting** with two VM opcodes:

- **`ORETAIN`** — increments the reference count of an object
- **`ORELEASE`** — decrements the reference count; frees the object when it reaches zero

The IR generator automatically inserts retain/release pairs at scope boundaries. When a `let` binding goes out of scope, its object (if any) is released. This provides **deterministic destruction** in LIFO order — resources are freed as soon as they become unreachable, without waiting for a GC cycle.

### Scope-Based Release

```fsharp
let x = Some 42       // ORETAIN on the Option object
let y = [1; 2; 3]     // ORETAIN on the List object
// ... use x and y ...
// scope exit: ORELEASE y, then ORELEASE x (LIFO order)
```

### Implementation Details

- **Recursive child release**: When an object's reference count reaches zero, `releaseAndFree()` uses an iterative worklist to recursively release all child object pointers before freeing the parent.
- **Object pool**: `ObjectPool` provides O(1) allocation and deallocation via size-class slabs with intrusive free lists, replacing the previous linear-scan approach.
- **String arena**: Strings are allocated in an arena that is freed in bulk when the runner is destroyed, rather than individually reference-counted.

## Scoped Resource Management

The `let use` binding provides deterministic resource cleanup tied to scope exit:

```fsharp
let use fd = File.open "data.txt" "r"
let content = File.readAll fd
// fd is automatically closed when scope exits
```

This works through the type system:

1. Types with a `disposeCallbackName` (e.g., `FileHandle` has `"file_close"`) are **disposable**.
2. Binding a disposable type with plain `let` is a compile-time error — you must use `let use` or `let manual`.
3. At scope exit, `let use` bindings invoke their dispose callback in LIFO order, before the normal `ORELEASE`.

The `let manual` escape hatch opts out of automatic disposal for cases where the caller manages the resource lifetime explicitly.

## Strings

Strings (`CoreString`) are currently allocated in an arena managed by the runner. All strings created during execution persist until the runner is destroyed. This is simple and avoids use-after-free, but means string memory is not reclaimed during long-running sessions.

**Current and planned improvements:**

- O(1) string pointer validation via `std::unordered_set` (implemented)
- Per-string reference counting or integration with the GC (planned)

## Cycle Collection

### Why Cycles Matter

With only immutable data, reference counting is sufficient — no cycles can form. A value can point to older values, but never back to itself or to something that points to it.

However, **mutable references** (`ref<T>`, planned for Phase 13) break this invariant:

```fsharp
// Hypothetical cycle (requires ref cells):
let a = ref None
let b = ref (Some a)
a <- Some b   // a → b → a — cycle!
```

Without a cycle collector, the objects in a cycle would never reach a reference count of zero, causing a memory leak.

### Mark-and-Sweep Design

Endo's cycle collector uses a **mark-and-sweep** algorithm:

1. **Root enumeration**: Walk the VM stack and global variables, identifying all directly reachable objects.
2. **Mark phase**: Starting from roots, traverse object slots iteratively (worklist-based, no C++ recursion) and mark each reachable object.
3. **Sweep phase**: Iterate all objects in the pool; free any unmarked objects.

The collector is triggered by allocation count thresholds, configurable via `RuntimeConfig`:

| Setting            | Description                              | Default |
|--------------------|------------------------------------------|---------|
| `gcEnabled`        | Whether the cycle collector is active    | `true`  |
| `gcThreshold`      | Allocation count before triggering GC    | 10000   |
| `gcMemoryThreshold` | Byte threshold before triggering GC     | 10 MB   |

Since Endo is immutable by default, cycles can **only** arise through `ref<T>` mutation. The collector is a safety net, not the primary memory management strategy.

### Write Barriers

When a mutable slot is written (`<-` on a `ref<T>`), a **write barrier** records the modified object as a potential cycle root. This allows the collector to focus its work on the subset of objects that could participate in cycles, rather than scanning all live objects.

## Ref Cells (Planned)

Ref cells introduce controlled mutability into Endo's otherwise immutable data model:

| Syntax          | Meaning                                  |
|-----------------|------------------------------------------|
| `ref expr`      | Create a new mutable reference cell      |
| `!expr`         | Dereference (read the current value)     |
| `expr <- expr`  | Mutate (write a new value)               |

```fsharp
let counter = ref 0
counter <- !counter + 1
print !counter   // 1
```

Ref cells are the **only** source of mutability for heap objects. This design keeps the language predominantly functional while providing an escape hatch for stateful algorithms.

!!! note
    Ref cells cannot be destructured in pattern matching. To match on the contained value, dereference first: `match !r with | ...`.

## Summary

| Aspect              | Current                                          | Planned                           |
|---------------------|--------------------------------------------------|-----------------------------------|
| Stack values        | Zero-cost, no allocation                         | —                                 |
| Heap objects        | Scope-based RC + object pool + recursive release | —                                 |
| Strings             | Arena (bulk free) + O(1) pointer validation      | Per-string RC                     |
| Cycle collection    | Mark-and-sweep (triggered when suspects exist)   | —                                 |
| Resource cleanup    | `let use` with dispose callbacks                 | —                                 |
| Mutable references  | Not available                                    | `ref<T>` with write barriers      |
