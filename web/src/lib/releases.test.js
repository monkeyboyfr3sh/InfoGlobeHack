import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { parseReleases, inline, RELEASES } from './releases.js';

describe('release notes (CHANGELOG.md)', () => {
  it('splits into releases with headings, bullets that run over lines, and paragraphs', () => {
    const r = parseReleases('# Notes\nintro\n\n## 1.1.0 (2027-01-02)\n\nSome words\nmore words.\n\n### New\n- one\n  continued\n- two\n\n## 1.0.0\n- first\n');
    expect(r.map((x) => [x.version, x.date])).toEqual([['1.1.0', '2027-01-02'], ['1.0.0', '']]);
    expect(r[0].blocks).toEqual([{ kind: 'p', text: 'Some words more words.' }, { kind: 'h', text: 'New' },
      { kind: 'li', text: 'one continued' }, { kind: 'li', text: 'two' }]);
  });
  it('bold and code without HTML', () => {
    expect(inline('a **b** c `d`')).toEqual([{ text: 'a ' }, { text: 'b', b: true }, { text: ' c ' }, { text: 'd', code: true }]);
    expect(inline('<b>x</b>')).toEqual([{ text: '<b>x</b>' }]);
  });
  it('the newest release is the version in VERSION', () => {
    const v = readFileSync(new URL('../../../VERSION', import.meta.url), 'utf8').trim();
    expect(RELEASES[0].version).toBe(v);
    expect(v).toMatch(/^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/);
    expect(v.length).toBeLessThan(32);                 // the firmware keeps it in a 32-byte field
  });
});
