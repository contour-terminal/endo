// Endo Playground Controller
// Manages xterm.js terminal with endo-signature prompt and Web Worker WASM execution.

'use strict';

(function() {
    var container = document.getElementById('endo-playground');
    if (!container) return;

    // --- Constants ---
    var TIMEOUT_MS = 5000;
    var PROMPT_PATH = '~/playground';

    // --- ANSI helpers ---
    var ESC = '\x1b[';
    var RESET = ESC + '0m';
    var BLUE = ESC + '34m';
    var TEAL = ESC + '36m';
    var GRAY = ESC + '90m';
    var GREEN = ESC + '32m';
    var RED = ESC + '31m';
    var BOLD = ESC + '1m';
    var BOLD_BLUE = ESC + '1;34m';
    var WHITE_ON_TEAL = ESC + '48;2;26;58;64m' + ESC + '97m';

    // --- State ---
    var term = null;
    var fitAddon = null;
    var worker = null;
    var workerReady = false;
    var currentLine = '';
    var cursorPos = 0;
    var history = [];
    var historyIndex = -1;
    var tempLine = '';
    var isExecuting = false;
    var timeoutHandle = null;
    var isMultiLine = false;
    var multiLineBuffer = '';

    // --- Prompt rendering ---
    function writePrompt() {
        // Line 1: ╭─ ~/playground │ 𝑓#
        term.write('\r\n' + BLUE + '\u256D\u2500' + RESET + ' ' + TEAL + PROMPT_PATH + RESET + ' ' + GRAY + '\u2502' + RESET + ' ' + WHITE_ON_TEAL + ' \uD835\uDC53# ' + RESET);
        // Line 2: ╰─ |>
        term.write('\r\n' + BLUE + '\u256E\u2500' + RESET + ' ' + BOLD_BLUE + '|>' + RESET + ' ');
    }

    function writeStatusIndicator(success) {
        if (success) {
            term.write(BLUE + '\u2502' + RESET + ' ' + GREEN + '\u2713' + RESET);
        } else {
            term.write(BLUE + '\u2502' + RESET + ' ' + RED + '\u2717' + RESET);
        }
    }

    function writeContinuationPrompt() {
        term.write('\r\n' + BLUE + '\u00B7' + RESET + '  ' + GRAY + '\u2026' + RESET + ' ');
    }

    // --- Worker management ---
    function createWorker() {
        if (worker) {
            worker.terminate();
        }
        workerReady = false;
        var basePath = getBasePath();
        worker = new Worker(basePath + 'playground-worker.js');
        worker.onmessage = function(e) {
            var msg = e.data;
            if (msg.type === 'ready') {
                workerReady = true;
                updateToolbarState(true);
                writeWelcome(msg.version);
            } else if (msg.type === 'result') {
                handleResult(msg);
            } else if (msg.type === 'reset_done') {
                // Session reset complete
            } else if (msg.type === 'error') {
                writeError(msg.message);
            }
        };
        worker.onerror = function(err) {
            writeError('Worker error: ' + err.message);
            isExecuting = false;
        };
    }

    function getBasePath() {
        // Derive base path from the script tag src
        var scripts = document.getElementsByTagName('script');
        for (var i = 0; i < scripts.length; i++) {
            var src = scripts[i].src || '';
            if (src.indexOf('playground.js') !== -1) {
                return src.replace('playground.js', '');
            }
        }
        return 'assets/js/';
    }

    // --- Output handling ---
    function writeWelcome(version) {
        term.write('\r\n' + GRAY + '  Endo Playground' + (version ? ' v' + version : '') + RESET);
        term.write('\r\n' + GRAY + '  Type Endo expressions below. Shift+Enter for multi-line.' + RESET);
        writePrompt();
    }

    function writeOutput(text) {
        if (!text) return;
        var lines = text.split('\n');
        for (var i = 0; i < lines.length; i++) {
            if (i === lines.length - 1 && lines[i] === '') continue; // skip trailing empty line
            term.write('\r\n  ' + lines[i]);
        }
    }

    function writeError(text) {
        if (!text) return;
        var lines = (Array.isArray(text) ? text.join('\n') : text).split('\n');
        for (var i = 0; i < lines.length; i++) {
            if (lines[i]) {
                term.write('\r\n  ' + RED + lines[i] + RESET);
            }
        }
    }

    function handleResult(msg) {
        clearTimeout(timeoutHandle);
        timeoutHandle = null;
        isExecuting = false;

        if (msg.status === 'ok') {
            if (msg.output) {
                writeOutput(msg.output);
            }
            term.write('\r\n');
            writeStatusIndicator(true);
        } else {
            if (msg.errors && msg.errors.length > 0) {
                writeError(msg.errors);
            }
            term.write('\r\n');
            writeStatusIndicator(false);
        }
        writePrompt();
    }

    // --- Input handling ---
    function refreshLine() {
        // Move to beginning of input area and clear
        term.write('\r' + BLUE + '\u256E\u2500' + RESET + ' ' + BOLD_BLUE + '|>' + RESET + ' ');
        term.write(currentLine);
        // Clear any remaining characters after cursor
        term.write(ESC + 'K');
        // Move cursor to correct position
        var moveBack = currentLine.length - cursorPos;
        if (moveBack > 0) {
            term.write(ESC + moveBack + 'D');
        }
    }

    function submitInput() {
        var source = isMultiLine ? (multiLineBuffer + '\n' + currentLine) : currentLine;
        source = source.trim();

        if (!source) {
            writePrompt();
            return;
        }

        // Add to history
        if (history.length === 0 || history[history.length - 1] !== source) {
            history.push(source);
        }
        historyIndex = -1;
        tempLine = '';

        currentLine = '';
        cursorPos = 0;
        isMultiLine = false;
        multiLineBuffer = '';

        if (!workerReady) {
            term.write('\r\n');
            writeError('WASM module is still loading...');
            writePrompt();
            return;
        }

        isExecuting = true;
        term.write('\r\n' + GRAY + '  \u27F3 evaluating...' + RESET);

        worker.postMessage({ type: 'eval', source: source });

        // Timeout protection
        timeoutHandle = setTimeout(function() {
            isExecuting = false;
            createWorker(); // Kill and recreate worker
            term.write('\r\n');
            writeError('Execution timed out (' + (TIMEOUT_MS / 1000) + 's limit)');
            term.write('\r\n');
            writeStatusIndicator(false);
            writePrompt();
        }, TIMEOUT_MS);
    }

    function handleData(data) {
        if (isExecuting) return;

        // Handle special sequences
        if (data === '\r' || data === '\n') {
            // Enter - submit
            submitInput();
            return;
        }

        if (data === '\x1b[A') {
            // Up arrow - history previous
            if (history.length === 0) return;
            if (historyIndex === -1) {
                tempLine = currentLine;
                historyIndex = history.length - 1;
            } else if (historyIndex > 0) {
                historyIndex--;
            } else {
                return;
            }
            currentLine = history[historyIndex];
            cursorPos = currentLine.length;
            refreshLine();
            return;
        }

        if (data === '\x1b[B') {
            // Down arrow - history next
            if (historyIndex === -1) return;
            if (historyIndex < history.length - 1) {
                historyIndex++;
                currentLine = history[historyIndex];
            } else {
                historyIndex = -1;
                currentLine = tempLine;
            }
            cursorPos = currentLine.length;
            refreshLine();
            return;
        }

        if (data === '\x1b[C') {
            // Right arrow
            if (cursorPos < currentLine.length) {
                cursorPos++;
                term.write(data);
            }
            return;
        }

        if (data === '\x1b[D') {
            // Left arrow
            if (cursorPos > 0) {
                cursorPos--;
                term.write(data);
            }
            return;
        }

        if (data === '\x7f' || data === '\b') {
            // Backspace
            if (cursorPos > 0) {
                currentLine = currentLine.slice(0, cursorPos - 1) + currentLine.slice(cursorPos);
                cursorPos--;
                refreshLine();
            }
            return;
        }

        if (data === '\x1b[3~') {
            // Delete key
            if (cursorPos < currentLine.length) {
                currentLine = currentLine.slice(0, cursorPos) + currentLine.slice(cursorPos + 1);
                refreshLine();
            }
            return;
        }

        if (data === '\x01') {
            // Ctrl+A - beginning of line
            cursorPos = 0;
            refreshLine();
            return;
        }

        if (data === '\x05') {
            // Ctrl+E - end of line
            cursorPos = currentLine.length;
            refreshLine();
            return;
        }

        if (data === '\x0c') {
            // Ctrl+L - clear screen
            term.clear();
            writePrompt();
            term.write(currentLine);
            return;
        }

        if (data === '\x15') {
            // Ctrl+U - clear line
            currentLine = '';
            cursorPos = 0;
            refreshLine();
            return;
        }

        // Regular printable characters
        if (data >= ' ' || data === '\t') {
            currentLine = currentLine.slice(0, cursorPos) + data + currentLine.slice(cursorPos);
            cursorPos += data.length;
            refreshLine();
        }
    }

    // --- Example snippets ---
    var examples = [
        {
            name: 'Hello World',
            code: 'println "Hello, Endo!"'
        },
        {
            name: 'Functions',
            code: 'let add x y = x + y\nprintln (string (add 3 4))'
        },
        {
            name: 'Pattern Match',
            code: 'let fizzbuzz n =\n  match (n % 3, n % 5) with\n  | (0, 0) -> "FizzBuzz"\n  | (0, _) -> "Fizz"\n  | (_, 0) -> "Buzz"\n  | _ -> string n\n\nfor i in [1;2;3;4;5;6;7;8;9;10;11;12;13;14;15] do\n  println (fizzbuzz i)\ndone'
        },
        {
            name: 'Recursion',
            code: 'let rec factorial n =\n  if n <= 1 then 1\n  else n * factorial (n - 1)\n\nprintln (string (factorial 10))'
        },
        {
            name: 'Pipelines',
            code: 'let xs = [1; 2; 3; 4; 5]\nxs |> List.map (fun x -> x * x) |> List.filter (fun x -> x > 5) |> println'
        }
    ];

    function createToolbar() {
        var toolbar = document.getElementById('endo-playground-toolbar');
        if (!toolbar) {
            toolbar = document.createElement('div');
            toolbar.id = 'endo-playground-toolbar';
            container.parentNode.insertBefore(toolbar, container);
        }
        toolbar.innerHTML = '';
        toolbar.className = 'playground-toolbar';

        var label = document.createElement('span');
        label.className = 'playground-toolbar-label';
        label.textContent = 'Examples:';
        toolbar.appendChild(label);

        examples.forEach(function(example) {
            var btn = document.createElement('button');
            btn.className = 'playground-example-btn';
            btn.textContent = example.name;
            btn.disabled = !workerReady;
            btn.onclick = function() {
                if (isExecuting || !workerReady) return;
                // Reset session
                worker.postMessage({ type: 'reset' });
                // Clear terminal and run example
                term.clear();
                term.write(GRAY + '  Endo Playground' + RESET);
                writePrompt();
                // Display and execute the example
                currentLine = '';
                cursorPos = 0;
                term.write(example.code.split('\n')[0]);
                // Send full source for evaluation
                isExecuting = true;
                term.write('\r\n' + GRAY + '  \u27F3 evaluating...' + RESET);
                worker.postMessage({ type: 'eval', source: example.code });
                timeoutHandle = setTimeout(function() {
                    isExecuting = false;
                    createWorker();
                    term.write('\r\n');
                    writeError('Execution timed out (' + (TIMEOUT_MS / 1000) + 's limit)');
                    term.write('\r\n');
                    writeStatusIndicator(false);
                    writePrompt();
                }, TIMEOUT_MS);
            };
            toolbar.appendChild(btn);
        });
    }

    function updateToolbarState(enabled) {
        var buttons = document.querySelectorAll('.playground-example-btn');
        buttons.forEach(function(btn) {
            btn.disabled = !enabled;
        });
    }

    // --- Initialization ---
    function init() {
        // Create terminal
        term = new Terminal({
            theme: {
                background: '#1a1b26',
                foreground: '#c0caf5',
                cursor: '#c0caf5',
                cursorAccent: '#1a1b26',
                selectionBackground: '#33467c',
                black: '#15161e',
                red: '#f7768e',
                green: '#9ece6a',
                yellow: '#e0af68',
                blue: '#7aa2f7',
                magenta: '#bb9af7',
                cyan: '#7dcfff',
                white: '#a9b1d6',
                brightBlack: '#414868',
                brightRed: '#f7768e',
                brightGreen: '#9ece6a',
                brightYellow: '#e0af68',
                brightBlue: '#7aa2f7',
                brightMagenta: '#bb9af7',
                brightCyan: '#7dcfff',
                brightWhite: '#c0caf5'
            },
            fontFamily: '"JetBrains Mono", monospace',
            fontSize: 14,
            lineHeight: 1.4,
            cursorBlink: true,
            cursorStyle: 'bar',
            scrollback: 1000,
            convertEol: true,
            allowProposedApi: true
        });

        fitAddon = new FitAddon.FitAddon();
        term.loadAddon(fitAddon);
        term.open(container);
        fitAddon.fit();

        // Handle resize
        window.addEventListener('resize', function() {
            if (fitAddon) fitAddon.fit();
        });

        // Input handler
        term.onData(handleData);

        // Show loading message
        term.write(GRAY + '  Loading Endo WASM module...' + RESET);

        // Create toolbar
        createToolbar();

        // Start worker
        createWorker();
    }

    // Wait for DOM to be ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
