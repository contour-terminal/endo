---
title: Platform Differences
description: Windows vs POSIX differences in the Endo shell.
---

# Platform Differences

Endo runs on Linux, macOS, and Windows. Most features work identically across platforms,
but some low-level behaviors differ due to fundamental OS differences. This page documents
what Windows users should know.

---

## Signal Handling

### POSIX (Linux, macOS)

On POSIX systems, Endo uses standard Unix signals for process lifecycle management:

- **SIGCHLD** -- notification when a child process exits or stops
- **SIGTSTP** -- triggered by Ctrl+Z to suspend the foreground process
- **SIGCONT** -- resumes a stopped process
- **SIGINT** -- triggered by Ctrl+C to interrupt the foreground process
- **SIGTERM / SIGKILL** -- used to terminate processes

On Linux, signals are received via `signalfd` for efficient event-loop integration. On
macOS/BSD, traditional signal handlers set atomic flags that the main loop polls.

### Windows

Windows does not have POSIX signals. Endo uses native Windows APIs to achieve equivalent
behavior:

| POSIX Signal | Windows Equivalent |
|--------------|-------------------|
| `SIGINT` | `GenerateConsoleCtrlEvent(CTRL_C_EVENT, ...)` |
| `SIGTERM` | `TerminateProcess()` |
| `SIGKILL` | `TerminateProcess()` |
| `SIGTSTP` | `SuspendThread()` on all threads of the target process |
| `SIGCONT` | `ResumeThread()` on all threads of the target process |
| `SIGCHLD` | Polling via `WaitForSingleObject` / `WaitForMultipleObjects` |

Child process termination is detected by waiting on the process handle rather than
receiving a signal. The shell polls for child state changes using the Windows wait APIs.

---

## Shell Suspension (Ctrl+Z)

### POSIX

On POSIX systems, pressing **Ctrl+Z** sends `SIGTSTP` to the foreground process group. This
can suspend both child processes and the shell itself. When suspended, the parent shell (e.g.
Bash) takes back control. Typing `fg` in the parent shell resumes Endo.

### Windows

On Windows, **Ctrl+Z cannot suspend the Endo shell itself**. This is because:

1. Windows has no `SIGTSTP` signal.
2. Windows has no process groups in the POSIX sense -- there is no mechanism for a parent
   terminal to "stop" and later "continue" an arbitrary process the way Unix shells do.
3. There is no equivalent of `tcsetpgrp()` for transferring terminal foreground control.

**What works:**

- **Ctrl+C** -- interrupts the foreground child process
- **bg** -- resumes a suspended job in the background
- **fg** -- brings a background job to the foreground
- **Job management** -- suspending and resuming child processes with `SuspendThread` /
  `ResumeThread`

**What does not work:**

- Suspending the Endo shell itself with Ctrl+Z

**Workarounds:**

- Open multiple terminal tabs or windows instead of suspending and resuming the shell.
- Use the built-in job control (`bg`, `fg`, `jobs`) to manage child processes within Endo.

---

## Job Control

Job control (background processes, `bg`, `fg`, `jobs`) works on both platforms, but the
underlying implementation differs.

### POSIX

- Uses process groups (`setpgid`, `tcsetpgrp`) to manage foreground/background jobs.
- Sends `SIGTSTP` to stop a process group and `SIGCONT` to resume it.
- The terminal driver handles foreground group switching.

### Windows

- Process groups are tracked internally by Endo using a map of group membership.
- Suspension is implemented by enumerating all threads of a process
  (`CreateToolhelp32Snapshot`) and calling `SuspendThread` on each one.
- Resumption calls `ResumeThread` on each thread.
- There is no terminal foreground group concept; `setForegroundPgrp` and
  `getForegroundPgrp` are no-ops that return the current process ID.

These differences are transparent to the user -- `bg`, `fg`, and `jobs` commands work the
same way on both platforms.

---

## Process Substitution

Process substitution (`<(command)` and `>(command)`) is implemented on POSIX but **not yet
available on Windows**.

### POSIX

