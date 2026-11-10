import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import doc from '../../../docs/effects.json';
import { ANIMS, animStep, airtimeMs, normalizeAnim, isFiniteAnim, makeAnimPlayer, changeAnimOf, cleanAnim } from './anim.js';
import { encodeText, TEXT_MAX } from './protocol.js';
import { makeScene, makeSequence, stillLayout } from './sim.js';
import { makeClockSim } from './clocksim.js';
import { cleanScene, toStored, TEMPLATES } from './scenes.js';
import { compileClock, DEFAULT_CLOCK } from './clockstyles.js';
import { buildVectors } from '../../../hello_world/test_host/vectors.mjs';

const TARGETS = ['HELLO WORLD', '12:34 PM', 'CPU 42', 'Café ✉ ok', '<=>', '', 'X'.repeat(40), 'a b c 7 8 9 # * . -'];
const VARIANTS = {
  decode: [{}, { order: 'random', noise: 'mixed' }, { order: 'rl', noise: 'symbols', lead: 0 }],
  typewriter: [{}, { cursor: 'none' }, { cursor: '.' }],
  count: [{}, { mode: 'digits', steps: 5 }],
  bounce: [{}, { amp: 20, period: 1000, pause: 0 }],
  scanner: [{}, { trail: 'o.', bg: '.', width: 35 }],
  sprites: [{ sprite: 'pacman' }, { sprite: 'chase' }, { sprite: 'pulse' }, { sprite: 'fish', width: 35 }],
  automaton: [{}, { rule: 110, start: 'random', width: 12 }, { rule: 0 }],
  sequence: [{ texts: ['A', 'BB', '( o o )'] }, { texts: ['ONE', 'TWO'], loop: false }],
};
const LOOP_KS = [...Array(60).keys(), 1000, 123457];
const steps = (anim, input) => {
  const out = [];
  for (let k = 0; k < 400; k++) {
    const s = animStep(anim, input, k);
    out.push(s);
    if (s.done) break;
    if (!isFiniteAnim(anim) && k >= 59) break;
  }
  if (!isFiniteAnim(anim)) for (const k of LOOP_KS.slice(60)) out.push(animStep(anim, input, k));
  return out;
};

