#!/usr/bin/env python3
"""The interactive shell exits when its terminal hangs up, rather than spinning.

A terminal that goes away (the emulator closed, the ssh connection dropped) ends the shell's input:
every read of the pty's slave side then reports end of file or hang-up. SIGHUP normally kills the
shell first, but a process that ignores it (`nohup`, a parent that set SIG_IGN) is left reading a
terminal that will never produce another byte. core-cpp's TUI runtime reports that as the end of
input; the prompt must treat it as final, like Ctrl+D. If it read again instead, the shell would
loop at full CPU for ever -- which is what this checks.

The shell runs on a pty with SIGHUP ignored. Once it has drawn its prompt, the master side is
closed, and the shell must exit within a bound. If it does not, its CPU time over a second of wall
clock is reported, the process is killed, and the check fails.

POSIX only: it needs a pty. Exit 0 = pass, 1 = fail, 77 = skipped (no pty support here).
"""

from __future__ import annotations

import argparse
import os
import select
import signal
import sys
import tempfile
import time

SKIPPED = 77
PROMPT_WAIT_SECONDS = 20.0
EXIT_WAIT_SECONDS = 15.0


def cpu_seconds(pid: int) -> float | None:
    """User plus system CPU time of @p pid, from /proc (Linux); None where that is unavailable."""
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii") as stat:
            fields = stat.read().rsplit(")", 1)[1].split()
    except OSError:
        return None
    ticks = os.sysconf("SC_CLK_TCK")
    return (int(fields[11]) + int(fields[12])) / ticks


def wait_for_exit(pid: int, seconds: float) -> int | None:
    """The raw wait status of @p pid if it exits within @p seconds, else None."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done == pid:
            return status
        time.sleep(0.05)
    return None


def print_log(home: str) -> None:
    """Prints the tail of what the shell wrote to standard error."""
    try:
        with open(os.path.join(home, "stderr.log"), encoding="utf-8", errors="replace") as log:
            lines = log.read().splitlines()
    except OSError:
        return
    if lines:
        print("--- the shell's standard error, last 40 lines ---")
        print("\n".join(lines[-40:]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--endo-path", required=True, help="the endo executable to run")
    args = parser.parse_args()

    try:
        import pty
    except ImportError:
        print("SKIP: no pty module on this platform")
        return SKIPPED

    with tempfile.TemporaryDirectory(prefix="endo-hangup-") as home:
        pid, master = pty.fork()
        if pid == 0:
            # The child: the shell, with SIGHUP ignored (inherited across exec), and a HOME of its
            # own so no user profile, history or configuration takes part.
            signal.signal(signal.SIGHUP, signal.SIG_IGN)
            # Its standard error goes to a file, so what it says on the way out survives the
            # terminal it is losing.
            log = os.open(os.path.join(home, "stderr.log"), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            os.dup2(log, 2)
            env = {"HOME": home, "TERM": "xterm-256color", "PATH": os.environ.get("PATH", "/usr/bin:/bin")}
            os.execve(args.endo_path, [args.endo_path, "--no-profile"], env)

        # Let the shell start and draw its prompt: wait for its output to go quiet.
        seen = b""
        deadline = time.monotonic() + PROMPT_WAIT_SECONDS
        quiet_since = time.monotonic()
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.2)
            if ready:
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                seen += chunk
                quiet_since = time.monotonic()
            elif seen and time.monotonic() - quiet_since > 1.0:
                break
        if not seen:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            print("FAIL: the shell wrote nothing to its terminal; it never reached the prompt")
            return 1

        # The terminal goes away.
        os.close(master)
        status = wait_for_exit(pid, EXIT_WAIT_SECONDS)
        if status is not None:
            if os.WIFEXITED(status):
                print(
                    f"PASS: the shell exited (code {os.WEXITSTATUS(status)}) after its terminal hung up, "
                    "with SIGHUP ignored"
                )
                return 0
            # Ending by a signal is a crash on the way out (SIGHUP itself is ignored), not an exit.
            print(f"FAIL: the shell was killed by signal {os.WTERMSIG(status)} after its terminal hung up")
            print_log(home)
            return 1

        before = cpu_seconds(pid)
        time.sleep(1.0)
        after = cpu_seconds(pid)
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        spin = "" if before is None or after is None else f"; it used {after - before:.2f}s of CPU in the last second"
        print(f"FAIL: the shell was still running {EXIT_WAIT_SECONDS:.0f}s after its terminal hung up{spin}")
        print_log(home)
        return 1


if __name__ == "__main__":
    sys.exit(main())
