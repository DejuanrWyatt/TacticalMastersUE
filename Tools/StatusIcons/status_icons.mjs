// The status icons: one small design per status, drawn as SVG and rendered to
// the 128-pixel PNGs the HUD reads from Content/Data/Icons/statuses/<id>.png.
//
// A status icon is a rounded square (an ability's is a circle, so the two never
// read as each other): its colour is the status's own, the rim says which way
// it cuts -- red for harmful, gold for helpful -- and one bold glyph says what
// it does, drawn to still read at the 18-pixel size over a unit's head.
//
//   node Tools/StatusIcons/render.mjs      writes the PNGs and a contact sheet
//
// Each glyph draws in a 64-unit square, in the light colour c; d is the dark
// of the backing, for holes and details.

const HARMFUL_RIM = '#ff6a55';
const HELPFUL_RIM = '#f3cf68';

/** id: [name, colour, harmful, glyph]. The colours are the HUD's (TMBattleHudStyle.h). */
export const STATUSES = {
  burn: ['Burn', '#ff8033', true, (c, d) => `<path d="M32 8 C40 19 49 25 47 39 C46 51 38 57 32 57 C24 57 16 51 17 40 C18 31 24 27 26 17 C30 23 30 29 34 32 C37 24 35 16 32 8 Z" fill="${c}"/><path d="M32 34 C36 40 40 43 39 49 C38 53 35 55 32 55 C28 55 25 52 26 48 C26 44 30 42 32 34 Z" fill="${d}"/>`],
  bleed: ['Bleed', '#cc1a33', true, c => `<path d="M24 10 C29 20 36 27 36 35 C36 42 31 46 24 46 C17 46 12 42 12 35 C12 27 19 20 24 10 Z" fill="${c}"/><path d="M44 28 C47 34 52 38 52 44 C52 49 48 52 44 52 C40 52 36 49 36 44 C36 38 41 34 44 28 Z" fill="${c}"/>`],
  regen: ['Regen', '#73ff8c', false, (c, d) => `<path d="M32 55 C18 45 9 37 9 26 C9 18 15 12 23 12 C28 12 31 15 32 18 C33 15 36 12 41 12 C49 12 55 18 55 26 C55 37 46 45 32 55 Z" fill="${c}"/><rect x="29" y="21" width="6" height="20" rx="2" fill="${d}"/><rect x="22" y="28" width="20" height="6" rx="2" fill="${d}"/>`],
  slow: ['Slow', '#80bfff', true, (c, d) => `<path d="M10 48 H50 C54 48 56 45 54 42 L50 36" fill="none" stroke="${c}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/><circle cx="32" cy="32" r="15" fill="${c}"/><path d="M32 32 m-9 0 a9 9 0 1 1 9 9 a5 5 0 1 1 -5 -5" fill="none" stroke="${d}" stroke-width="3" stroke-linecap="round"/><path d="M50 36 L53 26 M50 36 L58 30" stroke="${c}" stroke-width="3" stroke-linecap="round"/>`],
  stun: ['Stun', '#ffe64d', true, c => `<path d="M12 40 C12 28 52 28 52 40" fill="none" stroke="${c}" stroke-width="3.5" stroke-linecap="round" stroke-dasharray="3 5"/>${[[16, 26], [32, 17], [48, 26]].map(([x, y]) => `<path d="${starAt(x, y, 5, 3.5, 8.5)}" fill="${c}"/>`).join('')}<circle cx="32" cy="47" r="7" fill="${c}"/>`],
  shield: ['Shield', '#99d9ff', false, (c, d) => `<path d="M32 8 L51 15 L51 31 C51 44 42 52 32 57 C22 52 13 44 13 31 L13 15 Z" fill="${c}"/><path d="M32 15 L45 20 L45 31 C45 40 39 46 32 50 Z" fill="${d}" opacity="0.35"/>`],
  barrier: ['Barrier', '#bfe6ff', false, (c, d) => `<path d="M32 7 L54 19.5 L54 44.5 L32 57 L10 44.5 L10 19.5 Z" fill="none" stroke="${c}" stroke-width="5" stroke-linejoin="round"/><path d="M32 18 L44 25 L44 39 L32 46 L20 39 L20 25 Z" fill="${c}"/><circle cx="28" cy="28" r="3" fill="${d}" opacity="0.5"/>`],
  root: ['Root', '#b38c4d', true, c => `<rect x="28" y="8" width="8" height="20" rx="3" fill="${c}"/><path d="M32 26 C32 36 22 38 14 50 M32 26 C32 38 32 44 32 56 M32 26 C32 36 42 38 50 50 M24 38 C20 36 14 38 10 42 M40 38 C44 36 50 38 54 42" fill="none" stroke="${c}" stroke-width="4.5" stroke-linecap="round"/>`],
  crippled: ['Crippled', '#998059', true, (c, d) => `<path d="M22 8 H34 V36 L50 40 C54 41 55 44 55 47 V52 H16 C15 44 18 40 22 36 Z" fill="${c}"/><path d="M26 16 L32 24 L26 30 L32 38 L28 46" fill="none" stroke="${d}" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>`],
  stride: ['Stride', '#80f2cc', false, c => `<path d="M26 10 H38 V36 L52 40 C56 41 57 44 57 47 V52 H20 C19 44 22 40 26 36 Z" fill="${c}"/><path d="M6 22 H18 M4 32 H18 M8 42 H18" stroke="${c}" stroke-width="4" stroke-linecap="round"/>`],
  silence: ['Silence', '#cc80e6', true, (c, d) => `<path d="M10 16 C10 12 13 10 17 10 H47 C51 10 54 12 54 16 V36 C54 40 51 42 47 42 H28 L17 52 V42 C13 42 10 40 10 36 Z" fill="${c}"/><path d="M22 20 L42 32 M42 20 L22 32" stroke="${d}" stroke-width="5" stroke-linecap="round"/>`],
  blind: ['Blind', '#736680', true, (c, d) => `<path d="M6 32 C14 18 24 14 32 14 C40 14 50 18 58 32 C50 46 40 50 32 50 C24 50 14 46 6 32 Z" fill="${c}"/><circle cx="32" cy="32" r="9" fill="${d}"/><path d="M12 54 L52 10" stroke="${d}" stroke-width="9" stroke-linecap="round"/><path d="M12 54 L52 10" stroke="${c}" stroke-width="4" stroke-linecap="round"/>`],
  shred: ['Shred', '#e6734d', true, (c, d) => `<path d="M32 8 L51 15 L51 31 C51 44 42 52 32 57 C22 52 13 44 13 31 L13 15 Z" fill="${c}"/><path d="M30 8 L36 20 L27 29 L37 38 L29 57" fill="none" stroke="${d}" stroke-width="4.5" stroke-linejoin="round"/>`],
  sleep: ['Sleep', '#8ca6ff', true, c => `<path d="M12 14 H30 L12 34 H30" fill="none" stroke="${c}" stroke-width="5.5" stroke-linecap="round" stroke-linejoin="round"/><path d="M36 32 H52 L36 50 H52" fill="none" stroke="${c}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>`],
  freeze: ['Freeze', '#99e6ff', true, (c, d) => `<rect x="8" y="8" width="48" height="48" rx="6" fill="${c}" opacity="0.45"/>${[0, 60, 120].map(a => `<g transform="rotate(${a} 32 32)"><path d="M32 12 V52 M32 18 L26 13 M32 18 L38 13 M32 46 L26 51 M32 46 L38 51" stroke="${c}" stroke-width="4" stroke-linecap="round"/></g>`).join('')}`],
  knockdown: ['Knockdown', '#ccb373', true, c => `<path d="M32 6 V30" stroke="${c}" stroke-width="6" stroke-linecap="round"/><path d="M20 24 L32 38 L44 24" fill="none" stroke="${c}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/><rect x="8" y="46" width="48" height="7" rx="3" fill="${c}"/>`],
  doom: ['Doom', '#b33373', true, (c, d) => `<path d="M32 9 C20 9 13 17 13 28 C13 35 17 39 21 41 L21 50 H43 V41 C47 39 51 35 51 28 C51 17 44 9 32 9 Z" fill="${c}"/><circle cx="24.5" cy="29" r="5" fill="${d}"/><circle cx="39.5" cy="29" r="5" fill="${d}"/><path d="M28 50 V56 M32 50 V56 M36 50 V56" stroke="${c}" stroke-width="3"/>`],
  taunt: ['Taunt', '#ff9959', true, (c, d) => `<path d="${star(10, 17, 27)}" fill="${c}"/><rect x="29" y="17" width="6" height="18" rx="3" fill="${d}"/><circle cx="32" cy="42" r="3.5" fill="${d}"/>`],
  fly: ['Fly', '#bfd9ff', false, c => `<path d="M31 36 C24 22 14 16 4 16 C8 22 9 26 8 30 C12 30 16 31 18 34 C14 35 12 37 11 40 C18 40 25 40 31 36 Z" fill="${c}"/><path d="M33 36 C40 22 50 16 60 16 C56 22 55 26 56 30 C52 30 48 31 46 34 C50 35 52 37 53 40 C46 40 39 40 33 36 Z" fill="${c}"/><circle cx="32" cy="42" r="5" fill="${c}"/>`],
  immunity: ['Immunity', '#fff299', false, (c, d) => `<circle cx="32" cy="32" r="22" fill="none" stroke="${c}" stroke-width="4"/><path d="${star(4, 6, 16)}" fill="${c}"/>`],
  invuln: ['Invulnerable', '#ffffd9', false, (c, d) => `<path d="M32 8 L51 15 L51 31 C51 44 42 52 32 57 C22 52 13 44 13 31 L13 15 Z" fill="${c}"/><path d="${starAt(32, 31, 5, 6, 13)}" fill="${d}"/>`],
  relentless: ['Relentless', '#ffbf4d', false, c => `<path d="M10 14 L28 32 L10 50" fill="none" stroke="${c}" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/><path d="M30 14 L48 32 L30 50" fill="none" stroke="${c}" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/><rect x="50" y="12" width="6" height="40" rx="3" fill="${c}"/>`],
  // The camps' and items' own.
  surge: ['Surge', '#ff7066', false, (c, d) => `<path d="${star(8, 12, 26)}" fill="${c}"/><path d="M32 18 L42 30 L36 30 L36 44 L28 44 L28 30 L22 30 Z" fill="${d}"/>`],
  veil: ['Vanished', '#9aa6c9', false, (c, d) => `<path d="M32 8 C44 8 50 18 50 30 V54 L44 49 L38 54 L32 49 L26 54 L20 49 L14 54 V30 C14 18 20 8 32 8 Z" fill="${c}" opacity="0.75"/><circle cx="25" cy="28" r="4" fill="${d}"/><circle cx="39" cy="28" r="4" fill="${d}"/>`],
  lured: ['Lured', '#e6b359', true, c => `<path d="M16 54 C8 46 24 40 16 30 C10 22 22 16 16 8 M32 54 C24 46 40 40 32 30 C26 22 38 16 32 8 M48 54 C40 46 56 40 48 30 C42 22 54 16 48 8" fill="none" stroke="${c}" stroke-width="4.5" stroke-linecap="round"/>`],
  staggered: ['Staggered', '#e69966', true, c => `<path d="M32 32 m0 -2 a2 2 0 1 1 -2 2 a5 5 0 1 1 5 5 a9 9 0 1 1 -9 -9 a13 13 0 1 1 13 13 a17 17 0 1 1 -17 -17" fill="none" stroke="${c}" stroke-width="4" stroke-linecap="round"/>`],
  // The second set (Docs/design/feat-status-effects.md).
  marked: ['Marked', '#ff5a5a', true, c => `<circle cx="32" cy="32" r="18" fill="none" stroke="${c}" stroke-width="4.5"/><circle cx="32" cy="32" r="5.5" fill="${c}"/><path d="M32 4 V18 M32 46 V60 M4 32 H18 M46 32 H60" stroke="${c}" stroke-width="4.5" stroke-linecap="round"/>`],
  offbalance: ['Off-Balance', '#ffa64d', true, (c, d) => `<path d="M32 40 L44 56 H20 Z" fill="${c}"/><g transform="rotate(-20 32 40)"><rect x="6" y="36" width="52" height="6" rx="3" fill="${c}"/><circle cx="12" cy="29" r="7" fill="${c}"/></g><path d="M48 12 C52 16 52 22 48 26" fill="none" stroke="${c}" stroke-width="3.5" stroke-linecap="round"/>`],
  wet: ['Wet', '#4da6ff', true, (c, d) => `<path d="M32 6 C40 20 48 29 48 39 C48 48 41 53 32 53 C23 53 16 48 16 39 C16 29 24 20 32 6 Z" fill="${c}"/><path d="M24 38 C24 43 27 46 31 47" fill="none" stroke="${d}" stroke-width="3" stroke-linecap="round" opacity="0.6"/><path d="M8 58 C14 54 20 54 26 58 M38 58 C44 54 50 54 56 58" fill="none" stroke="${c}" stroke-width="3" stroke-linecap="round"/>`],
  oiled: ['Oiled', '#b3894d', true, (c, d) => `<path d="M32 7 C40 20 47 28 47 38 C47 46 41 51 32 51 C23 51 17 46 17 38 C17 28 24 20 32 7 Z" fill="#1c140b" stroke="${c}" stroke-width="3.5" stroke-linejoin="round"/><path d="M37 23 C41 29 42 33 42 38" fill="none" stroke="${c}" stroke-width="3.5" stroke-linecap="round"/><ellipse cx="32" cy="57" rx="20" ry="3.5" fill="${c}"/>`],
  chilled: ['Chilled', '#a6e0ff', true, (c, d) => `<rect x="24" y="6" width="12" height="36" rx="6" fill="${c}"/><circle cx="30" cy="48" r="10" fill="${c}"/><rect x="27" y="26" width="6" height="18" rx="3" fill="${d}"/><circle cx="30" cy="48" r="5" fill="${d}"/><path d="M46 12 V28 M39 16 L53 24 M53 16 L39 24" stroke="${c}" stroke-width="3" stroke-linecap="round"/>`],
  haste: ['Haste', '#ffdf59', false, (c, d) => `<circle cx="36" cy="34" r="20" fill="${c}"/><path d="M36 22 V34 L44 40" fill="none" stroke="${d}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M4 22 H12 M2 32 H10 M4 42 H12" stroke="${c}" stroke-width="4" stroke-linecap="round"/>`],
  stop: ['Stop', '#b3a6ff', true, (c, d) => `<circle cx="32" cy="32" r="23" fill="${c}"/><rect x="22" y="21" width="7" height="22" rx="2" fill="${d}"/><rect x="35" y="21" width="7" height="22" rx="2" fill="${d}"/>`],
  suppressed: ['Suppressed', '#e65c40', true, c => `<path d="M32 4 V36" stroke="${c}" stroke-width="5" stroke-linecap="round"/><path d="M22 28 L32 42 L42 28 Z" fill="${c}"/><path d="M26 4 L32 10 L38 4" fill="none" stroke="${c}" stroke-width="3.5" stroke-linecap="round"/><rect x="8" y="48" width="48" height="6" rx="3" fill="${c}"/><path d="M14 44 L20 48 M50 44 L44 48" stroke="${c}" stroke-width="3" stroke-linecap="round"/>`],
  protect: ['Protect', '#e6c373', false, (c, d) => `<path d="M32 8 L51 15 L51 31 C51 44 42 52 32 57 C22 52 13 44 13 31 L13 15 Z" fill="${c}"/><g transform="rotate(45 32 32)"><path d="M32 15 L35 20 L35 38 L29 38 L29 20 Z" fill="${d}"/><rect x="24" y="38" width="16" height="4" rx="2" fill="${d}"/><rect x="30" y="42" width="4" height="7" fill="${d}"/></g>`],
  shell: ['Shell', '#b38cff', false, (c, d) => `<path d="M32 8 L51 15 L51 31 C51 44 42 52 32 57 C22 52 13 44 13 31 L13 15 Z" fill="${c}"/><path d="${starAt(32, 31, 4, 4, 14)}" fill="${d}"/><circle cx="43" cy="20" r="2.5" fill="${d}"/><circle cx="21" cy="42" r="2" fill="${d}"/>`],
  guarded: ['Guarded', '#8cc7e6', false, (c, d) => `<circle cx="44" cy="16" r="7" fill="${c}"/><path d="M34 52 V34 C34 28 38 25 44 25 C50 25 54 28 54 34 V52 Z" fill="${c}"/><path d="M22 12 L36 17 L36 30 C36 41 30 47 22 51 C14 47 8 41 8 30 L8 17 Z" fill="${c}" stroke="${d}" stroke-width="3" stroke-linejoin="round"/>`],
  reraise: ['Reraise', '#ffd98c', false, (c, d) => `<ellipse cx="32" cy="17" rx="9" ry="11" fill="none" stroke="${c}" stroke-width="5.5"/><rect x="28.5" y="27" width="7" height="29" rx="2" fill="${c}"/><rect x="15" y="30" width="34" height="7" rx="2" fill="${c}"/>`],
  reflect: ['Reflect', '#d9f0ff', false, (c, d) => `<path d="M40 6 L50 16 L50 48 L40 58 Z" fill="${c}"/><path d="M8 20 L34 32" stroke="${c}" stroke-width="4" stroke-linecap="round" stroke-dasharray="1 7"/><path d="M34 32 L12 48" stroke="${c}" stroke-width="4.5" stroke-linecap="round"/><path d="M8 51 L10 41 L18 47 Z" fill="${c}"/>`],
  charmed: ['Charmed', '#ff80c2', true, (c, d) => `<path d="M32 55 C18 45 9 37 9 26 C9 18 15 12 23 12 C28 12 31 15 32 18 C33 15 36 12 41 12 C49 12 55 18 55 26 C55 37 46 45 32 55 Z" fill="${c}"/><path d="M26 28 m-5 0 a5 5 0 1 1 5 5" fill="none" stroke="${d}" stroke-width="3" stroke-linecap="round"/><path d="M44 28 m-5 0 a5 5 0 1 1 5 5" fill="none" stroke="${d}" stroke-width="3" stroke-linecap="round"/><path d="M26 40 C30 43 34 43 38 40" fill="none" stroke="${d}" stroke-width="3" stroke-linecap="round"/>`],
  terrified: ['Terrified', '#a680d9', true, (c, d) => `<circle cx="32" cy="32" r="24" fill="${c}"/><circle cx="23" cy="26" r="6" fill="${d}"/><circle cx="41" cy="26" r="6" fill="${d}"/><circle cx="23" cy="26" r="2" fill="${c}"/><circle cx="41" cy="26" r="2" fill="${c}"/><path d="M20 44 L24 40 L28 44 L32 40 L36 44 L40 40 L44 44" fill="none" stroke="${d}" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/><path d="M14 12 L10 6 M50 12 L54 6" stroke="${d}" stroke-width="3" stroke-linecap="round"/>`],
  decay: ['Decay', '#8fad47', true, (c, d) => `<rect x="25" y="8" width="14" height="42" rx="3" fill="${c}"/><rect x="11" y="22" width="42" height="14" rx="3" fill="${c}"/><path d="M28 10 L34 20 L28 28 L36 36 L30 48" fill="none" stroke="${d}" stroke-width="3.5" stroke-linejoin="round"/><path d="M18 36 C18 41 16 44 16 47 C16 50 20 50 20 47 C20 44 18 41 18 36 Z M46 36 C46 43 44 47 44 50 C44 54 48 54 48 50 C48 47 46 43 46 36 Z" fill="${c}"/>`],
  // 2026-10-02: healing received halved -- half the cross only outlined, an arrow down.
  wounded: ['Wounded', '#bf525c', true, c => `<path d="M25 8 H39 V22 H53 V36 H39 V50 H25 V36 H11 V22 H25 Z" fill="none" stroke="${c}" stroke-width="3.5" stroke-linejoin="round"/><path d="M25 8 H32 V50 H25 V36 H11 V22 H25 Z" fill="${c}"/><path d="M50 36 V56 M42 48 L50 56 L58 48" fill="none" stroke="${c}" stroke-width="4.5" stroke-linecap="round" stroke-linejoin="round"/>`],
};

