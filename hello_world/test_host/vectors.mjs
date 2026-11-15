// Test vectors for the animation module: every case is run through
// web/src/lib/anim.js and written out; anim_host_test.c runs the same cases
// through globe_anim.c and compares. Texts are hex (UTF-8) prefixed with "x".
//
//   A <type> [key=n:<number> | key=s:<hex> | key=b:<0|1> | texts=t:<hex>,<hex>...]
//   I <target> <from or -> <shift> <gap ms> <min ms>
//   S <k> <text> <shift> <blink> <ms> <done> <tmpl>

import { animStep, isFiniteAnim, normalizeAnim } from '../../web/src/lib/anim.js';

const enc = new TextEncoder();
const hex = (s) => `x${[...enc.encode(s)].map((b) => b.toString(16).padStart(2, '0')).join('')}`;

function param(key, v) {
  if (key === 'texts') return `texts=t:${v.filter((t) => typeof t === 'string').map(hex).join(',')}`;   // the engine skips non-strings too
  if (typeof v === 'number') return `${key}=n:${v}`;
  if (typeof v === 'boolean') return `${key}=b:${v ? 1 : 0}`;
  return `${key}=s:${hex(v)}`;
}

const LOOP_KS = [...Array(48).keys(), 100, 257, 1000, 4097, 99991, 123456, 4294967295];
const input = (target, o = {}) => ({ target, from: null, shift: -1, gapMs: 10, minMs: 0, ...o });

