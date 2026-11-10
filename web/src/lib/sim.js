// Display simulator: turns (previous frame, new frame, effect catalogue) into
// a picture of the whole LED band at any moment.
//
// Model (docs/protocol.md): the band is ~38 characters around, 8 rows high
// (7 + a descender row);
// the front window shows ~12 characters. A picture is a Float32Array of
// ROWS x BAND intensities (0..1). Effects are animated from the parameters
// recorded while mapping (enterType, exitOrder, ...), so the same catalogue
// drives the firmware table and this preview. Timings are approximate until
// calibrated from video (durationMs in docs/effects.json).

import { glyph, ROWS } from './font.js';
import { encodeText, TEXT_MAX } from './protocol.js';
import { makeAnimPlayer } from './anim.js';

export { ROWS };
export const CHAR_W = 6;                 // typical glyph (5) + gap (1)
export const BAND = 38 * CHAR_W;         // columns around the globe
export const WINDOW = 12 * CHAR_W;       // columns in the front window
export const FRONT = BAND / 2;           // band column at the centre of the front window
const TILE_GAP = 3 * CHAR_W;             // gap between repeated copies of short text
export const SPIN_COLS_PER_S = 39;       // steady spin, measured from video: ~6.5 characters/s
const BLINK_MS = 500;                    // blink/flash half-period (~1 Hz)

/* ---------- text -> columns ---------- */

/** Render codes to columns, with the column span of each character. */
export function renderText(codes) {
  const cols = [];
  const spans = [];
  codes.forEach((code, i) => {
    if (i > 0) cols.push(Array(ROWS).fill(false));
    const g = glyph(code);
    spans.push({ start: cols.length, end: cols.length + g.length });
    for (const c of g) cols.push(c);
  });
  return { cols, spans, width: cols.length };
}

export function textWidth(codes) {
  return renderText(codes).width;
}

const mod = (a, n) => ((a % n) + n) % n;

/**
 * A layout is a full band picture plus, for every band column, which text
 * position it belongs to (-1 = gap). Used by per-letter effects.
 */
function emptyLayout() {
  return { plane: new Float32Array(ROWS * BAND), owner: new Int16Array(BAND).fill(-1), len: 0 };
}

function stamp(layout, text, x0) {
  text.cols.forEach((col, j) => {
    const x = mod(x0 + j, BAND);
    col.forEach((on, y) => { if (on) layout.plane[y * BAND + x] = 1; });
  });
  text.spans.forEach((s, i) => {
    for (let j = s.start; j < s.end + 1; j++) layout.owner[mod(x0 + j, BAND)] = i;
  });
}

/** Spinning text: repeated round the band with a gap, first copy centred in front. */
export function tiledLayout(codes) {
  const L = emptyLayout();
  L.len = codes.length;
  if (!codes.length) return L;
  const text = renderText(codes);
  const unit = text.width + TILE_GAP;
  const copies = Math.max(1, Math.floor(BAND / unit));
  const spacing = BAND / copies;
  const first = text.width <= WINDOW ? FRONT - Math.floor(text.width / 2) : FRONT - WINDOW / 2;
  for (let k = 0; k < copies; k++) stamp(L, text, Math.round(first + k * spacing));
  return L;
}

/** Still text (commands 01/02/05): one copy in the front window. */
export function stillLayout(codes, style) {
  const L = emptyLayout();
  L.len = codes.length;
  if (!codes.length) return L;
  const text = renderText(codes);
  let x0;
  if (style.after === 'instant') x0 = FRONT - WINDOW / 2 + 1;              // left-aligned
  else {
    // Each shift step moves the text one column left; shift = length is centred.
    const shift = style.shift >= 0 ? style.shift : codes.length;
    x0 = FRONT - Math.floor(text.width / 2) + (codes.length - shift);
  }
  stamp(L, text, x0);
  L.spans = text.spans.map((s) => ({ start: s.start + x0, end: s.end + x0 }));
  return L;
}

/* ---------- orders and randomness ---------- */

