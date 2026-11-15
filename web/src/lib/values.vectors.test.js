// The globe (C: hello_world/main/src/clock_values.c + live_values.c) and the
// preview (values.js) must show the same text for the same instant.
//
//  1. Golden vectors (values.vectors.json): hand-checked expected text, run
//     against values.js and against the C code compiled for this PC.
//  2. Sweep: generated instants in ten zones (2020-2031, every local DST
//     change), every template feature, random colon settings, locations and
//     pushed values; the C output must equal values.js.
//  3. World clock: every {tz:...} city at each DST transition 2024-2030 found
//     from Intl, C against values.js (which uses Intl).
//
// The C side needs a compiler: $CC, gcc/cc on PATH, or MSYS2's gcc in
// C:\msys64. Without one (or with SKIP_C_CHECK=1) those tests are skipped
// and say so.
// The C program gets the local UTC offset from Node's tz database, because on
// the globe it comes from newlib's TZ handling, which a PC C library can't
// stand in for; everything after that is the globe's own code.

import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, resolve, isAbsolute, delimiter } from 'node:path';
import { fileURLToPath } from 'node:url';
import doc from './values.vectors.json';
import { fillValues, setLocation, setVars, CITIES } from './values.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const FW = resolve(HERE, '../../../hello_world');
const IP = '10.0.0.5';
const TZ0 = process.env.TZ;

function useZone(z) { if (process.env.TZ !== z) process.env.TZ = z; }
afterAll(() => { if (TZ0 === undefined) delete process.env.TZ; else process.env.TZ = TZ0; setVars({}); setLocation(null); });

// ---- cases ----

function instant(v, zone) {
  if (v.at) return Date.parse(v.at);
  const m = /^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)(?:\.(\d{3}))?$/.exec(v.local);
  if (!m) throw new Error(`bad local time ${v.local}`);
  useZone(zone);
  const [y, mo, d, h, mi, s, ms] = m.slice(1).map((x) => +(x || 0));
  const t = new Date(y, mo - 1, d, h, mi, s, ms);
  if (t.getHours() !== h || t.getDate() !== d) throw new Error(`${v.local} does not exist in ${zone}; use "at"`);
  return t.getTime();
}

const golden = [];
for (const g of doc.groups) {
  for (const v of g.vectors) {
    const c = { ...g, ...v, vars: v.vars || g.vars, zone: v.zone || g.zone || doc.zone, group: g.name };
    c.ms = instant(c, c.zone);
    c.id = `g${golden.length}`;
    golden.push(c);
  }
}

function runJs(c) {
  useZone(c.zone);
  setLocation(c.loc ? { lat: c.loc[0], lon: c.loc[1] } : null);
  setVars(c.vars || {});
  return fillValues(c.t, new Date(c.ms), IP, !!c.steady, c.colon || null);
}

const describeCase = (c) => `${c.t} @ ${new Date(c.ms).toISOString()} ${c.zone}${c.loc ? ` loc ${c.loc}` : ''}${c.colon ? ` colon ${JSON.stringify(c.colon)}` : ''}${c.steady ? ' steady' : ''}${c.vars ? ` vars ${JSON.stringify(c.vars)}` : ''}`;