const CASES = [
  // finite
  { anim: { type: 'decode' }, inputs: ['HELLO WORLD', '12:34 PM', 'Café ✉ ok', 'X'.repeat(40), '', 'mixed Case 42'] },
  { anim: { type: 'decode', order: 'random', noise: 'mixed', lead: 2, seed: 7 }, inputs: ['HELLO WORLD', 'A B C D E F G H I J K L M N O P Q R'] },
  { anim: { type: 'decode', order: 'rl', noise: 'symbols', ms: 30 }, inputs: ['DOWN 3', '12:05'] },
  { anim: { type: 'decode', ms: 99999, lead: -3, order: 'sideways', seed: -5 }, inputs: ['CLAMP'] },
  { anim: { type: 'typewriter' }, inputs: ['HELLO WORLD', 'é ü', ''] },
  { anim: { type: 'typewriter', cursor: 'none', ms: 45.7 }, inputs: ['TYPE'] },
  { anim: { type: 'typewriter', cursor: '-' }, inputs: ['ABC'] },
  { anim: { type: 'typewriter', cursor: '_' }, inputs: ['BAD CURSOR'] },
  { anim: { type: 'typewriter', cursor: 'ab', ms: true }, inputs: ['TWO'] },
  { anim: { type: 'count' }, inputs: [input('CPU 42'), input('CPU 42', { from: 'CPU 7' }), input('TEMP -3', { from: 'TEMP 5' }),
    input('X 1234567890 7'), input('05', { from: '100' }), input('NO DIGITS'), input('999999999', { from: '0' })] },
  { anim: { type: 'count', mode: 'digits', steps: 5 }, inputs: [input('01:00 PM', { from: '12:59 PM' }), input('2026'), input('9', { from: '123' })] },
  { anim: { type: 'count', steps: 100, ms: 10 }, inputs: [input('1 2 3', { from: '9 8' })] },
  // loops
  { anim: { type: 'bounce' }, inputs: ['HI', 'HELLO', 'HELLO WORLD', 'A LONGER TEXT THAT IS WIDE', input('HI', { shift: 3 }), input('MOVE', { gapMs: 30, minMs: 200 })] },
  { anim: { type: 'bounce', period: 1000, pause: 0, ms: 40 }, inputs: ['ZIP'] },
  { anim: { type: 'bounce', amp: 12, pause: 9000 }, inputs: ['WIDE', input('X', { shift: 250 })] },
  { anim: { type: 'bounce', amp: 70, period: 50000 }, inputs: ['CLAMPED'] },
  { anim: { type: 'scanner' }, inputs: ['<=>', '=>', '', 'HELLO', '(o>'] },
  { anim: { type: 'scanner', trail: 'o.', bg: '.', width: 12 }, inputs: ['O', '=>'] },
  { anim: { type: 'scanner', width: 4, trail: 'toolong', bg: 'xy' }, inputs: ['<<'] },
  { anim: { type: 'scanner', width: 50 }, inputs: ['*', 'Z'.repeat(36)] },
  { anim: { type: 'sprites' }, inputs: ['', 'HELLO', 'A LONG TEXT FOR PAC-MAN TO EAT'] },
  { anim: { type: 'sprites', sprite: 'chase' }, inputs: [''] },
  { anim: { type: 'sprites', sprite: 'chase', width: 3 }, inputs: [''] },
  { anim: { type: 'sprites', sprite: 'pulse' }, inputs: [''] },
  { anim: { type: 'sprites', sprite: 'pulse', width: 9 }, inputs: [''] },
  { anim: { type: 'sprites', sprite: 'fish', width: 35 }, inputs: [''] },
  { anim: { type: 'sprites', sprite: 'fish', width: 3 }, inputs: [''] },
  { anim: { type: 'automaton' }, inputs: ['', 'seeded'] },
  { anim: { type: 'automaton', rule: 90, width: 12 }, inputs: [''] },
  { anim: { type: 'automaton', rule: 110, start: 'random', gens: 10, live: '*', dead: '.' }, inputs: ['', 'other seed'] },
  { anim: { type: 'automaton', rule: 0, width: 8 }, inputs: [''] },
  { anim: { type: 'automaton', rule: 255, start: 'random', seed: 99 }, inputs: [''] },
  { anim: { type: 'automaton', rule: 184, start: 'random', width: 20, live: '%' }, inputs: [''] },
  { anim: { type: 'sequence', texts: ['A', 'BB', '{time}', '( o o )'] }, inputs: ['FRAME TEXT'] },
  { anim: { type: 'sequence', texts: ['ONE', 'TWO', 'THREE'], loop: false, ms: 250 }, inputs: ['DONE'] },
  { anim: { type: 'sequence', texts: [...Array(20).keys()].map((i) => `T${i}`), loop: 0 }, inputs: ['END'] },
  { anim: { type: 'sequence', texts: ['é'.repeat(40), 'x'.repeat(70), 7, 'ok'] }, inputs: [''] },
  { anim: { type: 'sequence', texts: [] }, inputs: ['ONLY'] },
  { anim: { type: 'sequence', loop: false }, inputs: ['NO TEXTS'] },
  { anim: { type: 'flap' }, inputs: ['HELLO WORLD', 'Gate 12 B', '', 'é ok 9', 'X'.repeat(40), input('07:05 PM', { from: '07:04 PM' }),
    input('10:00', { from: '9:59' }), input('NEW', { from: 'OLDER TEXT' }), input('abc', { from: 'ABC' }), input('SAME', { from: 'SAME' })] },
  { anim: { type: 'flap', order: 'random', steps: 26, seed: 3, ms: 40 }, inputs: ['DEPARTURES 14:35'] },
  { anim: { type: 'flap', order: 'rl', steps: 1 }, inputs: ['AB', 'Z9z'] },
  { anim: { type: 'flap', steps: 99, order: 'sideways' }, inputs: ['CLAMPED 60'] },
  { anim: { type: 'sparkle' }, inputs: ['HAPPY NEW YEAR', '', 'é ✉', 'X'.repeat(40)] },
  { anim: { type: 'sparkle', density: 20, width: 12, seed: 9 }, inputs: ['', 'STARS'] },
  { anim: { type: 'sparkle', density: 0, ms: 20 }, inputs: ['CLAMPED'] },
  { anim: { type: 'wave' }, inputs: ['hello world', 'MiXeD 42 é', '', 'A', 'X'.repeat(40)] },
  { anim: { type: 'wave', crest: 5, lead: 0, order: 'rl' }, inputs: ['Right To Left'] },
  { anim: { type: 'wave', crest: 99, lead: 30, order: 'random' }, inputs: ['clamp'] },
  { anim: { type: 'nonsense' }, inputs: ['UNKNOWN TYPE'] },
];

/** The vectors file contents. */
export function buildVectors() {
  const lines = ['# Generated by gen_vectors.mjs from web/src/lib/anim.js; do not edit.'];
  for (const c of CASES) {
    const params = Object.entries(c.anim).filter(([k]) => k !== 'type').map(([k, v]) => param(k, v));
    lines.push(['A', c.anim.type, ...params].join(' '));
    const finite = isFiniteAnim(normalizeAnim(c.anim));
    for (const raw of c.inputs) {
      const inp = typeof raw === 'string' ? input(raw) : raw;
      lines.push(`I ${hex(inp.target)} ${inp.from === null ? '-' : hex(inp.from)} ${inp.shift} ${inp.gapMs} ${inp.minMs}`);
      let ks = LOOP_KS;
      if (finite) {
        ks = [];
        for (let k = 0; k < 200; k++) { ks.push(k); if (animStep(c.anim, inp, k).done) { ks.push(k + 1, k + 7); break; } }
      }
      for (const k of ks) {
        const s = animStep(c.anim, inp, k);
        lines.push(`S ${k} ${hex(s.text)} ${s.shift} ${s.blink} ${s.ms} ${s.done ? 1 : 0} ${s.tmpl ? 1 : 0}`);
      }
    }
  }
  return `${lines.join('\n')}\n`;
}
