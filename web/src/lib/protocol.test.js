import { describe, it, expect } from 'vitest';
import { encodeText, compileFrame, hex } from './protocol.js';

// Expected bytes are what the firmware's POST /api/v1/frame returned on the real globe.
describe('protocol matches firmware', () => {
  it('entrance + hold with blink', () => {
    const { codes } = encodeText('SERVER OK');
    const f = compileFrame(codes, { entrance: 2, after: 'hold', blink: 6, shift: -1 });
    expect(hex(f.first)).toBe('00 00 53 45 52 56 45 52 20 4F 4B 02');
    expect(hex(f.then)).toBe('05 00 53 45 52 56 45 52 20 4F 4B 00 07 09');
  });
  it('accents and envelope', () => {
    const { codes, bad } = encodeText('Café ✉ Ça');
    expect(hex(compileFrame(codes, { entrance: 2, after: 'spin' }).first)).toBe('00 00 43 61 66 06 20 80 20 15 61 02');
    expect(bad).toEqual([]);
  });
  it('flash, plain spin, unsupported characters', () => {
    expect(hex(compileFrame(encodeText('ALERT').codes, { entrance: -1, after: 'flash', shift: -1 }).first)).toBe('05 00 41 4C 45 52 54 00 06 05');
    const e = encodeText('50% + ok');
    expect(hex(compileFrame(e.codes, { entrance: -1, after: 'spin' }).first)).toBe('04 00 35 30 20 20 20 20 6F 6B 00');
    expect(e.bad.map((b) => b.ch)).toEqual(['%', '+']);
  });
  it('rejects text over 35 characters', () => {
    expect(() => compileFrame(encodeText('x'.repeat(36)).codes, { entrance: -1, after: 'spin' })).toThrow();
  });
});