On Linux, process substitution uses `/dev/fd/<n>` or `/proc/self/fd/<n>` paths to expose a
pipe as a file path. The shell forks a child process to run the substituted command, connects
it to a pipe, and passes the file descriptor path to the parent command.

```endo
# Compare output of two commands
diff <(ls dir1) <(ls dir2)

# Use command output as input file
sort <(cat file1 file2)
```

### Windows

Process substitution is not yet implemented on Windows. Attempting to use `<(...)` or
`>(...)` will produce an error message:

```
Process substitution not implemented on Windows
```

A future implementation may use Windows named pipes (`CreateNamedPipe`) to provide equivalent
functionality. Named pipes on Windows can be opened by path (e.g. `\\.\pipe\endo-procsubst-<id>`)
and would allow child processes to read from or write to the pipe as if it were a file.

---

## Tilde Expansion (`~username`)

Tilde expansion for the current user (`~` and `~/path`) works on all platforms.

Expansion of other users' home directories (`~username`) differs in implementation:

### POSIX

Uses `getpwnam()` to look up the user's home directory from the system password database.
This provides an authoritative answer for any valid local user.

### Windows

Windows has no equivalent of `/etc/passwd` or `getpwnam()`. Endo uses a heuristic approach:

1. Reads the current user's profile path from the `USERPROFILE` environment variable
   (e.g. `C:\Users\alice`).
2. Derives the parent directory (`C:\Users`).
3. Checks whether `C:\Users\<username>` exists on disk.
4. If found, uses that path. Otherwise, returns the literal `~username` unexpanded.

This heuristic works well when all user profiles reside under the same parent directory,
which is the default Windows configuration. It will not resolve users whose profiles are
stored in non-standard locations.

---

## Environment Variables

### Case Sensitivity

Environment variable names are **case-sensitive** on POSIX and **case-insensitive** on
Windows. Endo's Windows environment provider uses case-insensitive comparison for lookups,
matching native Windows behavior.

```endo
# On Windows, these refer to the same variable:
echo $PATH
echo $Path
echo $path
```

### Home Directory

| Variable | POSIX | Windows |
|----------|-------|---------|
| Home directory | `$HOME` | `$USERPROFILE` (Endo also sets `$HOME`) |
| Config directory | `$XDG_CONFIG_HOME` or `~/.config` | `$APPDATA` |

---

## Process Creation

### POSIX

Uses `fork()` + `execve()` for process creation. Child processes inherit open file
descriptors. Process groups are set with `setpgid()`.

### Windows

Uses `CreateProcessW()` with explicit handle inheritance via `STARTUPINFOW`. The
`CREATE_NEW_PROCESS_GROUP` flag is used when the shell needs to manage a job as a separate
group. Command-line arguments are quoted according to Microsoft's C/C++ parameter parsing
rules.

---

## File Paths

Endo normalizes path handling across platforms, but some differences are visible:

