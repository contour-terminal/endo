---
title: Playground
description: Try Endo in your browser — no installation required.
---

# Playground

Try Endo directly in your browser. This runs the real Endo interpreter compiled to WebAssembly — no installation needed.

<div id="endo-playground-toolbar"></div>
<div id="endo-playground"></div>

!!! tip "Quick start"
    Type an expression and press **Enter** to evaluate. Try `println "Hello, Endo!"` to get started, or click an example above.

!!! info "Limitations"
    The playground runs a sandboxed subset of Endo. Shell commands, file I/O, and network access are not available. Standard library functions for strings, lists, pattern matching, and arithmetic work as expected.

<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@xterm/xterm@5/css/xterm.css">
<script src="https://cdn.jsdelivr.net/npm/@xterm/xterm@5/lib/xterm.js"></script>
<script src="https://cdn.jsdelivr.net/npm/@xterm/addon-fit@0/lib/addon-fit.js"></script>
<script src="../assets/js/playground.js"></script>
