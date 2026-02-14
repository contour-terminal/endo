# Modules & Imports

### 12.1 File-based Modules

```endo
# Import another endo script
import "./utils.endo"
import "../lib/helpers.endo"

# Import with alias
import "./database.endo" as db
db.connect "localhost"
db.query "SELECT * FROM users"

# Selective import
from "./math.endo" import (add, multiply, PI)
from "./utils.endo" import *

# Conditional import
if production then
    import "./config.prod.endo" as config
else
    import "./config.dev.endo" as config
fi
```

### 12.2 Standard Library

```endo
# Core functions are always available
# No import needed for: map, filter, fold, head, tail, etc.

# Module-qualified access for clarity
let numbers = [1; 2; 3; 4; 5]
let sum = List.fold 0 (+) numbers
let doubled = List.map (fun x -> x * 2) numbers

let text = "hello,world,foo"
let parts = String.split "," text     # ["hello"; "world"; "foo"]
let joined = String.join "-" parts    # "hello-world-foo"

# File operations
let content = File.read "data.txt"
File.write "output.txt" processedData
let files = File.list "."

# Path operations
let full = Path.join [homeDir; "documents"; "file.txt"]
let ext = Path.extension "file.txt"   # ".txt"
let base_ = Path.basename "/a/b/c.txt" # "c.txt"
```

### 12.3 HTTP: `fetch`

The `fetch` builtin downloads a URL to a file in the current working directory,
streaming the response directly to disk without holding the entire body in memory.
The return type is `result<str, str>`:

- **`Ok filename`** -- the local filename where the response was saved (relative to CWD).
- **`Error msg`** -- a human-readable error message. This covers both transport-level
  failures (DNS resolution, connection refused, timeout) and
  HTTP-level errors (non-2xx status codes produce `"HTTP 404"`, `"HTTP 500"`, etc.).

The filename is derived from the URL path (last non-empty segment, query/fragment stripped).
If the URL has no filename (e.g. `https://example.com/`), a unique name is generated
automatically (e.g. `fetch_a1b2c3`). Existing files with the same name are overwritten.

```endo
# Signature
fetch : str -> result<str, str>
fetch : str -> list<str> -> result<str, str>
```

**Basic download:**

```endo
# Downloads to ./data.json, returns Ok "data.json"
let result = fetch "https://api.example.com/data.json"

match result with
| Ok path  -> println ("Saved to: " + path)
| Error msg -> println ("Error: " + msg)
```

**Unwrap with `?` operator** -- propagates `Error` automatically:

```endo
let path = (fetch "https://api.example.com/data.json")?
println ("Downloaded: " + path)
```

**Custom request headers** -- pass a list of `"Key: Value"` strings as the second argument:

```endo
let path = (fetch "https://example.com/file.tar.gz"
                  ["Authorization: Bearer tok"; "Accept: application/octet-stream"])?
```

**Pipeline usage:**

```endo
fetch "https://api.example.com/data.json" |> print
```

**Progress bar:** When the shell is interactive and stderr is a TTY, `fetch` displays a
progress bar on stderr. It is automatically suppressed when stdout is piped or when the
environment variable `ENDO_FETCH_QUIET=1` is set.

**Limits:**

| Property | Value |
|----------|-------|
| HTTP method | GET only (POST/PUT/etc. planned) |
| Response size | No limit (streams directly to disk) |
| Redirects | Followed automatically (up to 10) |
| Timeout | None by default (libcurl default) |

### 12.4 Creating Modules

```endo
# mymodule.endo

# Public exports (default)
let add x y = x + y
let multiply x y = x * y

# Private (underscore prefix convention)
let _helper x = x * 2

# Export types
type Point = { x: float; y: float }

# Module initialization
let _init = echo "Module loaded"
```

---
**See also:** [Functions](functions.md) | [Interoperability](interoperability.md) | [Error Handling](error-handling.md)