| Aspect | POSIX | Windows |
|--------|-------|---------|
| Separator | `/` | `\` (but `/` also works) |
| Root | `/` | `C:\` (or other drive letter) |
| Null device | `/dev/null` | `NUL` (Endo also accepts `/dev/null`) |
| Path length limit | ~4096 bytes | ~260 characters (32,767 with long path support) |

Endo accepts forward slashes in paths on Windows for convenience.

### Null-device portability (`/dev/null` on Windows)

On POSIX systems, the null device is a regular file at `/dev/null`. On Windows the
equivalent is the reserved device name `NUL`, which is not a filesystem path and has
no counterpart under `C:\`. Shell scripts and pipelines that target `/dev/null` are
therefore not portable in general.

To keep commands portable between platforms, Endo rewrites the literal path
`/dev/null` to `NUL` on Windows at the point where a file is opened. This applies
consistently across:

- **Builtins that open user-supplied paths** — `tee`, `cat`, `tail`, and other
  text-processing commands that accept file arguments.
- **Shell redirections** — both input (`< /dev/null`) and output (`> /dev/null`,
  `>> /dev/null`) forms.

So the following all behave the same on Linux, macOS, and Windows:

```endo
echo hello | tee /dev/null          # tees to stdout only
cat /dev/null                       # prints nothing
grep pattern file > /dev/null       # discards stdout
some-command 2> /dev/null           # discards stderr
```

On Windows, `NUL` itself remains the canonical null device name and may still be
used directly (e.g. `echo hello > NUL`). The `/dev/null` alias is purely a
convenience for portability — no other POSIX `/dev/*` names are recognised.

#### Scope of the translation

The rewrite is intentionally narrow: only the exact literal string `/dev/null` is
mapped. Paths such as `/dev/nullx`, `/dev/null/file`, or `/dev/zero` are treated
as ordinary filesystem paths, which on Windows will fail to open as expected.

This translation happens inside the shell's file-open helpers — it is not a
preprocessing step, so user variables and argument strings are unaffected until
they are passed to a file operation. The implementation lives in
`endo::platform::resolveDevicePath()` in `src/platform/PathUtils.hpp`.

---

## Summary

| Feature | Linux / macOS | Windows |
|---------|--------------|---------|
| Shell pipes (`\|`) | Full support | Full support |
| Forward pipes (`\|>`) | Full support | Full support |
| Job control (`bg`, `fg`, `jobs`) | Full support | Full support (thread-based) |
| Ctrl+C (interrupt) | Full support | Full support |
| Ctrl+Z (suspend shell) | Full support | Not supported |
| Process substitution | Full support | Not yet implemented |
| Tilde expansion (`~`) | Full support | Full support |
| Tilde expansion (`~user`) | Full support | Heuristic-based |
| Signal handling | Native signals | Windows API equivalents |
| Default shell | `chsh` | Registry (OpenSSH) / Windows Terminal profile |
| Tab completion | Full support | Full support |
| Syntax highlighting | Full support | Full support |

---

## Setting Endo as the Default Shell

### POSIX (Linux, macOS)

On POSIX systems, use `chsh` to change your login shell. Endo must first be listed in
`/etc/shells`:

```bash
# Add Endo to the list of allowed login shells (requires root)
echo /usr/local/bin/endo | sudo tee -a /etc/shells

# Change your default shell
chsh -s /usr/local/bin/endo
```

The change takes effect on your next login. Adjust the path if you installed Endo to a
different location (e.g. `/usr/bin/endo`).

!!! warning
    Make sure Endo is working correctly before setting it as your login shell. A broken
    login shell can lock you out of your account. Keep a root shell or recovery option
    available.

### Windows

Windows does not have a single "default shell" concept like POSIX. Instead, different
contexts use different configuration mechanisms.

#### Windows Terminal Default Profile

To launch Endo when you open a new tab in Windows Terminal:

1. Open **Windows Terminal** and go to **Settings** (Ctrl+,).
2. Select **Add a new profile** and configure it:
    - **Name**: Endo
    - **Command line**: `C:\path\to\endo.exe`
    - **Starting directory**: `%USERPROFILE%`
3. Optionally set the new profile as the **Default profile** under **Startup**.

#### OpenSSH Default Shell

To make Endo the default shell for incoming SSH connections, set the `DefaultShell`
registry value under `HKLM:\SOFTWARE\OpenSSH`. Run the following in an **elevated**
(administrator) PowerShell prompt:

```powershell
New-ItemProperty -Path "HKLM:\SOFTWARE\OpenSSH" -Name DefaultShell `
    -Value "C:\path\to\endo.exe" -PropertyType String -Force
```

No restart of the SSH service is required -- the change takes effect on the next SSH login.

To verify the current setting:

```powershell
Get-ItemProperty -Path "HKLM:\SOFTWARE\OpenSSH" -Name DefaultShell
```

To revert to the Windows default (cmd.exe), remove the registry value:

```powershell
Remove-ItemProperty -Path "HKLM:\SOFTWARE\OpenSSH" -Name DefaultShell
```

!!! tip
    Replace `C:\path\to\endo.exe` with the actual installation path of the Endo
    executable. If you built from source without installing, this may be inside your
    build directory (e.g. `D:\endo\build\clang-release\src\shell\endo.exe`).
