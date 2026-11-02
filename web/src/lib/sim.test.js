import { describe, it, expect } from 'vitest';
import effectsDoc from '../../../docs/effects.json';
import { encodeText } from './protocol.js';
import { tiledLayout, effectPicture, makeScene, orderRanks, BAND, ROWS } from './sim.js';

const fx = effectsDoc.effects;
const oldL = tiledLayout(encodeText('12345678').codes);
const newL = tiledLayout(encodeText('AB:CDE').codes);
const diff = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0);

describe('simulator', () => {
  it('every effect starts from something and ends on the new text', () => {
    for (const id of Object.keys(fx)) {
      const end = effectPicture(fx[id], oldL, newL, 1);
      expect(end.length).toBe(ROWS * BAND);
      expect(diff(end, newL.plane), `FX ${id} end`).toBeLessThan(1e-6);
      for (const u of [0.1, 0.3, 0.5, 0.7, 0.9]) {
        const p = effectPicture(fx[id], oldL, newL, u);
        expect(p.every(Number.isFinite), `FX ${id} at ${u}`).toBe(true);
      }
    }
  });
  it('effects actually animate (mid-point differs from both ends)', () => {
    for (const id of Object.keys(fx)) {
      const mid = effectPicture(fx[id], oldL, newL, 0.5);
      expect(diff(mid, newL.plane) + diff(mid, oldL.plane), `FX ${id}`).toBeGreaterThan(0);
    }
  });
  it('scramble order matches the camera (2,4,7,6,5,8,3,1)', () => {
    const { rank } = orderRanks('scramble', 8);
    const seq = [...Array(8).keys()].sort((a, b) => rank[a] - rank[b]).map((i) => i + 1);
    expect(seq).toEqual([2, 4, 7, 6, 5, 8, 3, 1]);
  });
  it('blink turns off only the first N characters', () => {
    const s = makeScene(null, { codes: encodeText('DOWN NAS').codes, style: { entrance: -1, after: 'hold', blink: 4, shift: -1 } }, fx);
    const on = s.pictureAt(100), off = s.pictureAt(600);
    expect(off.reduce((a, b) => a + b, 0)).toBeLessThan(on.reduce((a, b) => a + b, 0));
    expect(off.reduce((a, b) => a + b, 0)).toBeGreaterThan(0);
  });
});
