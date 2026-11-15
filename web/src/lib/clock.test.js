import { describe, it, expect } from 'vitest';
import doc from '../../../docs/effects.json';
import { fillValues } from './values.js';
import { compileClock, DEFAULT_CLOCK, STYLES } from './clockstyles.js';
import { makeClockSim } from './clocksim.js';

// Values the real globe produced at 22:38 local on Mon 2 Nov 2026 (see the API check).
const d = new Date(2026, 10, 2, 22, 38, 55, 100);
describe('clock values match the globe', () => {
  it('fun formats', () => {
    expect(fillValues('{fuzzy}', d)).toBe('TWENTY TO ELEVEN');
    expect(fillValues('{words}', d)).toBe('TEN THIRTY EIGHT');
    expect(fillValues('{roman}', d)).toBe('X:XXXVIII');
    expect(fillValues('{binary}', d)).toBe('10110:100110');
    expect(fillValues('{clock:24} {clock:sx}', d)).toBe('22:38 10:38:55');
    expect(fillValues('{until:12-25}', d)).toBe('53');
    expect(fillValues('{doy} {week}', d)).toBe('306 45');
  });
  it('clock options: no AM/PM, colon blink rates', () => {
    expect(fillValues('{clock:x}', new Date(2026, 0, 1, 9, 5, 0, 100))).toBe('09:05');
    expect(fillValues('{clock:s}', new Date(2026, 0, 1, 21, 5, 7, 100))).toBe('09:05:07 PM');
    const off = new Date(2026, 0, 1, 9, 5, 1, 0);   // second half of a 2 s cycle
    expect(fillValues('{time}', off, '', false, { period: 2000, look: 'dot' })).toBe('9.05');
    expect(fillValues('{time}', off, '', false, { period: 0 })).toBe('9:05');
    expect(fillValues('{time}', off, '', true, { period: 2000 })).toBe('9:05');
  });
  it('fuzzy edge cases', () => {
    expect(fillValues('{fuzzy}', new Date(2026, 0, 1, 11, 58))).toBe('NOON');
    expect(fillValues('{fuzzy}', new Date(2026, 0, 1, 9, 13))).toBe('QUARTER PAST NINE');
    expect(fillValues('{words}', new Date(2026, 0, 1, 9, 5))).toBe('NINE OH FIVE');
    expect(fillValues('{roman}', new Date(2026, 0, 1, 9, 0))).toBe('IX:N');
  });
});

describe('clock studio', () => {
  it('every style compiles to text that fits', () => {
    for (const s of STYLES) {
      const { frames } = compileClock({ ...DEFAULT_CLOCK, style: s.key });
      for (const f of frames) expect(fillValues(f.text, d).length, `${s.key}: ${f.text}`).toBeLessThanOrEqual(35);
    }
  });
  it('flip clock plays a transition when the minute changes', () => {
    const { frames } = compileClock({ ...DEFAULT_CLOCK, style: 'flip' });
    expect(frames[0].onChange).toBe(26);
    const start = new Date(2026, 0, 1, 9, 59, 58).getTime();
    const sim = makeClockSim(frames, doc.effects, { speed: 1, start });
    const sum = (p) => p.reduce((a, b) => a + b, 0);
    sim.pictureAt(0); sim.pictureAt(1000);
    const before = sim.pictureAt(1900);
    sim.pictureAt(2100);                       // 10:00 -> change transition starts
    const mid = sim.pictureAt(3200);           // mid drop & replace
    const after = sim.pictureAt(7000);
    expect(sum(before)).toBeGreaterThan(0);
    expect(sum(after)).toBeGreaterThan(0);
    expect(mid).not.toEqual(after);
  });
});
