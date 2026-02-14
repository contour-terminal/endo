# Fetch

Demonstrates Endo's built-in `fetch` function for downloading files from the internet, combined with the `?` operator for error propagation.

```endo
// Downloading files with fetch
//
// fetch downloads a URL to a file in the current working directory
// and returns result<str, str> where Ok contains the local filename.

// Download a file
let path = (fetch "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.19.tar.xz")?
println ("Downloaded to: " + path)
```

## Key Techniques

- **`fetch`** is a built-in function that downloads a URL to a file in the current working directory. It returns a `result<str, str>` where `Ok` contains the local filename and `Error` contains an error message.
- **The `?` operator** unwraps the `Ok` value from `fetch`. If the download fails, the error is propagated to the caller immediately, and the remaining code is not executed.
- **String concatenation** combines the result with a descriptive prefix for display.
- This example shows how Endo bridges shell-like convenience (downloading files) with functional safety (Result types and error propagation). In a traditional shell, a failed download might silently produce an empty variable; in Endo, the error is explicit and must be handled.

## See Also

- [Error Handling](../language/error-handling.md)
- [Command Execution](../language/command-execution.md)
