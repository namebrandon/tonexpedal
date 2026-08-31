#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { summarizeDiagnosticCapture } = require('./diagnostic_capture');

const capturePath = process.argv[2];
if (!capturePath) {
    console.error('Usage: npm run diagnostics:summary -- /path/to/tonex-diagnostic.json');
    process.exitCode = 1;
} else {
    try {
        const absolutePath = path.resolve(capturePath);
        const capture = JSON.parse(fs.readFileSync(absolutePath, 'utf8'));
        console.log(JSON.stringify(summarizeDiagnosticCapture(capture), null, 2));
    } catch (error) {
        console.error(`Unable to summarize diagnostic capture: ${error.message}`);
        process.exitCode = 1;
    }
}
