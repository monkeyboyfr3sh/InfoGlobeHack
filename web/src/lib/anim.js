// Custom animations: still frames sent one step at a time (docs/animations.md).
// Mirrors hello_world/main/src/globe_anim.c line for line: the same animation,
// input and step number give the same text, position and step time, including
// the "random" parts (a stateless hash seeded from the inputs). Change both
// together; hello_world/test_host checks they agree.
//
// Text is handled as code points (one cell = one glyph on the globe). Integer
// arithmetic only, so both sides round identically.

export const CELLS_MAX = 35;
export const SEQ_MAX = 16;
export const SEQ_TEXT_MAX = 63;      // bytes of UTF-8, before live values
const FRAME_EXTRA = 5;               // bytes of a 05 frame besides its text
const MS_MIN = 20, MS_MAX = 60000;

const TYPES = ['none', 'decode', 'typewriter', 'count', 'bounce', 'scanner', 'sprites', 'automaton', 'sequence', 'flap', 'sparkle', 'wave'];
const DEFAULT_MS = { decode: 110, typewriter: 140, count: 70, bounce: 100, scanner: 110, sprites: 150, automaton: 400, sequence: 400,
  flap: 70, sparkle: 150, wave: 110 };
const CHOICES = {
  order: ['lr', 'rl', 'random'], noise: ['match', 'symbols', 'mixed'], mode: ['number', 'digits'],
  sprite: ['pacman', 'chase', 'pulse', 'fish'], start: ['single', 'random'],
};
const NUMS = {   // [min, max] like ga_set_num()
  ms: [MS_MIN, MS_MAX], lead: [0, 30], steps: [1, 60], density: [1, 20], crest: [1, 8], period: [400, 20000], pause: [0, 5000], amp: [0, 60],
  width: [3, CELLS_MAX], rule: [0, 255], gens: [2, 250], seed: [0, 4294967295],
};
export const ANIM_KEYS = [...Object.keys(NUMS), ...Object.keys(CHOICES), 'loop', 'cursor', 'bg', 'live', 'dead', 'trail', 'texts'];

const MISSING = '%&+;@[\\]^_`{|}~';
const drawable = (c) => c >= 0x20 && c < 0x7f && !MISSING.includes(String.fromCharCode(c));
const one = (v) => v.length === 1 && drawable(v.charCodeAt(0));
const utf8Len = (cp) => (cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4);

/** Clip to SEQ_TEXT_MAX bytes of UTF-8 without splitting a character (ga_add_text). */
function clipText(s) {
  let bytes = 0, out = '';
  for (const ch of s) {
    const n = utf8Len(ch.codePointAt(0));
    if (bytes + n > SEQ_TEXT_MAX) break;
    bytes += n;
    out += ch;
  }
  return out;
}

/** Defaults for a type (ga_init); null if unknown. */
function init(type) {
  if (typeof type !== 'string' || TYPES.indexOf(type) <= 0) return null;
  return {
    type, ms: DEFAULT_MS[type], lead: 4, order: 'lr', noise: 'match', steps: 12, density: 3, crest: 2, mode: 'number',
    period: 3000, pause: 100, amp: 0, width: type === 'automaton' ? 35 : 10, sprite: 'pacman',
    rule: 30, gens: 120, start: 'single', loop: true, cursor: '#', bg: ' ', live: '#', dead: ' ', trail: '',
    seed: 0, texts: [],
  };
}

function setNum(a, key, v) {
  if (!Number.isFinite(v)) return;
  if (key === 'loop') { a.loop = v !== 0; return; }
  const r = NUMS[key];
  if (r) a[key] = Math.trunc(Math.min(r[1], Math.max(r[0], v)));
}

function setStr(a, key, v) {
  if (CHOICES[key]) { if (CHOICES[key].includes(v)) a[key] = v; }
  else if (key === 'cursor') { if (v === '' || v === 'none') a.cursor = ''; else if (one(v)) a.cursor = v; }
  else if (key === 'bg' || key === 'live' || key === 'dead') { if (one(v)) a[key] = v; }
  else if (key === 'trail') { if (v.length < 4 && [...v].every((c) => drawable(c.charCodeAt(0)))) a.trail = v; }
}

const NORMALIZED = new WeakSet();     // objects normalizeAnim() made

