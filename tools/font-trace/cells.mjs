// Slice traced groups into 6-column cells and print each glyph (5x8).
import { readFileSync, writeFileSync } from 'node:fs';

const txt = readFileSync(new URL('./traced.txt', import.meta.url), 'utf8');
const groups = {};
let cur = null;
for (const line of txt.split(/\r?\n/)) {
  const m = line.match(/^=== 0x([0-9a-f]+)/i);
  if (m) { cur = parseInt(m[1], 16); groups[cur] = []; continue; }
  if (cur !== null && /^[#.]+$/.test(line)) groups[cur].push(line);
}
const MISSING = new Set([...'%&+;@[\\]^_`{|}~'].map((c) => c.charCodeAt(0)).concat([0x20, 0x7f]));
const out = {};
for (const [g, rows] of Object.entries(groups)) {
  const base = +g;
  const W = rows[0].length;
  const litCol = (x) => x >= 0 && x < W && rows.some((r) => r[x] === '#');
  // phase o: gap columns are x = o + 5 (mod 6) -> should be empty
  let best = null;
  for (let o = 0; o < 6; o++) {
    let gapLit = 0;
    for (let x = 0; x < W; x++) if (((x - o) % 6 + 6) % 6 === 5 && litCol(x)) gapLit++;
    if (!best || gapLit < best.gapLit) best = { o, gapLit };
  }
  const codes = [...Array(base === 0x80 ? 1 : 8).keys()].map((i) => base + i);
  const firstVis = codes.findIndex((c) => !MISSING.has(c));
  let firstLit = 0; while (firstLit < W && !litCol(firstLit)) firstLit++;
  const firstCell = best.o + 6 * Math.floor((firstLit - best.o) / 6);
  const cell0 = firstCell - 6 * firstVis;
  codes.forEach((c, i) => {
    if (MISSING.has(c)) return;
    const s = cell0 + 6 * i;
    const width = base === 0x80 ? 11 : 5;
    const glyph = rows.map((r) => [...Array(width).keys()].map((k) => (r[s + k] === '#' ? '#' : '.')).join(''));
    out[c] = glyph;
  });
  console.log(`0x${g.toString(16)}: phase ${best.o} (lit gap cols ${best.gapLit}), first cell at ${cell0}`);
}
writeFileSync(new URL('./glyphs.json', import.meta.url), JSON.stringify(out, null, 1));
// print in rows of 8 glyphs
const keys = Object.keys(out).map(Number).sort((a, b) => a - b);
for (let i = 0; i < keys.length; i += 10) {
  const chunk = keys.slice(i, i + 10);
  console.log(chunk.map((k) => String.fromCharCode(k).padEnd(out[k][0].length)).join('  '));
  for (let y = 0; y < 8; y++) console.log(chunk.map((k) => out[k][y]).join('  '));
  console.log();
}
