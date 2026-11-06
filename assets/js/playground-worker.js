// Endo Playground Web Worker
// Loads the Endo WASM module and handles eval/reset requests.

'use strict';

let module = null;
let endoEval = null;
let endoReset = null;
let endoVersion = null;

// Load the WASM module
importScripts('endo-playground.js');

EndoModule().then(function(mod) {
    module = mod;
    endoEval = mod.cwrap('endo_eval', 'string', ['string']);
    endoReset = mod.cwrap('endo_reset', null, []);
    endoVersion = mod.cwrap('endo_version', 'string', []);
    postMessage({ type: 'ready', version: endoVersion() });
}).catch(function(err) {
    postMessage({ type: 'error', message: 'Failed to load WASM module: ' + err.message });
});

self.onmessage = function(e) {
    var msg = e.data;

    if (msg.type === 'eval') {
        if (!endoEval) {
            postMessage({ type: 'result', status: 'error', errors: ['WASM module not loaded yet'] });
            return;
        }
        try {
            var jsonResult = endoEval(msg.source);
            var result = JSON.parse(jsonResult);
            postMessage({ type: 'result', status: result.status, output: result.output || '', errors: result.errors || [] });
        } catch (err) {
            postMessage({ type: 'result', status: 'error', errors: ['Internal error: ' + err.message] });
        }
    } else if (msg.type === 'reset') {
        if (endoReset) {
            endoReset();
        }
        postMessage({ type: 'reset_done' });
    }
};