/**
 * The scene JSON "anim" object -> a full, validated animation (or null), exactly
 * as the engine parses it: unknown keys and invalid values are ignored, numbers
 * clamped, sequence texts limited to 16 of up to 63 bytes.
 */
export function normalizeAnim(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const a = init(raw.type);
  if (!a) return null;
  for (const [key, v] of Object.entries(raw)) {
    if (typeof v === 'number') setNum(a, key, v);
    else if (typeof v === 'boolean') setNum(a, key, v ? 1 : 0);
    else if (typeof v === 'string' && key !== 'type') setStr(a, key, v);
    else if (key === 'texts' && Array.isArray(v)) {
      for (const t of v) if (typeof t === 'string' && a.texts.length < SEQ_MAX) a.texts.push(clipText(t));
    }
  }
  NORMALIZED.add(a);
  return a;
}

/** Keep only what the engine reads (for saving and importing scenes). */
export function cleanAnim(raw) {
  if (!raw || typeof raw !== 'object' || !init(raw.type)) return null;
  const out = { type: raw.type };
  for (const k of ANIM_KEYS) {
    const v = raw[k];
    if (k === 'texts') { if (Array.isArray(v)) out.texts = v.filter((t) => typeof t === 'string').slice(0, SEQ_MAX); }
    else if (['number', 'string', 'boolean'].includes(typeof v)) out[k] = v;
  }
  return out;
}

const norm = (a) => (NORMALIZED.has(a) ? a : normalizeAnim(a));

/** Ends on the full text (an entrance) rather than looping (ga_finite). */
export function isFiniteAnim(anim) {
  const a = norm(anim);
  if (!a) return true;
  if (['decode', 'typewriter', 'count', 'flap'].includes(a.type)) return true;
  return a.type === 'sequence' && (!a.loop || !a.texts.length);
}

/**
 * The animation a frame's "onChange" plays when a live value changes, as the
 * engine reads it: an anim object, or a name ("decode") which takes the frame's
 * own anim options when the type matches. Only finite ones; else null.
 */
export function changeAnimOf(frame) {
  const oc = frame && frame.onChange;
  let a = null;
  if (oc && typeof oc === 'object') a = normalizeAnim(oc);
  else if (typeof oc === 'string' && oc !== 'random') {
    a = frame.anim && frame.anim.type === oc ? normalizeAnim(frame.anim) : normalizeAnim({ type: oc });
  }
  return a && isFiniteAnim(a) ? a : null;
}

/** Time on the IR link for a still frame of `cells` characters: 1 ms per bit plus the gap. */
export const airtimeMs = (cells, gapMs = 10) => (cells + FRAME_EXTRA) * 8 + gapMs;

/* ---- text as cells ---- */

function cellsOf(s) {
  const c = [...(s || '')].slice(0, CELLS_MAX);
  return { c, cp: c.map((ch) => ch.codePointAt(0)), n: c.length };
}

/* ---- deterministic randomness (same hash as globe_anim.c) ---- */

function mix(x) {
  x >>>= 0;
  x = Math.imul(x ^ (x >>> 16), 0x7feb352d);
  x = Math.imul(x ^ (x >>> 15), 0x846ca68b);
  return (x ^ (x >>> 16)) >>> 0;
}
const rnd = (seed, a, b) => mix(seed ^ mix(a + mix(b + 0x9e3779b9)));

function textSeed(a, t) {
  let h = 2166136261;
  for (const cp of t.cp) h = Math.imul(h ^ cp, 16777619) >>> 0;
  return mix(h ^ Math.imul(TYPES.indexOf(a.type), 0x9e3779b9) ^ a.seed);
}

/* ---- the animations ---- */

const UPPER = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', LOWER = 'abcdefghijklmnopqrstuvwxyz', DIGIT = '0123456789';
const SYMBOL = '#$*<>=?/', MIXED = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$*<>=?';
const isDigit = (cp) => cp >= 48 && cp <= 57;

