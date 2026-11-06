#!/usr/bin/env node
// NewShell16 - Copyright (C) 2026 NewShell16 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke test of a page made by mkpcjs.py, in headless Chromium (Playwright):
// serves the directory, boots the machine, saves a screenshot of the
// desktop, presses Ctrl+Esc (NewShell16's Task Manager) and saves another.
//
//   node tools/pcjs/smoke.js [DIR] [OUTDIR] [SECONDS]
//
// DIR defaults to build/pcjs, OUTDIR to DIR/../pcjs-shots, SECONDS (the
// wait for Windows) to 45. Exits non-zero on a page error.
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const root = path.resolve(__dirname, '..', '..');
const dir = path.resolve(process.argv[2] || path.join(root, 'build', 'pcjs'));
const out = path.resolve(process.argv[3] || path.join(dir, '..', 'pcjs-shots'));
const wait = Number(process.argv[4] || 45) * 1000;
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json5': 'application/json' };

const server = http.createServer((req, res) => {
    const f = path.join(dir, decodeURIComponent(req.url.split('?')[0]).replace(/\/$/, '/index.html'));
    if (!f.startsWith(dir) || !fs.existsSync(f) || !fs.statSync(f).isFile()) {
        res.writeHead(404); res.end(); return;
    }
    res.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' });
    fs.createReadStream(f).pipe(res);
});

server.listen(0, '127.0.0.1', async () => {
    const url = `http://127.0.0.1:${server.address().port}/`;
    fs.mkdirSync(out, { recursive: true });
    let failed = false;
    const browser = await chromium.launch();
    try {
        const page = await browser.newPage({ viewport: { width: 1360, height: 1200 } });
        page.on('pageerror', e => { console.log('page error: ' + e.message); failed = true; });
        page.on('console', m => { if (/error|fail/i.test(m.text())) console.log('console: ' + m.text()); });
        await page.goto(url);
        await page.waitForTimeout(wait);
        const screen = page.locator('canvas.pcjs-canvas');
        await screen.screenshot({ path: path.join(out, 'desktop.png') });
        // a click gives the screen (PCjs's overlay) the keyboard
        const box = await screen.boundingBox();
        await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
        await page.keyboard.down('Control');
        await page.waitForTimeout(100);
        await page.keyboard.press('Escape');
        await page.waitForTimeout(100);
        await page.keyboard.up('Control');
        await page.waitForTimeout(5000);
        await screen.screenshot({ path: path.join(out, 'ctrl-esc.png') });
        console.log('screenshots in ' + out);
    } finally {
        await browser.close();
        server.close();
    }
    process.exit(failed ? 1 : 0);
});