describe('custom animations', () => {
  it('cover every type with a designer entry', () => {
    expect(ANIMS.map((a) => a.type).sort()).toEqual(Object.keys(VARIANTS).sort());
    for (const a of ANIMS) expect(isFiniteAnim({ type: a.type, texts: ['A'] }), a.type).toBe(a.finite);
    expect(isFiniteAnim({ type: 'sequence' })).toBe(true);    // no texts: just shows the frame text
  });

  for (const [type, variants] of Object.entries(VARIANTS)) {
    it(`${type}: fits a frame, draws only real glyphs, never outruns the link`, () => {
      for (const v of variants) {
        const anim = { type, ...v };
        for (const target of TARGETS) {
          for (const gapMs of [10, 40]) {
            const input = { target, from: type === 'count' ? 'CPU 7 12:59' : null, gapMs };
            const all = steps(anim, input);
            const finite = isFiniteAnim(anim);
            for (const s of all) {
              const cells = [...s.text].length;
              if (!s.tmpl) expect(cells, `${type} ${JSON.stringify(v)} "${target}"`).toBeLessThanOrEqual(TEXT_MAX);
              const legal = encodeText(target).bad.length === 0;
              if (legal && !s.tmpl) expect(encodeText(s.text).bad, `${type} "${s.text}"`).toEqual([]);
              expect(s.ms).toBeGreaterThanOrEqual(airtimeMs(Math.min(TEXT_MAX, cells), gapMs));
              expect(s.shift).toBeGreaterThanOrEqual(-1);
              expect(s.shift).toBeLessThanOrEqual(255);
            }
            if (finite) {
              const last = all[all.length - 1];
              expect(last.done, `${type} ends`).toBe(true);
              expect(last.text, `${type} ${JSON.stringify(v)} ends on the text`).toBe([...target].slice(0, TEXT_MAX).join(''));
            } else {
              expect(all.some((s) => s.done)).toBe(false);
            }
          }
        }
      }
    });
  }

  it('finite animations change something on the way', () => {
    for (const type of ['decode', 'typewriter', 'count']) {
      const all = steps({ type }, { target: 'ROOM 42' });
      expect(all.length, type).toBeGreaterThan(3);
      expect(all.some((s) => s.text !== 'ROOM 42'), type).toBe(true);
    }
  });

  it('count rolls between the old and the new value, and keeps the length', () => {
    const all = steps({ type: 'count' }, { target: 'CPU 42', from: 'CPU 7' });
    const values = all.map((s) => Number(s.text.slice(4)));
    expect(values[values.length - 1]).toBe(42);
    expect(values.every((v, i) => i === 0 || v >= values[i - 1])).toBe(true);
    expect(values.every((v) => v >= 7 && v <= 42)).toBe(true);
    expect(all.every((s) => s.text.length === 6)).toBe(true);
    const odo = steps({ type: 'count', mode: 'digits' }, { target: '01:00', from: '12:59' });
    expect(odo[odo.length - 1].text).toBe('01:00');
    expect(odo.every((s) => s.text[2] === ':')).toBe(true);
  });

  it('decode keeps spaces and punctuation in place and is repeatable', () => {
    const a = steps({ type: 'decode' }, { target: '12:34 PM' });
    expect(a.every((s) => s.text[2] === ':' && s.text[5] === ' ')).toBe(true);
    expect(steps({ type: 'decode' }, { target: '12:34 PM' })).toEqual(a);
    expect(steps({ type: 'decode', seed: 5 }, { target: '12:34 PM' })).not.toEqual(a);
  });

  it('bounce stays inside the shift range and moves', () => {
    const shifts = steps({ type: 'bounce' }, { target: 'HI' }).map((s) => s.shift);
    expect(Math.min(...shifts)).toBe(0);
    expect(Math.max(...shifts)).toBe(16);                  // padded to 8 cells: the pendulum's proven range
    const wide = steps({ type: 'bounce', amp: 60 }, { target: 'HI', shift: 250 }).map((s) => s.shift);
    expect(Math.max(...wide)).toBeLessThanOrEqual(255);
  });

  it('validates options like the firmware', () => {
    const a = normalizeAnim({ type: 'decode', ms: 5, lead: 99, order: 'sideways', cursor: '_', junk: 1 });
    expect(a.ms).toBe(20);
    expect(a.lead).toBe(30);
    expect(a.order).toBe('lr');
    expect(normalizeAnim({ type: 'typewriter', cursor: '_' }).cursor).toBe('#');
    expect(normalizeAnim({ type: 'nope' })).toBe(null);
    const seq = normalizeAnim({ type: 'sequence', texts: [...Array(30).keys()].map(String).concat(['x']) });
    expect(seq.texts.length).toBe(16);
    expect(normalizeAnim({ type: 'sequence', texts: ['é'.repeat(40)] }).texts[0].length).toBe(31);   // 63 bytes, whole characters
    expect(cleanAnim({ type: 'scanner', width: 9, nonsense: true })).toEqual({ type: 'scanner', width: 9 });
  });

  it('onChange can name a finite animation, with the frame options when the type matches', () => {
    expect(changeAnimOf({ onChange: 'decode' }).type).toBe('decode');
    expect(changeAnimOf({ onChange: 'decode', anim: { type: 'decode', lead: 9 } }).lead).toBe(9);
    expect(changeAnimOf({ onChange: { type: 'count', mode: 'digits' } }).mode).toBe('digits');
    expect(changeAnimOf({ onChange: 'bounce' })).toBe(null);           // loops can't be a change
    expect(changeAnimOf({ onChange: 26 })).toBe(null);
    expect(changeAnimOf({ onChange: 'random' })).toBe(null);
  });

  it('the player waits each step out and settles', () => {
    const p = makeAnimPlayer({ type: 'typewriter', ms: 20 }, { target: 'HELLO' });
    const air = airtimeMs(5);                            // 20 ms asked, the link needs more
    expect(p.totalMs).toBe(6 * air);
    expect(p.at(0).k).toBe(0);
    expect(p.at(air - 1).k).toBe(0);
    expect(p.at(air).k).toBe(1);
    expect(p.at(10 * air).done).toBe(true);
    expect(makeAnimPlayer({ type: 'scanner' }, { target: '*' }).totalMs).toBe(Infinity);
  });

  it('matches the vectors the C host test checks (node hello_world/test_host/run.mjs)', () => {
    const file = readFileSync(new URL('../../../hello_world/test_host/anim_vectors.txt', import.meta.url), 'utf8').replace(/\r\n/g, '\n');
    expect(file === buildVectors(), 'anim_vectors.txt is out of date: run node hello_world/test_host/run.mjs').toBe(true);
  });
});