/** The turn each of n cells takes (0 = first): left to right, right to left, or shuffled. */
function orderRanks(a, n) {
  const rank = Array.from({ length: n }, (_, i) => (a.order === 'rl' ? n - 1 - i : i));
  if (a.order === 'random') {
    // seeded without the text, so the order holds still while a colon blinks
    const pos = rank.map((_, i) => i);
    const s = mix(a.seed ^ Math.imul(n, 0x85ebca6b));
    for (let j = n - 1; j > 0; j--) {
      const r = rnd(s, 0x5eed, j) % (j + 1);
      [pos[j], pos[r]] = [pos[r], pos[j]];
    }
    pos.forEach((p, r) => { rank[p] = r; });
  }
  return rank;
}

function decode(a, t, k) {
  const n = t.n, span = Math.min(n, 16);
  const rank = orderRanks(a, n);
  const seed = textSeed(a, t);
  let text = '';
  t.c.forEach((ch, i) => {
    const cp = t.cp[i];
    let set = null;
    if (cp !== 32) {
      if (a.noise === 'symbols') set = SYMBOL;
      else if (a.noise === 'mixed') set = MIXED;
      else if (cp >= 65 && cp <= 90) set = UPPER;
      else if (cp >= 97 && cp <= 122) set = LOWER;
      else if (isDigit(cp)) set = DIGIT;
    }
    const lock = a.lead + Math.floor((rank[i] * span) / n);
    text += !set || k >= lock ? ch : set[rnd(seed, k, i) % set.length];
  });
  return { text, done: n === 0 || k + 1 >= a.lead + span };
}

function typewriter(a, t, k) {
  let text = '';
  t.c.forEach((ch, i) => { text += i < k ? ch : i === k && a.cursor ? a.cursor : ' '; });
  return { text, done: k >= t.n };
}

function digitRuns(t) {
  const runs = [];
  for (let i = 0; i < t.n;) {
    if (!isDigit(t.cp[i])) { i++; continue; }
    let j = i;
    while (j < t.n && isDigit(t.cp[j])) j++;
    runs.push({ start: i, len: j - i });
    i = j;
  }
  return runs;
}
const runValue = (t, r) => { let v = 0; for (let i = 0; i < r.len; i++) v = v * 10 + (t.cp[r.start + i] - 48); return v; };

function count(a, input, t, k) {
  const f = cellsOf(input.from || '');
  const tr = digitRuns(t), fr = digitRuns(f);
  const S = a.steps, j = Math.min(k + 1, S);
  const ch = t.c.slice();
  let counted = 0;
  tr.forEach((run, r) => {
    const w = run.len, s0 = run.start;
    if (w > 9) return;                                          // too long to be a number: as it is
    counted++;
    const V = runValue(t, run);
    let F = r < fr.length && fr[r].len <= 9 ? runValue(f, fr[r]) : 0;
    if (a.mode === 'digits') {
      for (let d = w - 1; d >= 0; d--) {                       // right-aligned, like wheels
        const b = t.cp[s0 + d] - 48, a0 = F % 10, dist = (b - a0 + 10) % 10;
        F = Math.floor(F / 10);
        ch[s0 + d] = String((a0 + Math.floor((dist * j) / S)) % 10);
      }
    } else {
      const D3 = S * S * S, rest = S - j, e = D3 - rest * rest * rest;   // ease-out cubic
      let v = V >= F ? F + Math.floor(((V - F) * e * 2 + D3) / (2 * D3)) : F - Math.floor(((F - V) * e * 2 + D3) / (2 * D3));
      v = Math.min(v, 10 ** w - 1);
      const zero = w > 1 && t.cp[s0] === 48;                  // "05" keeps its leading zero
      for (let d = w - 1; d >= 0; d--) {
        if (d === w - 1 || v > 0) { ch[s0 + d] = String(v % 10); v = Math.floor(v / 10); }
        else ch[s0 + d] = zero ? '0' : ' ';
      }
    }
  });
  return { text: ch.join(''), done: counted === 0 || k + 1 >= S };
}