/** A star with its centre anywhere. */
function starAt(cx, cy, n, r0, r1) {
  return 'M' + Array.from({length: n * 2}, (_, i) => {
    const a = (i / (n * 2)) * Math.PI * 2 - Math.PI / 2;
    const r = i % 2 ? r0 : r1;
    return `${(cx + Math.cos(a) * r).toFixed(1)} ${(cy + Math.sin(a) * r).toFixed(1)}`;
  }).join(' L') + ' Z';
}
function star(n, r0, r1) {
  return starAt(32, 32, n, r0, r1);
}

function hex(c) {
  return [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16));
}
function mix(a, b, t) {
  const x = hex(a), y = hex(b);
  return '#' + x.map((v, i) => Math.round(v + (y[i] - v) * t).toString(16).padStart(2, '0')).join('');
}

/** The whole icon, a 128-pixel SVG. */
export function statusSvg(id) {
  const [, colour, harmful, glyph] = STATUSES[id];
  const top = mix(colour, '#000000', 0.45);
  const bottom = mix(colour, '#000000', 0.78);
  const light = mix(colour, '#ffffff', 0.55);
  const dark = mix(colour, '#000000', 0.8);
  const rim = harmful ? HARMFUL_RIM : HELPFUL_RIM;
  return `<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 64 64">
<defs>
  <linearGradient id="b_${id}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${top}"/><stop offset="1" stop-color="${bottom}"/></linearGradient>
  <filter id="s_${id}" x="-20%" y="-20%" width="140%" height="140%"><feDropShadow dx="0" dy="1.2" stdDeviation="0.9" flood-color="#000" flood-opacity="0.75"/></filter>
</defs>
<rect x="2.5" y="2.5" width="59" height="59" rx="12" fill="url(#b_${id})" stroke="${rim}" stroke-width="3"/>
<rect x="6" y="5.5" width="52" height="16" rx="8" fill="#ffffff" opacity="0.10"/>
<g transform="translate(32 32) scale(0.8) translate(-32 -32)" filter="url(#s_${id})">${glyph(light, dark)}</g>
</svg>`;
}