describe('animations in the simulator', () => {
  const sum = (p) => p.reduce((a, b) => a + b, 0);
  const diff = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0);

  it('a finite animation is the entrance, then the frame settles', () => {
    const codes = encodeText('HELLO').codes;
    const style = { entrance: 5, after: 'hold', blink: 0, shift: -1 };
    const sc = makeScene(null, { codes, style, anim: { type: 'decode' }, text: 'HELLO' }, doc.effects);
    expect(sc.entranceMs).toBe(makeAnimPlayer({ type: 'decode' }, { target: 'HELLO' }).totalMs);
    expect(diff(sc.pictureAt(sc.entranceMs + 10), stillLayout(codes, style).plane)).toBe(0);
    expect(diff(sc.pictureAt(0), stillLayout(codes, style).plane)).toBeGreaterThan(0);
  });

  it('a looping animation plays all along', () => {
    const sc = makeScene(null, { codes: [], style: { entrance: -1, after: 'hold', shift: -1 }, anim: { type: 'sprites', sprite: 'pulse' }, text: '' }, doc.effects);
    expect(sc.loopMs).toBe(Infinity);
    expect(sc.entranceMs).toBe(0);
    expect(diff(sc.pictureAt(0), sc.pictureAt(400))).toBeGreaterThan(0);
    const seq = makeSequence([{ codes: [], style: { entrance: -1, after: 'hold' }, anim: { type: 'automaton' }, text: '', ms: 3000 }], doc.effects);
    expect(sum(seq.pictureAt(2000))).toBeGreaterThan(0);
  });

  it('the decode clock decodes into the new minute', () => {
    const { frames } = compileClock({ ...DEFAULT_CLOCK, style: 'decode' });
    expect(frames[0].onChange).toBe('decode');
    const start = new Date(2026, 0, 1, 9, 59, 58).getTime();
    const sim = makeClockSim(frames, doc.effects, { speed: 1, start });
    sim.pictureAt(0); sim.pictureAt(1000);
    sim.pictureAt(2100);                                // 10:00: the decode starts
    const mid = sim.pictureAt(2300);
    const after = sim.pictureAt(6000);
    expect(sum(after)).toBeGreaterThan(0);
    expect(diff(mid, after)).toBeGreaterThan(0);
  });

  it('every animation template plays and survives a save round trip', () => {
    for (const t of TEMPLATES.filter((x) => x.frames.some((f) => f.anim))) {
      const scene = cleanScene({ name: t.name, frames: t.frames });
      expect(toStored(scene).frames.map((f) => f.anim)).toEqual(t.frames.map((f) => f.anim));
      const seq = makeSequence(scene.frames.map((f) => ({ codes: encodeText(f.text).codes, style: { entrance: -1, after: f.after, shift: -1 }, anim: f.anim, text: f.text, ms: 4000 })), doc.effects);
      for (const ms of [0, 300, 1700, 5000]) expect(seq.pictureAt(ms).every(Number.isFinite), t.key).toBe(true);
    }
  });
});

describe('cleanScene keeps what the engine reads', () => {
  it('Clock studio fields and animations survive open + save', () => {
    const stored = {
      name: 'Clock', clock: { style: 'flip' },
      frames: [{ text: '{time}', entrance: null, after: 'hold', blink: 0, shift: -1, seconds: 0,
        onChange: 26, motion: 'pendulum', motionMs: 300, blinkColon: { period: 2000, look: 'dot' } },
      { text: 'X', after: 'spin', onChange: 'decode', anim: { type: 'decode', lead: 2 } },
      { text: 'Y', after: 'spin', onChange: { type: 'count', mode: 'digits' }, motion: 'wobble', anim: { type: 'bogus' } }],
    };
    const s = toStored(cleanScene(stored));
    expect(s.clock).toEqual({ style: 'flip' });
    expect(s.frames[0]).toMatchObject({ onChange: 26, motion: 'pendulum', motionMs: 300, blinkColon: { period: 2000, look: 'dot' } });
    expect(s.frames[1]).toMatchObject({ onChange: 'decode', anim: { type: 'decode', lead: 2 } });
    expect(s.frames[2].onChange).toEqual({ type: 'count', mode: 'digits' });
    expect(s.frames[2].motion).toBeUndefined();
    expect(s.frames[2].anim).toBeUndefined();
  });

  it('plain frames stay exactly as before', () => {
    const s = toStored(cleanScene({ name: 'A', frames: [{ text: 'HI', entrance: 2, after: 'spin', blink: 0, shift: -1, seconds: 6 }] }));
    expect(s).toEqual({ name: 'A', frames: [{ text: 'HI', entrance: 2, after: 'spin', blink: 0, shift: -1, seconds: 6 }] });
  });
});