function bounce(a, input, t, k) {
  const n = t.n;
  const N = Math.min(CELLS_MAX, Math.max(n, 8, a.amp));
  const left = Math.floor((N - n) / 2);
  const text = ' '.repeat(left) + t.c.join('') + ' '.repeat(N - n - left);
  const c = input.shift >= 0 ? Math.min(input.shift, 255) : N;
  let amp = a.amp ? a.amp : N <= 12 ? Math.max(36 - 3 * N, 2) : 3 * N - 36;   // fill the ~72-column window
  amp = Math.min(amp, c, 255 - c);
  const lo = c - amp, hi = c + amp;
  const step = Math.max(a.ms, airtimeMs(N, input.gapMs), input.minMs);
  const half = Math.floor(a.period / 2), P = a.pause < half ? a.pause : half - 1, M = half - P;
  const tm = k * step + P + Math.floor(M / 2);                 // start mid-swing, in the centre
  const cyc = tm % (2 * half), u = cyc % half, ed = M * M * M;
  let en = 0;
  if (u >= P) { const x = u - P; en = 3 * M * x * x - 2 * x * x * x; }   // smoothstep
  const d = Math.floor(((hi - lo) * en * 2 + ed) / (2 * ed));
  return { text, shift: cyc < half ? lo + d : hi - d };
}

const MIRROR = { '<': '>', '>': '<', '(': ')', ')': '(' };

function scanner(a, t, k) {
  const s = t.n ? t : cellsOf('*');
  const L = Math.min(s.n, CELLS_MAX - 1);
  const W = Math.max(a.width, L + 1), P = W - L;
  const m = k % (2 * P);
  const pos = m <= P ? m : 2 * P - m;
  const right = m < P, flip = !right && s.cp.some((cp) => cp === 60 || cp === 62);
  const cells = Array(W).fill(a.bg);
  [...a.trail].forEach((g, j) => {
    const c = right ? pos - 1 - j : pos + L + j;
    if (c >= 0 && c < W) cells[c] = g;
  });
  for (let i = 0; i < L; i++) {
    const ch = s.c[flip ? L - 1 - i : i];
    cells[pos + i] = flip && MIRROR[ch] ? MIRROR[ch] : ch;
  }
  return { text: cells.join('') };
}

const PULSE = ['.', 'o', 'O', 'o', '.', ' ', ' ', ' '];

function sprites(a, t, k) {
  let W = a.width;
  let c;
  switch (a.sprite) {
    case 'pacman': {                  // C chomps along a row of dots (or your text), then it all comes back
      W = Math.max(W, t.n);
      const p = (k % (W + 3)) - 1;
      c = Array.from({ length: W }, (_, i) => {
        if (p >= 0 && i < p) return ' ';
        if (p >= 0 && i === p) return k % 2 ? 'O' : 'C';
        if (t.n) return i < t.n ? t.c[i] : ' ';
        return i % 2 ? '.' : ' ';
      });
      break;
    }
    case 'chase': {                   // => closes in on a dawdling >, which dashes off; the chaser follows out
      W = Math.max(W, 6);
      const s = Math.floor(W / 3), m = k % (W + 3), H = m - 1;
      const L = m <= 2 * s - 2 ? s + Math.floor(m / 2) : 2 * s - 1 + 2 * (m - (2 * s - 2));
      c = Array(W).fill(' ');
      if (L < W) c[L] = '>';
      if (H >= 0 && H < W) c[H] = '>';
      if (H >= 1 && H <= W) c[H - 1] = '=';
      break;
    }
    case 'pulse':                     // . o O o . rippling out from the middle
      c = Array.from({ length: W }, (_, i) => PULSE[((k % 8) + 8 - (Math.floor(Math.abs(2 * i - (W - 1)) / 2) % 8)) % 8]);
      break;
    default: {                        // a fish swims across, turns, swims back
      W = Math.max(W, 5);
      const P = W - 3, m = k % (2 * P + 2);
      const right = m <= P;
      const pos = right ? m : m === P + 1 ? P : 2 * P + 1 - m;
      c = Array(W).fill(' ');
      [...(right ? '><>' : '<><')].forEach((g, i) => { c[pos + i] = g; });
    }
  }
  return { text: c.join('') };
}

function automaton(a, t, k) {
  const W = a.width;
  const b = Math.floor(k / a.gens), g = k % a.gens, seed = textSeed(a, t);
  let s = Array.from({ length: W }, (_, i) => (a.start === 'random' ? rnd(seed, b, i) & 1 : i === Math.floor(W / 2) ? 1 : 0));
  for (let step = 1; step <= g; step++) {
    let nx = s.map((_, i) => (a.rule >> ((s[(i + W - 1) % W] << 2) | (s[i] << 1) | s[(i + 1) % W])) & 1);
    const alive = nx.reduce((x, y) => x + y, 0);
    if (alive === 0 || alive === W) nx = nx.map((_, i) => rnd(seed, b * 256 + step, i + 1000) & 1);
    s = nx;
  }
  return { text: s.map((v) => (v ? a.live : a.dead)).join('') };
}