function rng(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const SCRAMBLE = [2, 4, 7, 6, 5, 8, 3, 1];   // FX 29/33, 1-based positions in order

/** rank[i] = step at which text position i changes; the gap (index n) goes last. */
export function orderRanks(kind, n, seed = 1) {
  const pos = [...Array(n).keys()];
  let seq;                                     // positions in the order they change
  const evens = pos.filter((i) => i % 2 === 1);  // 1-based even positions
  const odds = pos.filter((i) => i % 2 === 0);
  switch (kind) {
    case 'rl': seq = [...pos].reverse(); break;
    case 'even-odd': case 'alternate': seq = [...evens, ...odds]; break;
    case 'even-odd-zig': seq = [...evens, ...odds.reverse()]; break;
    case 'random': {
      const r = rng(seed); seq = [...pos];
      for (let i = seq.length - 1; i > 0; i--) { const j = Math.floor(r() * (i + 1)); [seq[i], seq[j]] = [seq[j], seq[i]]; }
      break;
    }
    case 'scramble': {
      seq = [];
      for (let b = 0; b < n; b += 8) for (const p of SCRAMBLE) if (b + p - 1 < n) seq.push(b + p - 1);
      break;
    }
    default: seq = pos;
  }
  const rank = new Array(n + 1);
  seq.forEach((p, k) => { rank[p] = k; });
  rank[n] = n;
  return { rank, steps: n + 1 };
}

/* ---------- picture helpers ---------- */

const at = (plane, x, y) => (y < 0 || y >= ROWS ? 0 : plane[y * BAND + mod(Math.round(x), BAND)]);

function blank() { return new Float32Array(ROWS * BAND); }

/* ---------- entrance / exit renderers ---------- */

// Slides and wipes play across the front window (what a viewer watches);
// columns outside it switch straight to the new picture.
const WIN_L = FRONT - WINDOW / 2;
const winQ = (x) => mod(x - WIN_L, BAND);    // 0..WINDOW-1 inside the window

// Slides move a banner across the whole band (FX 0 on video: ~8.5 characters/s
// for ~4.5 s), entering at the edge of the front window, so the viewer sees
// the new text travel the whole time. Old text is overwritten (or pushed).
function slideEnter(fx, oldP, newP, u, dir, push) {
  const out = blank();
  const D = BAND, half = BAND / 2;
  for (let x = 0; x < BAND; x++) {
    const q = winQ(x);                         // 0 at the window's left edge, wraps round the band
    const r = mod(WINDOW - 1 - q, BAND);       // 0 at the window's right edge, going left
    for (let y = 0; y < ROWS; y++) {
      let v;
      if (dir === 'split') {
        const mid = mod(q - WINDOW / 2, BAND);  // 0 at the window centre
        const fromLeft = mid >= half;           // left half of the band (seen from the front)
        const d = fromLeft ? BAND - mid : mid;  // distance from the centre
        // both halves close in from the back towards the front centre
        v = d > (1 - u) * half ? at(newP, x + (fromLeft ? -1 : 1) * (1 - u) * half, y) : at(oldP, x, y);
      } else if (dir === 'rl') {
        v = r < u * D ? at(newP, x + (1 - u) * D, y) : push ? at(oldP, x - u * D, y) : at(oldP, x, y);
      } else {
        v = q < u * D ? at(newP, x - (1 - u) * D, y) : push ? at(oldP, x + u * D, y) : at(oldP, x, y);
      }
      out[y * BAND + x] = v;
    }
  }
  return out;
}

function wipe(fromP, toP, u, dir, passes) {
  const out = blank();
  const twoPass = passes && passes.startsWith('2');
  const parity = passes === '2-odd' ? 1 : 0;
  const pass = twoPass && u >= 0.5 ? 1 : 0;
  const pu = twoPass ? (u < 0.5 ? u * 2 : (u - 0.5) * 2) : u;
  const edge = Math.floor(pu * WINDOW);
  for (let x = 0; x < BAND; x++) {
    let q = winQ(x);
    const inside = q < WINDOW;
    if (dir === 'rl' && inside) q = WINDOW - 1 - q;
    let done;
    if (!inside) done = true;
    else if (!twoPass) done = q < edge;
    else if (pass === 0) done = q < edge && x % 2 === parity;
    else done = x % 2 === parity || q < edge;
    for (let y = 0; y < ROWS; y++) {
      out[y * BAND + x] = inside && q === edge && pu < 1 ? 1 : done ? at(toP, x, y) : at(fromP, x, y);
    }
  }
  return out;
}

const ROW_ORDER = {   // 8 rows: 0-6 letters, 7 = descenders
  'bottom-up': [7, 6, 5, 4, 3, 2, 1, 0], 'top-down': [0, 1, 2, 3, 4, 5, 6, 7],
  'center-out': [3, 4, 2, 5, 1, 6, 0, 7], 'outer-in': [0, 7, 6, 1, 5, 2, 4, 3], 'odd-even': [0, 2, 4, 6, 1, 3, 5, 7],
};

function rows(oldP, newP, u, pattern) {
  const order = ROW_ORDER[pattern] || ROW_ORDER['top-down'];
  const shown = Math.floor(u * (ROWS + 0.999));
  const out = blank();
  order.slice(0, shown).forEach((y) => { for (let x = 0; x < BAND; x++) out[y * BAND + x] = newP[y * BAND + x]; });
  return out;
}

function vshift(p, off, flip) {
  const out = blank();
  const o = Math.round(off);
  for (let y = 0; y < ROWS; y++) {
    const sy = flip ? ROWS - 1 - (y - o) : y - o;
    for (let x = 0; x < BAND; x++) out[y * BAND + x] = at(p, x, sy);
  }
  return out;
}

/** Per-letter effects: each text position changes at its rank in the order. */
function perLetter(oldP, newL, u, kind, order, seed, phase) {
  const n = newL.len;
  const { rank, steps } = orderRanks(order, n, seed);
  const out = blank();
  for (let x = 0; x < BAND; x++) {
    const owner = newL.owner[x];
    const r = rank[owner < 0 ? n : owner];
    const local = Math.min(1, Math.max(0, u * steps - r));   // 0 before its turn, 1 after
    for (let y = 0; y < ROWS; y++) {
      let v;
      if (kind === 'letters') {
        if (phase === 'exit') v = local < 1 ? at(oldP, x, y) : 0;
        else if (phase === 'enter') v = local < 1 ? 0 : at(newL.plane, x, y);
        else v = local < 1 ? at(oldP, x, y) : at(newL.plane, x, y);   // swap in place
      } else if (kind === 'drop') {
        v = local < 0.5 ? at(oldP, x, y - Math.round(local * 2 * ROWS))
          : at(newL.plane, x, y + Math.round((1 - (local - 0.5) * 2) * ROWS));
        if (local === 0) v = at(oldP, x, y);
      } else {                                                   // pluck
        v = local < 0.6 ? at(oldP, x, y + Math.round((local / 0.6) * ROWS)) : at(newL.plane, x, y);
        if (local === 0) v = at(oldP, x, y);
      }
      out[y * BAND + x] = v;
    }
  }
  return out;
}

function columns(p, L, u, pattern, seed, out) {
  const r = rng(seed);
  const thresh = Array.from({ length: BAND }, () => r());
  const res = blank();
  // Character spans on the band, from the owner map.
  for (let x = 0; x < BAND; x++) {
    let vis;
    if (pattern === 'random') vis = thresh[x] < u;
    else {
      const o = L.owner[x];
      let a = x, b = x;
      while (o >= 0 && L.owner[mod(a - 1, BAND)] === o && x - a < 12) a--;
      while (o >= 0 && L.owner[mod(b + 1, BAND)] === o && b - x < 12) b++;
      const w = b - a + 1, j = x - a;
      if (pattern === 'char-center') vis = Math.abs(j - (w - 1) / 2) <= (1 - u) * (w / 2) + 0.01;
      else vis = (j + 1) / w <= u + 0.001;   // char-lr
    }
    if (out) vis = pattern === 'random' ? thresh[x] >= u : vis;
    for (let y = 0; y < ROWS; y++) res[y * BAND + x] = vis ? p[y * BAND + x] : 0;
  }
  return res;
}

function pixels(p, u, seed, out) {
  const r = rng(seed);
  const res = blank();
  for (let i = 0; i < res.length; i++) {
    const t = r();
    res[i] = (out ? t >= u : t < u) ? p[i] : 0;
  }
  return res;
}

function fade(p, k) { return p.map((v) => v * k); }

/** One phase (exit or enter) of an effect at progress u. */
function exitPicture(fx, oldL, u) {
  const p = oldL.plane;
  switch (fx.exitType) {
    case 'vslide': return vshift(p, (fx.exitVDir === 'up' ? -1 : 1) * u * ROWS, false);
    case 'wipe': return wipe(p, blank(), u, fx.exitDir, fx.exitPasses);
    case 'fade': return fade(p, 1 - u);
    case 'columns': return columns(p, oldL, u, fx.exitColPattern || 'char-center', 7, true);
    case 'pixels': return pixels(p, u, 11, true);
    case 'letters': return perLetter(p, { ...oldL, plane: blank() }, u, 'letters', fx.exitOrder, 3, 'exit');
    default: return u < 1 ? p : blank();
  }
}

function enterPicture(fx, oldP, newL, u) {
  const p = newL.plane;
  switch (fx.enterType) {
    case 'slide': return slideEnter(fx, oldP, p, u, fx.enterDir, fx.exitType === 'slide');
    case 'wipe': return wipe(oldP, p, u, fx.enterDir, fx.enterPasses);
    case 'rows': return rows(oldP, p, u, fx.enterPattern);
    case 'vslide': {
      const flip = fx.enterFlip === 'flip' && u < 0.85;
      const k = Math.min(1, u / 0.85);
      return vshift(p, (fx.enterVDir === 'down' ? -1 : 1) * (1 - k) * ROWS, flip);
    }
    case 'fade': return fade(p, u);
    case 'columns': return columns(p, newL, u, fx.enterColPattern || 'random', 5, false);
    case 'pixels': return pixels(p, u, 13, false);
    case 'letters': return perLetter(oldP, newL, u, 'letters', fx.enterOrder, 9,
      fx.exitType === 'letters' ? 'enter' : 'swap');
    default: return p;
  }
}

/** Picture of an entrance at progress u (0..1). */
export function effectPicture(fx, oldL, newL, u) {
  const ex = fx.exitType || 'overwrite';
  if ((ex === 'drop' || ex === 'pluck') && fx.enterType === ex) {
    return perLetter(oldL.plane, newL, u, ex, fx.enterOrder, 9, 'swap');
  }
  if (ex === 'crossfade' || (ex === 'fade' && fx.enterType === 'fade')) {
    const out = blank();
    for (let i = 0; i < out.length; i++) out[i] = oldL.plane[i] * (1 - u) + newL.plane[i] * u;
    return out;
  }
  if (ex === 'overwrite') return enterPicture(fx, oldL.plane, newL, u);
  if (ex === 'instant') return enterPicture(fx, blank(), newL, u);
  // Separate exit, a short blank, then the entrance.
  if (u < 0.45) return exitPicture(fx, oldL, u / 0.45);
  if (u < 0.5) return blank();
  return enterPicture(fx, blank(), newL, (u - 0.5) / 0.5);
}

/* ---------- scenes ---------- */

export function spin(L, ms) {
  const off = (ms / 1000) * SPIN_COLS_PER_S;
  const whole = Math.floor(off), frac = off - whole;
  const out = blank();
  for (let y = 0; y < ROWS; y++) {
    for (let x = 0; x < BAND; x++) {
      // content moves right to left; blend neighbouring columns for smooth motion
      out[y * BAND + x] = at(L.plane, x + whole, y) * (1 - frac) + at(L.plane, x + whole + 1, y) * frac;
    }
  }
  return out;
}

export function stillAt(L, style, ms) {
  const on = Math.floor(ms / BLINK_MS) % 2 === 0;
  if (style.after === 'flash') return on ? L.plane : blank();   // every character blinks
  if (style.after === 'hold' && style.blink > 0 && !on) {
    const out = L.plane.slice();
    const n = Math.min(style.blink, L.len);
    for (let x = 0; x < BAND; x++) {
      const o = L.owner[x];
      if (o >= 0 && o < n) for (let y = 0; y < ROWS; y++) out[y * BAND + x] = 0;
    }
    return out;
  }
  return L.plane;
}

/** Steady picture of a frame after its entrance (used as "previous" too). */
export function steadyLayout(codes, style) {
  return style.after === 'spin' ? tiledLayout(codes) : stillLayout(codes, style);
}

/** How the engine sends one animation step (scene_engine.c anim_tick): still text, 05 frame. */
export function animStepStyle(step, style, finite) {
  const st = { after: 'hold', blink: 0, shift: style.after === 'hold' ? style.shift ?? -1 : -1 };
  if (step.shift >= 0) st.shift = step.shift;
  if (step.blink >= 0) st.blink = step.blink;
  else if (!finite && style.after === 'flash') st.after = 'flash';      // a loop keeps the frame's blinking
  else if (!finite && style.after === 'hold') st.blink = style.blink || 0;
  return st;
}

/**
 * A frame with a custom animation (anim.js): a finite one plays as the entrance
 * and then the frame settles into its style; a looping one plays all along
 * (loopMs = Infinity: the preview shouldn't restart it).
 */
function makeAnimScene(next) {
  const style = next.style;
  const player = makeAnimPlayer(next.anim, {
    target: next.text ?? String.fromCodePoint(...next.codes), from: next.from ?? null,
    shift: style.after === 'hold' ? style.shift ?? -1 : -1, gapMs: next.gapMs ?? 10, fill: next.fill,
  });
  const finite = player.finite;
  const entranceMs = finite ? player.totalMs : 0;
  const tiled = tiledLayout(next.codes);
  const steady = steadyLayout(next.codes, style);
  let last = null, lastL = null;
  const stepPicture = (ms) => {
    const s = player.at(Math.max(0, ms));
    const st = animStepStyle(s, style, finite);
    if (s !== last) { last = s; lastL = stillLayout(encodeText(s.text).codes.slice(0, TEXT_MAX), st); }
    return stillAt(lastL, st, ms);
  };
  return {
    entranceMs,
    loopMs: finite ? undefined : Infinity,
    player,
    pictureAt(ms) {
      if (!finite || ms < entranceMs) return stepPicture(ms);
      const t = ms - entranceMs;
      if (style.after === 'spin') return spin(tiled, t);
      return stillAt(steady, style, t);
    },
  };
}

/**
 * A playable scene: from the previous frame (or blank) into `next`.
 * next = {codes, style:{entrance, after, blink, shift}}; effects = effects.json "effects".
 * With next.anim (a custom animation) also: text (string or ms => string, live
 * values filled in), fill (fills sequence templates), from (count: the old text).
 * Returns {entranceMs, pictureAt(ms)} (+ loopMs for looping animations).
 */
export function makeScene(prev, next, effects) {
  if (next.anim) return makeAnimScene(next);
  const oldL = prev ? steadyLayout(prev.codes, prev.style) : emptyLayout();
  const fxId = next.style.entrance;
  const fx = fxId >= 0 ? effects[String(fxId)] : null;
  const entranceMs = fx ? fx.durationMs || 1500 : 0;
  const tiled = tiledLayout(next.codes);
  const steady = steadyLayout(next.codes, next.style);
  return {
    entranceMs,
    pictureAt(ms) {
      if (fx && ms < entranceMs) return effectPicture(fx, oldL, tiled, ms / entranceMs);
      const t = ms - entranceMs;
      if (next.style.after === 'spin') return spin(tiled, t);
      return stillAt(steady, next.style, t);
    },
  };
}

/** Columns [start, start+WINDOW) of the band seen from the front. */
export function frontWindow() {
  return { start: FRONT - WINDOW / 2, end: FRONT + WINDOW / 2 };
}

/**
 * A whole scene: frames play in order, each entering from the one before
 * (the first from the last, as when the scene loops).
 * frames = [{codes, style, ms}]; ms = how long the frame stays (entrance included).
 * Returns {entranceMs: total length, pictureAt(ms), frameAt(ms)}.
 */
export function makeSequence(frames, effects) {
  const segs = [];
  let t = 0;
  frames.forEach((f, i) => {
    const prev = frames.length > 1 ? frames[(i + frames.length - 1) % frames.length] : null;
    const sc = makeScene(prev, f, effects);
    const ms = Math.max(f.ms || 6000, sc.entranceMs + 500);
    segs.push({ t0: t, ms, sc });
    t += ms;
  });
  const find = (ms) => {
    const m = t > 0 ? ((ms % t) + t) % t : 0;
    let k = segs.length - 1;
    while (k > 0 && segs[k].t0 > m) k--;
    return { k, local: m - segs[k].t0 };
  };
  return {
    entranceMs: t,
    pictureAt(ms) {
      if (!segs.length) return new Float32Array(ROWS * BAND);
      const { k, local } = find(ms);
      return segs[k].sc.pictureAt(local);
    },
    frameAt(ms) { return segs.length ? find(ms).k : 0; },
  };
}