// Small deterministic PRNG so the sweep is the same on every run.
function rng(seed) {
  return () => { seed = (seed + 0x6d2b79f5) | 0; let t = Math.imul(seed ^ (seed >>> 15), 1 | seed); t ^= t + Math.imul(t ^ (t >>> 7), 61 | t); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

const TEMPLATES = [
  '{clock}|{clock:24}|{clock:s}|{clock:x}|{clock:n}|{clock:24s}|{time}|{time24}|{ampm}|{sec}',
  '{date}|{day}|{dd}|{month}|{mon}|{year}|{doy}|{week}|{unix}|{beat}|{decimal}',
  '{fuzzy}|{words}|{roman}|{binary}',
  '{bar:min}{pct:min}|{bar:hour}{pct:hour}|{bar:day}{pct:day}|{bar:week}{pct:week}|{bar:month}{pct:month}|{bar:year}{pct:year}|{bar}{pct}',
  '{moon}|{moonpct}|{sunrise}|{sunset}',
  '{until:12-25}|{until:01-01}|{until:2-29}|{until:7-4}|{until:17:00}|{until:0:00}|{until:9:30}',
  '{countdown:12-25}|{countdown:01-01}|{countdown:2-29}|{countdown:7-4}|{countdown:17:00}|{countdown:0:00}|{countdown:9:30}|{countdown}|{countdown:soon}',
  '{var:cpu}|{cpu}|{bar:cpu}|{pct:cpu}|{bar:print:250}|{pct:print:250}|{var:nope}|{nope}|{bar:nope}|{pct:nope}',
  Object.keys(CITIES).map((k) => `{tz:${k}}`).join('|'),
];
const ZONES = ['America/Chicago', 'America/New_York', 'America/Los_Angeles', 'Europe/London', 'Europe/Berlin',
  'Australia/Sydney', 'Pacific/Auckland', 'Asia/Kolkata', 'America/St_Johns', 'UTC'];
const PUSHED = ['42', '0', '100', '-5', '250', '99.99', '33.3', 'abc', '', ' 7', '1e2', '0x10', 'inf', '12abc'];
const T0 = Date.UTC(2020, 0, 1), T1 = Date.UTC(2031, 0, 1);

// Instants where a zone's UTC offset changes, to the second (scan daily, then bisect).
function transitions(offsetAt, from, to) {
  const out = [];
  let prev = offsetAt(from);
  for (let t = from + 864e5; t < to; t += 864e5) {
    const o = offsetAt(t);
    if (o === prev) continue;
    let lo = t - 864e5, hi = t;
    while (hi - lo > 1000) { const mid = Math.floor((lo + hi) / 2000) * 1000; if (offsetAt(mid) === prev) lo = mid; else hi = mid; }
    out.push(hi);
    prev = o;
  }
  return out;
}
const localOffset = (zone) => (t) => { useZone(zone); return new Date(t).getTimezoneOffset(); };
const AROUND = [-3600e3, -61e3, -60e3, -1e3, -1, 0, 1, 1e3, 59e3, 60e3, 3600e3];

const sweep = [];
{
  const r = rng(20261109);
  const pick = (a) => a[Math.floor(r() * a.length)];
  for (const zone of ZONES) {
    const instants = [];
    for (let i = 0; i < 150; i++) instants.push(T0 + Math.floor(r() * (T1 - T0)));
    for (const t of transitions(localOffset(zone), Date.UTC(2024, 0, 1), Date.UTC(2029, 0, 1))) for (const d of AROUND) instants.push(t + d);
    for (const ms of instants) {
      const colon = r() < 0.5 ? null : { period: pick([0, 100, 250, 500, 1000, 1001, 1500, 2000, 3000]), look: pick(['dot', 'hide']) };
      const loc = r() < 0.15 ? null : [Math.round((r() * 178 - 89) * 1e4) / 1e4, Math.round((r() * 360 - 180) * 1e4) / 1e4];
      const vars = { cpu: pick(PUSHED), print: pick(PUSHED) };
      for (const t of TEMPLATES) sweep.push({ id: `s${sweep.length}`, zone, ms, t, colon, loc, vars, steady: r() < 0.2 });
    }
  }
}

const fmt = {};
const cityOffset = (zone) => (t) => {
  const f = fmt[zone] ??= new Intl.DateTimeFormat('en-US', { timeZone: zone, hourCycle: 'h23', year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric', second: 'numeric' });
  const p = Object.fromEntries(f.formatToParts(new Date(t)).map((x) => [x.type, x.value]));
  return Date.UTC(+p.year, +p.month - 1, +p.day, +p.hour % 24, +p.minute, +p.second) - Math.floor(t / 1000) * 1000;
};
const world = [];
let worldTransitions = 0;
for (const [key, [, zone]] of Object.entries(CITIES)) {
  for (const t of transitions(cityOffset(zone), Date.UTC(2024, 0, 1), Date.UTC(2031, 0, 1))) {
    worldTransitions++;
    for (const d of AROUND) world.push({ id: `w${world.length}`, zone: 'UTC', ms: t + d, t: `{tz:${key}}` });
  }
}

// ---- the C side ----

function findCompiler() {
  if (process.env.SKIP_C_CHECK) return null;
  const tries = [process.env.CC, 'gcc', 'cc', 'C:\\msys64\\ucrt64\\bin\\gcc.exe', 'C:\\msys64\\mingw64\\bin\\gcc.exe', 'clang'].filter(Boolean);
  for (const cc of tries) {
    if (isAbsolute(cc) && !existsSync(cc)) continue;
    // MSYS2's compiler needs its own bin folder on PATH to find its DLLs.
    const pathKey = Object.keys(process.env).find((k) => k.toUpperCase() === 'PATH') || 'PATH';
    const env = isAbsolute(cc) ? { ...process.env, [pathKey]: dirname(cc) + delimiter + process.env[pathKey] } : process.env;
    try { execFileSync(cc, ['--version'], { stdio: 'ignore', env }); return { cc, env }; } catch { /* try the next one */ }
  }
  return null;
}

const compiler = findCompiler();
if (!compiler) {
  console.warn(`values.vectors.test.js: ${process.env.SKIP_C_CHECK ? 'SKIP_C_CHECK is set' : 'no C compiler found (set CC, or put gcc on PATH)'}; `
    + 'the C side of the live-values check is SKIPPED. Only values.js is checked.');
}

const line = (c, offset) => [c.id, c.ms, offset, c.loc ? c.loc[0] : '', c.loc ? c.loc[1] : '', c.colon ? (c.colon.period ?? 0) : '',
  c.colon?.look === 'dot' ? 'dot' : 'hide', c.steady ? 1 : 0, Object.entries(c.vars || {}).map(([k, v]) => `${k}=${v}`).join('\x1f'), c.t].join('\t');

function runC(cases) {
  const dir = mkdtempSync(join(tmpdir(), 'live-values-'));
  try {
    const exe = join(dir, 'live_values_host.exe');
    const main = join(FW, 'main');
    try {
      execFileSync(compiler.cc, ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare',
        '-I', join(main, 'inc'), join(FW, 'host_test', 'live_values_host.c'), join(main, 'src', 'clock_values.c'),
        join(main, 'src', 'live_values.c'), '-o', exe, '-lm'], { env: compiler.env, stdio: 'pipe' });
    } catch (e) {
      throw new Error(`compiling the globe's live values failed:\n${e.stderr || e.message}`);
    }
    const input = cases.map((c) => { useZone(c.zone); return line(c, -new Date(c.ms).getTimezoneOffset() * 60); }).join('\n') + '\n';
    const out = execFileSync(exe, [], { input, encoding: 'utf8', maxBuffer: 1 << 28 });
    const byId = {};
    for (const l of out.split(/\r?\n/)) { const tab = l.indexOf('\t'); if (tab > 0) byId[l.slice(0, tab)] = l.slice(tab + 1); }
    return byId;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

// ---- tests ----

describe('live values: golden vectors (values.vectors.json)', () => {
  it(`values.js gives the expected text (${golden.length} vectors)`, () => {
    const bad = golden.map((c) => [c, runJs(c)]).filter(([c, got]) => got !== c.expect)
      .map(([c, got]) => `${c.group}: ${describeCase(c)}\n    got  ${JSON.stringify(got)}\n    want ${JSON.stringify(c.expect)}`);
    expect(bad).toEqual([]);
  });
});

describe.skipIf(!compiler)(compiler ? `live values: the globe's C code (built with ${compiler.cc})` : 'live values: the globe\'s C code: SKIPPED (no C compiler, or SKIP_C_CHECK set)', () => {
  let c;
  beforeAll(() => { c = runC([...golden, ...sweep, ...world]); }, 60000);

  it(`C gives the expected text (${golden.length} vectors)`, () => {
    const bad = golden.filter((v) => c[v.id] !== v.expect)
      .map((v) => `${v.group}: ${describeCase(v)}\n    got  ${JSON.stringify(c[v.id])}\n    want ${JSON.stringify(v.expect)}`);
    expect(bad).toEqual([]);
  });

  it(`C matches values.js on the sweep (${sweep.length} cases, ${ZONES.length} zones)`, () => {
    const bad = [];
    for (const v of sweep) {
      const js = runJs(v);
      if (c[v.id] !== js) bad.push(`${describeCase(v)}\n    C  ${JSON.stringify(c[v.id])}\n    JS ${JSON.stringify(js)}`);
    }
    expect({ mismatches: bad.length, first: bad.slice(0, 20) }).toEqual({ mismatches: 0, first: [] });
  });

  it(`world clock: C matches Intl around every DST change 2024-2030 (${worldTransitions} transitions, ${world.length} cases)`, () => {
    expect(worldTransitions).toBeGreaterThan(80);
    const bad = world.map((v) => [v, runJs(v)]).filter(([v, js]) => c[v.id] !== js)
      .map(([v, js]) => `${v.t} @ ${new Date(v.ms).toISOString()}: C ${c[v.id]} Intl ${js}`);
    expect(bad).toEqual([]);
  });
});