const wheelOf = (cp) => (cp >= 65 && cp <= 90 ? UPPER : cp >= 97 && cp <= 122 ? LOWER : isDigit(cp) ? DIGIT : null);

// A departure board: every letter flips forward through its alphabet and stops
// on the text. As an entrance the cells start one after another from blank;
// when a live value changed (`from`), each flips on from what it showed.
function flap(a, input, t, k) {
  const n = t.n, span = Math.min(n, 16);
  const rank = orderRanks(a, n);
  const f = cellsOf(input.from || '');
  const change = f.n > 0;
  const seed = textSeed(a, t), least = Math.min(a.steps, 3);
  let last = 0, text = '';
  t.c.forEach((ch, i) => {
    const set = wheelOf(t.cp[i]);
    const delay = change ? 0 : Math.floor((rank[i] * span) / n);
    let flips = 0, len = 1, p = 0;
    if (set) {
      len = set.length;
      p = t.cp[i] - set.charCodeAt(0);
      if (change && i < f.n && wheelOf(f.cp[i]) === set) flips = (p + len - (f.cp[i] - set.charCodeAt(0))) % len;
      else flips = least + (rnd(seed, 0xf1a9, i) % (a.steps - least + 1));
    }
    last = Math.max(last, delay + flips);
    if (k >= delay + flips) text += ch;
    else if (k < delay) text += ' ';
    else text += set[(p + len - ((delay + flips - k) % len)) % len];
  });
  return { text, done: k >= last };
}

// The text holds still while stars flash over it here and there; with no text, a field of them.
function sparkle(a, t, k) {
  const W = t.n ? t.n : a.width;
  const seed = mix(a.seed ^ 0x5ba7c1e5);                // not the text: a ticking clock keeps its sky
  let text = '';
  for (let i = 0; i < W; i++) text += rnd(seed, k, i) % 40 < a.density ? '*' : t.n ? t.c[i] : ' ';
  return { text };
}

// A ripple `crest` letters wide runs along the text, swapping capitals and small letters under it.
function wave(a, t, k) {
  const n = t.n, c = a.crest;
  const head = k % (n + c + a.lead);
  let text = '';
  t.c.forEach((ch, i) => {
    const j = a.order === 'rl' ? n - 1 - i : i, cp = t.cp[i];
    const under = j <= head && j > head - c;
    text += under && ((cp >= 65 && cp <= 90) || (cp >= 97 && cp <= 122)) ? String.fromCharCode(cp ^ 0x20) : ch;
  });
  return { text };
}

/**
 * What to show at step k (0 = first). anim: an "anim" object (raw or normalized).
 * input: {target, from, shift = -1, gapMs = 10, minMs = 0}.
 * Returns {text, shift (-1 = the frame's), blink (-1 = the frame's), ms, done, tmpl}.
 */
export function animStep(anim, input, k) {
  const a = norm(anim);
  const inp = { target: input.target, from: input.from ?? null, shift: input.shift ?? -1, gapMs: input.gapMs ?? 10, minMs: input.minMs ?? 0 };
  const t = cellsOf(inp.target);
  let r;
  switch (a && a.type) {
    case 'decode': r = decode(a, t, k); break;
    case 'typewriter': r = typewriter(a, t, k); break;
    case 'count': r = count(a, inp, t, k); break;
    case 'bounce': r = bounce(a, inp, t, k); break;
    case 'scanner': r = scanner(a, t, k); break;
    case 'sprites': r = sprites(a, t, k); break;
    case 'automaton': r = automaton(a, t, k); break;
    case 'flap': r = flap(a, inp, t, k); break;
    case 'sparkle': r = sparkle(a, t, k); break;
    case 'wave': r = wave(a, t, k); break;
    case 'sequence':
      if (a.texts.length && (a.loop || k < a.texts.length)) r = { text: a.texts[k % a.texts.length], tmpl: true };
      else r = { text: t.c.join(''), done: true };
      break;
    default: r = { text: t.c.join(''), done: true };
  }
  const cells = Math.min(CELLS_MAX, [...r.text].length);
  const ms = Math.max(a ? a.ms : 0, airtimeMs(cells, inp.gapMs), inp.minMs);
  return { text: r.text, shift: r.shift ?? -1, blink: -1, ms, done: !!r.done, tmpl: !!r.tmpl };
}

