// Renders every status icon (status_icons.mjs) to Content/Data/Icons/statuses/<id>.png,
// 128 pixels with a clear background, and a contact sheet beside this script
// (sheet.png: each icon at the sizes the HUD draws it). Needs Node and Playwright:
//
//   npm install -g playwright && npx playwright install chromium
//   node Tools/StatusIcons/render.mjs

import {mkdirSync, writeFileSync} from 'node:fs';
import {createRequire} from 'node:module';
import {dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {STATUSES, statusSvg} from './status_icons.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '..', '..', 'Content', 'Data', 'Icons', 'statuses');
mkdirSync(out, {recursive: true});

const require = createRequire(import.meta.url);
let playwright;
for (const where of ['playwright', join(process.env.NPM_GLOBAL || '', 'playwright')]) {
  try { playwright = require(where); break; } catch { /* next */ }
}
if (!playwright) {
  const root = (await import('node:child_process')).execSync('npm root -g').toString().trim();
  playwright = require(join(root, 'playwright'));
}

const browser = await playwright.chromium.launch();
const page = await browser.newPage({deviceScaleFactor: 1});
for (const id of Object.keys(STATUSES)) {
  await page.setContent(`<html><body style="margin:0;background:transparent">${statusSvg(id)}</body></html>`);
  const png = await page.locator('svg').screenshot({omitBackground: true});
  writeFileSync(join(out, `${id}.png`), png);
}

// The contact sheet: every icon at 128, 36 and 18 pixels, named, on a board-dark ground.
const cells = Object.entries(STATUSES).map(([id, [name, , harmful]]) => {
  const svg = statusSvg(id);
  const small = svg.replace('width="128" height="128"', 'width="36" height="36"');
  const tiny = svg.replace('width="128" height="128"', 'width="18" height="18"');
  return `<div class="c">${svg}<div class="s">${small}${tiny}</div><div class="n">${name}</div><div class="k">${harmful ? 'harmful' : 'helpful'}</div></div>`;
}).join('');
await page.setViewportSize({width: 1480, height: 400});
await page.setContent(`<html><body style="margin:0;background:#1d2230;font:14px sans-serif;color:#dfe6f2">
<style>.g{display:grid;grid-template-columns:repeat(8,180px);gap:4px;padding:12px}.c{text-align:center;padding:6px}.s{display:flex;gap:10px;justify-content:center;align-items:center;margin:4px}.n{font-weight:600}.k{font-size:11px;opacity:.6}</style>
<div class="g">${cells}</div></body></html>`);
await page.screenshot({path: join(here, 'sheet.png'), fullPage: true});
await browser.close();
console.log(`${Object.keys(STATUSES).length} status icons -> ${out}`);
