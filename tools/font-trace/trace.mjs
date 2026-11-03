// Trace dot-matrix glyphs from a still of the globe.
// usage: node trace.mjs <png> <width> <height>   (needs ffmpeg on PATH)
import { execFileSync } from 'node:child_process';

const [png, W, H] = [process.argv[2], +process.argv[3], +process.argv[4]];
const raw = execFileSync('ffmpeg', ['-v', 'error', '-i', png, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], { maxBuffer: 1 << 28 });
const R = (x, y) => raw[(y * W + x) * 3];          // red channel: white dashes high, blue background low
const TH = +(process.env.TH || 140);
const lit = (x, y) => R(x, y) > TH;

// rows: vertical profile
const prof = Array.from({ length: H }, (_, y) => { let n = 0; for (let x = 0; x < W; x++) n += lit(x, y); return n; });
const rows = [];
for (let y = 0; y < H;) {
  if (prof[y] > 6) { let a = y; while (y < H && prof[y] > 6) y++; const b = y; let s = 0, m = 0; for (let k = a; k < b; k++) { s += k * prof[k]; m += prof[k]; } rows.push({ a, b, c: s / m, mass: m }); }
  else y++;
}
const maxMass = Math.max(...rows.map((r) => r.mass));
const R7 = rows.filter((r) => r.mass > maxMass * +(process.env.RFRAC || 0.08)).sort((p, q) => p.c - q.c);
console.log('# rows at y', R7.map((r) => r.c.toFixed(0)).join(' '));
if (process.env.FIXROWS) { R7.length = 0; for (const y of process.env.FIXROWS.split(',')) R7.push({ c: +y }); }

// runs per row
const runs = R7.map((r) => {
  const out = []; const y0 = Math.round(r.c - 3), y1 = Math.round(r.c + 3);
  let x = 0;
  while (x < W) {
    const on = (xx) => { for (let y = y0; y <= y1; y++) if (lit(xx, y)) return true; return false; };
    if (on(x)) { const a = x; while (x < W && on(x)) x++; if (x - a > 6 && a > 8 && x < W - 8) out.push([a, x - 1]); } else x++;
  }
  return out;
});
const lens = runs.flat().map(([a, b]) => b - a + 1).sort((p, q) => p - q);
const w = lens[Math.floor(lens.length * 0.25)];      // a single dot's dash length
// first/last dot centres of every run
const centres = [];
for (const rr of runs) for (const [a, b] of rr) { if (b - a + 1 < w * 1.3) centres.push((a + b) / 2); else centres.push(a + w / 2, b - w / 2); }
// Fit a cylinder: screen x = cx + Rr*sin(arc/Rr), with dots evenly spaced in arc length.
function fit1(vals) {
  let best = null;
  for (let p = +(process.env.PMIN || 20); p <= +(process.env.PMAX || 34); p += 0.1) {
    for (let o = 0; o < p; o += 1) {
      let err = 0;
      for (const c of vals) { const k = (c - o) / p; const d = (k - Math.round(k)) * p; err += Math.min(d * d, (p / 2) ** 2); }
      if (!best || err < best.err) best = { p, o, err };
    }
  }
  return best;
}
let best = null;
if (process.env.FIXED) {
  const [P, CX, RR] = process.env.FIXED.split(',').map(Number);
  const tf = (c) => RR * Math.asin(Math.max(-1, Math.min(1, (c - CX) / RR)));
  let bo = null;
  for (let o = 0; o < P; o += 0.25) {
    let err = 0;
    for (const c of centres) { const k = (tf(c) - o) / P; const d = (k - Math.round(k)) * P; err += Math.min(d * d, (P / 2) ** 2); }
    if (!bo || err < bo.err) bo = { p: P, o, err };
  }
  best = { ...bo, cx: CX, Rr: RR, tf };
} else
for (let cx = W / 2 - 400; cx <= W / 2 + 400; cx += 25) {
  for (const Rr of [Infinity, 4000, 3000, 2400, 2000, 1700, 1500, 1300, 1150, 1000, 900, 800, 700, 600]) {
    const tf = (c) => (Rr === Infinity ? c : Rr * Math.asin(Math.max(-1, Math.min(1, (c - cx) / Rr))));
    const f = fit1(centres.map(tf));
    if (!best || f.err < best.err) best = { ...f, cx, Rr, tf };
  }
}
const { p, o, tf } = best;
const col = (c) => Math.round((tf(c) - o) / p);
const minC = Math.min(...centres.map(col)), maxC = Math.max(...centres.map(col));
const grid = R7.map(() => Array(maxC - minC + 1).fill('.'));
runs.forEach((rr, y) => {
  for (const [a, b] of rr) {
    let k0 = col(a + w / 2), k1 = col(b - w / 2);
    if (k1 < k0 || b - a + 1 < w * 1.3) k0 = k1 = col((a + b) / 2);   // short dash = one dot
    for (let k = k0; k <= k1; k++) if (k - minC >= 0 && k - minC < grid[y].length) grid[y][k - minC] = '#';
  }
});
const rmsResidual = Math.sqrt(best.err / centres.length);
console.log(`# pitch ${p.toFixed(2)} cx ${best.cx} R ${best.Rr} dot ${w}px rms ${rmsResidual.toFixed(2)}px`);
for (const line of grid) console.log(line.join(''));
