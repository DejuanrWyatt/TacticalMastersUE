// Draws every item's icon to Content/Data/Icons/items/<id>.png, in the class
// creator's icon style (lit square, tier colour: grey, green, blue, gold), with
// the glyph its .tmitem.json "icon" names (item_glyphs.mjs).
//
// To run: copy E:\TacticsClassCreator\app\*.mjs beside this folder's files,
// write items.json (id, name, icon, tier of each item), serve the folder on
// port 8766 (python -m http.server 8766) and run `node make_item_icons.mjs`
// with Playwright installed. It writes out/<id>.png and sheet.png to look at.
import {createRequire} from 'node:module'; import {execSync} from 'node:child_process'; import fs from 'node:fs';
const require=createRequire(import.meta.url); const pw=require(execSync('npm root -g').toString().trim()+'/playwright');
const b=await pw.chromium.launch(); const p=await b.newPage({viewport:{width:1130,height:1400}});
p.on('pageerror',e=>console.log('ERR',e.message)); 
await p.goto('http://127.0.0.1:8766/index.html'); await p.waitForFunction(()=>document.title==='done',null,{timeout:60000});
console.log('missing', JSON.stringify(await p.evaluate(()=>window.MISSING)));
const icons = await p.evaluate(()=>window.ICONS); fs.mkdirSync('out',{recursive:true});
for (const [id,b64] of Object.entries(icons)) fs.writeFileSync(`out/${id}.png`, Buffer.from(b64,'base64'));
await (await p.$('#sheet')).screenshot({path:'sheet.png'}); await b.close();
