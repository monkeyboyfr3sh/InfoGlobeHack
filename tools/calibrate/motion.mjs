// Horizontal motion of the globe's text between video frames.
// usage: node motion.mjs <video> <start s> <dur s> <crop w:h:x:y> [scaleW]
// Prints per-frame shift (px in the scaled crop) found by cross-correlating
// column brightness profiles of consecutive frames, plus the character pitch
// from the profile's autocorrelation.
import { execFileSync } from 'node:child_process';

const [video, ss, dur, crop, scaleW = '960'] = process.argv.slice(2);
const [cw, ch] = crop.split(':').map(Number);
const W = +scaleW, H = Math.round(ch * W / cw / 2) * 2;
const raw = execFileSync('ffmpeg', ['-v', 'error', '-ss', ss, '-t', dur, '-i', video,
  '-vf', `crop=${crop},scale=${W}:${H}`, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], { maxBuffer: 1 << 30 });
const fsz = W * H * 3;
const n = Math.floor(raw.length / fsz);
const profiles = [];
for (let f = 0; f < n; f++) {
  const p = new Float64Array(W);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) p[x] += raw[f * fsz + (y * W + x) * 3];   // red channel = lit dots
  const mean = p.reduce((a, b) => a + b, 0) / W;
  for (let x = 0; x < W; x++) p[x] -= mean;
  profiles.push(p);
}
function bestShift(a, b, maxS) {
  let best = { s: 0, c: -Infinity };
  const m = Math.floor(W * 0.15);                         // ignore the edges (dome curve)
  for (let s = -maxS; s <= maxS; s += 0.25) {
    let c = 0;
    for (let x = m; x < W - m; x++) {
      const xb = x + s, i = Math.floor(xb), fr = xb - i;
      const v = (b[i] ?? 0) * (1 - fr) + (b[i + 1] ?? 0) * fr;
      c += a[x] * v;
    }
    if (c > best.c) best = { s, c };
  }
  return best.s;
}
const shifts = [];
for (let f = 1; f < n; f++) shifts.push(bestShift(profiles[f - 1], profiles[f], 30));
// autocorrelation of the first frame -> character pitch
const p0 = profiles[0];
const ac = [];
for (let L = 4; L < W / 4; L++) { let c = 0; for (let x = 0; x + L < W; x++) c += p0[x] * p0[x + L]; ac.push([L, c]); }
const peaks = ac.filter((v, i) => i > 0 && i < ac.length - 1 && v[1] > ac[i - 1][1] && v[1] > ac[i + 1][1]).sort((a, b) => b[1] - a[1]).slice(0, 5);
console.log(JSON.stringify({ frames: n, W, H, shifts: shifts.map((s) => +s.toFixed(2)), acPeaks: peaks.map((p) => p[0]) }));
