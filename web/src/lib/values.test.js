import { describe, it, expect } from 'vitest';
import doc from '../../../docs/effects.json';
import { fillValues, worstCase } from './values.js';
import { slug, cleanScene } from './scenes.js';
import { makeSequence } from './sim.js';
import { encodeText } from './protocol.js';

const d = new Date(2026, 10, 2, 21, 58, 7, 100);   // Mon 2 Nov 2026, 9:58:07.100 PM
describe('live values (same formats as clock_values.c)', () => {
  it('fills known values, keeps unknown ones', () => {
    expect(fillValues('{clock}', d)).toBe('09:58 PM');
    expect(fillValues('{clock}', new Date(2026, 10, 2, 21, 58, 7, 700))).toBe('09 58 PM');
    expect(fillValues('{time} {date}', d)).toBe('9:58 Mon 02 Nov');   // matched the real globe's /now output
    expect(fillValues('{time24}|{ampm}|{sec}|{day}|{dd}|{month}|{mon}|{year}', d)).toBe('21:58|PM|07|Monday|02|November|Nov|2026');
    expect(fillValues('{ip} {nope}', d, '10.0.0.5')).toBe('10.0.0.5 {nope}');
  });
  it('worst case is at least as long as any real value', () => {
    expect(worstCase('{day}').length).toBeGreaterThanOrEqual('Wednesday'.length);
  });
});

describe('scenes', () => {
  it('slugs names into valid ids', () => {
    expect(slug('Server check!')).toBe('server-check');
    expect(slug('Café ÉTÉ')).toBe('cafe-ete');
    expect(slug('***')).toBe('scene');
  });
  it('cleans imported scenes', () => {
    const s = cleanScene({ name: 'X', frames: [{ text: 'A', entrance: 3, after: 'bogus' }] });
    expect(s.frames[0]).toMatchObject({ text: 'A', entrance: 3, after: 'spin', seconds: 6 });
    expect(() => cleanScene({ name: 'nope' })).toThrow();
  });
  it('sequence plays frames in order and loops', () => {
    const fr = (t, ms) => ({ codes: encodeText(t).codes, style: { entrance: -1, after: 'spin' }, ms });
    const seq = makeSequence([fr('ONE', 3000), fr('TWO', 4000)], doc.effects);
    expect(seq.entranceMs).toBe(7000);
    expect(seq.frameAt(100)).toBe(0);
    expect(seq.frameAt(3500)).toBe(1);
    expect(seq.frameAt(7100)).toBe(0);
  });
});

import { setVars } from './values.js';
describe('pushed values (same rules as live_values.c pushed_value)', () => {
  it('shows pushed values by {var:name} and by bare name', () => {
    setVars({ cpu: '42', print: '125', build_ok: 'PASS' });
    expect(fillValues('CPU {var:cpu} {cpu} {build_ok}', d)).toBe('CPU 42 42 PASS');
    expect(fillValues('{var:nope} {nope}', d)).toBe('-- {nope}');
    expect(fillValues('{bar:cpu}|{pct:cpu}', d)).toBe('####------|42');
    expect(fillValues('{bar:print:250}|{pct:print:250}', d)).toBe('#####-----|50');
    expect(fillValues('{bar:nope}|{pct:nope}', d)).toBe('----------|--');
    expect(fillValues('{bar:day}', d)).toMatch(/^#+-*$/);   // clock periods still work
    setVars({});
  });
});