/**
 * Plays an animation on a clock the way the engine does: each step stays up for
 * max(its time, its frame's airtime). opts: {target (string or ms => string),
 * from, shift, gapMs, minMs, fill (fills live values into sequence templates)}.
 * at(ms) -> the step showing at ms; totalMs = when a finite animation has
 * shown its last step (Infinity for loops).
 */
export function makeAnimPlayer(anim, opts = {}) {
  const a = norm(anim);
  const { target = '', from = null, shift = -1, gapMs = 10, minMs = 0, fill = (s) => s } = opts;
  const targetAt = typeof target === 'function' ? target : () => target;
  const finite = isFiniteAnim(a);
  const make = (k, t0) => {
    const s = animStep(a, { target: targetAt(t0), from, shift, gapMs, minMs }, k);
    const text = s.tmpl ? fill(s.text) : s.text;
    const hold = Math.max(s.ms, airtimeMs(Math.min(CELLS_MAX, [...text].length), gapMs));
    return { ...s, text, hold, k, t0 };
  };
  let cur = make(0, 0);
  const advance = (until) => {
    if (until < cur.t0) cur = make(0, 0);
    let guard = 0;
    while (until >= cur.t0 + cur.hold && !(finite && cur.done) && guard++ < 100000) cur = make(cur.k + 1, cur.t0 + cur.hold);
    return cur;
  };
  let total = null;
  return {
    finite,
    anim: a,
    at: advance,
    get totalMs() {
      if (!finite) return Infinity;
      if (total === null) {
        let s = make(0, 0);
        for (let i = 0; !s.done && i < 5000; i++) s = make(s.k + 1, s.t0 + s.hold);
        total = s.t0 + s.hold;
      }
      return total;
    },
  };
}

/* ---- for the designer ---- */

const MS = (min, max, step = 10) => ({ key: 'ms', label: 'Time per step', kind: 'range', min, max, step, unit: 'ms' });

