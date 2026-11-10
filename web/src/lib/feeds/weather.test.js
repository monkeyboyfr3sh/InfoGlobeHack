import { describe, it, expect, afterEach, vi } from 'vitest';
import { VALUES, SAMPLES, WORST, weatherScene, searchPlaces, ago } from './weather.js';
import { fillValues, setVars } from '../values.js';
import { encodeText, TEXT_MAX } from '../protocol.js';
import { compileClock, DEFAULT_CLOCK } from '../clockstyles.js';

const d = new Date(2026, 8, 30, 23, 37, 59);   // the same wide date worstCase() uses
afterEach(() => { setVars({}); vi.unstubAllGlobals(); });

describe('weather values (feed_weather.c)', () => {
  it('every menu value has a sample and a worst case, and is a valid pushed-value name', () => {
    for (const v of VALUES) {
      expect(v.group).toBe('Weather');
      expect(v.name).toMatch(/^[a-z0-9_]{1,15}$/);
      expect(SAMPLES[v.name]).toBeDefined();
      expect(WORST[v.name]).toBeDefined();
    }
  });
  it('samples render through fillValues (no built-in value shadows a weather name)', () => {
    expect(fillValues('{temp} {sky}', d)).toBe('61 CLOUDY');
    for (const v of VALUES) expect(fillValues(`{${v.name}}`, d)).toBe(SAMPLES[v.name]);
    setVars({ temp: '45', sky: 'CLEAR' });                 // what the globe holds wins over the samples
    expect(fillValues('{temp} {sky} {hi}', d)).toBe(`45 CLEAR ${SAMPLES.hi}`);
  });
  it('samples and worst cases only use glyphs the globe has', () => {
    for (const s of [...Object.values(SAMPLES), ...Object.values(WORST)]) {
      expect(encodeText(s).bad).toEqual([]);
      expect(s).toBe(s.toUpperCase());
      expect(s.length).toBeLessThanOrEqual(47);
    }
  });
});

describe('ready-made weather scene', () => {
  const scene = weatherScene();
  it('stays within 35 characters with worst-case values, and draws every character', () => {
    setVars(WORST);
    for (const f of scene.frames) {
      const text = fillValues(f.text, d, '192.168.100.100', true);
      expect(text).not.toMatch(/[{}]/);                   // every value is known
      const enc = encodeText(text);
      expect(enc.bad).toEqual([]);
      expect(enc.codes.length).toBeLessThanOrEqual(TEXT_MAX);
    }
  });
  it('held frames fit the front window with typical values', () => {
    for (const f of scene.frames.filter((x) => x.after === 'hold')) expect(fillValues(f.text, d).length).toBeLessThanOrEqual(12);
  });
  it('uses {var:x}, so it shows -- (not the value names) when the globe has no weather', () => {
    for (const f of scene.frames) {
      const refs = f.text.match(/\{[^}]*\}/g);
      expect(refs.length).toBeGreaterThan(0);
      for (const r of refs) expect(r).toMatch(/^\{var:[a-z0-9_]+\}$/);
    }
    expect(fillValues('HI {var:nothing_here}', d)).toBe('HI --');
  });
  it('is a valid stored scene', () => {
    expect(scene.name).toBe('Weather');
    expect(scene.frames.length).toBeGreaterThanOrEqual(2);
    for (const f of scene.frames) {
      expect(['spin', 'hold']).toContain(f.after);
      expect(f.entrance).toBeGreaterThanOrEqual(0);
      expect(f.entrance).toBeLessThanOrEqual(37);
      expect(f.seconds).toBeGreaterThan(0);
    }
  });
});

describe('clock extra', () => {
  it('alternates the time with the weather', () => {
    const { frames } = compileClock({ ...DEFAULT_CLOCK, extras: ['weather'] });
    expect(frames.map((f) => f.text)).toEqual(['{clock}', '{var:weather}']);
    expect(fillValues(frames[1].text, d)).toBe('61 CLOUDY');
  });
});

describe('helpers', () => {
  it('names places from the geocoding answer', async () => {
    const results = [
      { name: 'Chicago', admin1: 'Illinois', country: 'United States', latitude: 41.85003, longitude: -87.65005 },
      { name: 'Singapore', admin1: 'Singapore', country: 'Singapore', latitude: 1.28967, longitude: 103.85007 },
    ];
    const fetch = vi.fn(async () => ({ ok: true, json: async () => ({ results }) }));
    vi.stubGlobal('fetch', fetch);
    expect(await searchPlaces('chicago')).toEqual([
      { name: 'Chicago, Illinois, United States', lat: 41.85, lon: -87.65 },
      { name: 'Singapore', lat: 1.29, lon: 103.85 },
    ]);
    expect(fetch.mock.calls[0][0]).toContain('name=chicago');
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, json: async () => ({}) })));   // nothing found
    expect(await searchPlaces('xyzzy')).toEqual([]);
  });
  it('says how long ago', () => {
    const now = 1_800_000_000_000;
    expect(ago(now / 1000 - 30, now)).toBe('just now');
    expect(ago(now / 1000 - 600, now)).toBe('10 min ago');
    expect(ago(now / 1000 - 3 * 3600, now)).toBe('3 h ago');
    expect(ago('nope', now)).toBe('');
  });
});