/** Types, plain-language descriptions and the few options each one has. */
export const ANIMS = [
  { type: 'decode', name: 'Decode', finite: true, sample: 'HELLO WORLD',
    desc: 'Random characters lock into your text from left to right, Matrix style.',
    options: [MS(40, 400), { key: 'lead', label: 'Scramble first for', kind: 'range', min: 0, max: 12, unit: 'steps' },
      { key: 'order', label: 'Letters lock', kind: 'choice', choices: [['lr', 'Left to right'], ['rl', 'Right to left'], ['random', 'In any order']] },
      { key: 'noise', label: 'Scramble with', kind: 'choice', choices: [['match', 'Same kind'], ['symbols', 'Symbols'], ['mixed', 'Anything']] }] },
  { type: 'typewriter', name: 'Typewriter', finite: true, sample: 'HELLO WORLD',
    desc: 'Your text appears one character at a time.',
    options: [{ ...MS(40, 600), label: 'Time per letter' },
      { key: 'cursor', label: 'Cursor', kind: 'choice', choices: [['#', 'Block #'], ['-', 'Dash -'], ['.', 'Dot .'], ['none', 'None']] }] },
  { type: 'count', name: 'Count', finite: true, sample: 'CPU 42',
    desc: 'Numbers roll to their value instead of jumping. Best when a live value changes.',
    options: [MS(40, 300), { key: 'steps', label: 'Steps to get there', kind: 'range', min: 3, max: 40, unit: 'steps' },
      { key: 'mode', label: 'Numbers', kind: 'choice', choices: [['number', 'Count up or down'], ['digits', 'Roll each digit (odometer)']] }] },
  { type: 'flap', name: 'Split-flap', finite: true, sample: 'DEPARTURES',
    desc: 'Every letter flips through the alphabet and lands on yours, like an airport departure board.',
    options: [{ ...MS(40, 300), label: 'Time per flip' }, { key: 'steps', label: 'Most flips per letter', kind: 'range', min: 3, max: 26, unit: 'flips' },
      { key: 'order', label: 'Letters start', kind: 'choice', choices: [['lr', 'Left to right'], ['rl', 'Right to left'], ['random', 'In any order']] }] },
  { type: 'sparkle', name: 'Sparkle', finite: false, sample: 'HAPPY NEW YEAR',
    desc: 'Stars flash over your text here and there for as long as it shows. With no text, a little night sky.',
    options: [{ ...MS(60, 600), label: 'A new scatter every' }, { key: 'density', label: 'How many stars', kind: 'range', min: 1, max: 12, unit: '' }] },
  { type: 'wave', name: 'Wave', finite: false, sample: 'hello world',
    desc: 'A ripple runs along the text, swapping capitals and small letters as it passes.',
    options: [MS(60, 400), { key: 'crest', label: 'Width of the ripple', kind: 'range', min: 1, max: 6, unit: 'letters' },
      { key: 'lead', label: 'Calm between ripples', kind: 'range', min: 0, max: 30, unit: 'steps' },
      { key: 'order', label: 'It runs', kind: 'choice', choices: [['lr', 'Left to right'], ['rl', 'Right to left']] }] },
  { type: 'bounce', name: 'Bounce', finite: false, sample: 'HELLO',
    desc: 'Still text glides side to side and slows at each end.',
    options: [{ key: 'period', label: 'There and back in', kind: 'range', min: 1000, max: 12000, step: 250, unit: 'ms' },
      { key: 'pause', label: 'Rest at each end', kind: 'range', min: 0, max: 2000, step: 50, unit: 'ms' },
      { key: 'amp', label: 'Distance each side (0 = auto)', kind: 'range', min: 0, max: 30, unit: 'dots' }, MS(60, 400)] },
  { type: 'scanner', name: 'Scanner', finite: false, sample: '<=>',
    desc: 'Your text (keep it short, like * or <=>) sweeps back and forth: an idle, thinking look.',
    options: [MS(60, 400), { key: 'width', label: 'Field width', kind: 'range', min: 4, max: 35, unit: 'characters' },
      { key: 'trail', label: 'Trail', kind: 'choice', choices: [['', 'None'], ['o.', 'o .'], ['-', '-'], ['.', '.']] },
      { key: 'bg', label: 'Background', kind: 'choice', choices: [[' ', 'Blank'], ['.', 'Dots'], ['-', 'Dashes']] }] },
  { type: 'sprites', name: 'Sprites', finite: false, sample: '',
    desc: 'Little cartoons. Pac-Man eats your text (or a row of dots).',
    options: [{ key: 'sprite', label: 'Cartoon', kind: 'choice', choices: [['pacman', 'Pac-Man'], ['chase', 'Arrow chase'], ['pulse', 'Pulse'], ['fish', 'Fish']] },
      MS(60, 600), { key: 'width', label: 'Width', kind: 'range', min: 6, max: 35, unit: 'characters' }] },
  { type: 'automaton', name: 'Automaton', finite: false, sample: '',
    desc: 'A cellular automaton in # and spaces, a new generation each step: a screensaver that wraps the globe.',
    options: [{ key: 'rule', label: 'Rule', kind: 'choice', choices: [[30, '30 chaos'], [90, '90 triangles'], [110, '110 gliders'], [150, '150'], [184, '184 traffic']] },
      { key: 'start', label: 'Start from', kind: 'choice', choices: [['single', 'One cell'], ['random', 'Noise']] },
      { key: 'width', label: 'Width (35 wraps the globe)', kind: 'range', min: 8, max: 35, unit: 'cells' },
      { key: 'gens', label: 'Start again after', kind: 'range', min: 10, max: 250, unit: 'generations' }, MS(250, 2000, 50)] },
  { type: 'sequence', name: 'Sequence', finite: false, sample: '',
    desc: 'Your own frames: a list of texts shown in turn. Live values work in them.',
    options: [{ key: 'texts', label: 'Texts, one per line', kind: 'texts' }, { ...MS(100, 5000, 50), label: 'Time per text' },
      { key: 'loop', label: 'Loop while the frame shows (off: play once, then show the frame text)', kind: 'bool' }] },
];
