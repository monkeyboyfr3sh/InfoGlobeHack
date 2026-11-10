import { describe, it, expect, afterEach } from 'vitest';
import { fillValues, worstCase, setVars, VALUES as ALL } from '../values.js';
import { encodeText, TEXT_MAX } from '../protocol.js';
import { cleanScene } from '../scenes.js';
import { VALUES, SAMPLES, NONE, WORST, PLANE_SCENE } from './aircraft.js';

afterEach(() => setVars({}));

const drawable = (s) => encodeText(s).bad.length === 0;

describe('aircraft values (feed_aircraft.c)', () => {
  it('every value has a valid name, a sample and a worst case', () => {
    for (const v of VALUES) {
      expect(v.name).toMatch(/^[a-z0-9_]{1,15}$/);
      expect(v.group).toBe('Aircraft');
      expect(SAMPLES[v.name], v.name).toBeTypeOf('string');
      expect(WORST[v.name], v.name).toBeTypeOf('string');
    }
    expect(Object.keys(SAMPLES).sort()).toEqual(VALUES.map((v) => v.name).sort());
  });

  it('are in the designer menu', () => {
    expect(ALL.filter((v) => v.group === 'Aircraft').map((v) => v.name)).toEqual(VALUES.map((v) => v.name));
  });

  it('samples render through fillValues, by bare name and by {var:}', () => {
    for (const v of VALUES) {
      expect(fillValues(`{${v.name}}`)).toBe(SAMPLES[v.name]);
      expect(fillValues(`{var:${v.name}}`)).toBe(SAMPLES[v.name]);
    }
    expect(fillValues('{plane} {planeroute} {planetype}')).toBe('UAL123 SFO-ORD B738');
    expect(fillValues('{planeline}')).toBe(fillValues('{plane} {planeroute} {planetype}'));
    expect(fillValues('{bar:planedist:10}')).toBe('##--------');
  });

  it('the globe\'s live values win over the samples', () => {
    setVars({ planeline: 'ASA1386 PVR-SFO B38M' });
    expect(fillValues('{planeline} {plane}')).toBe('ASA1386 PVR-SFO B38M UAL123');
  });

  it('use only glyphs the globe has, and each fits a frame', () => {
    for (const set of [SAMPLES, NONE, WORST]) {
      for (const [k, v] of Object.entries(set)) {
        expect(drawable(v), `${k}: ${v}`).toBe(true);
        expect(v.length, `${k}: ${v}`).toBeLessThanOrEqual(TEXT_MAX);
        expect(v, k).toBe(v.toUpperCase());
      }
    }
  });
});

describe('plane scene', () => {
  it('is a valid scene the globe accepts', () => {
    const s = cleanScene(PLANE_SCENE);
    expect(s.frames).toHaveLength(PLANE_SCENE.frames.length);
    for (const f of PLANE_SCENE.frames) {
      expect(f.text.length).toBeLessThanOrEqual(95);                 // api_v1.c scene_check
      expect(f.entrance).toBeGreaterThanOrEqual(0);
      expect(f.entrance).toBeLessThanOrEqual(37);
      expect(['spin', 'hold', 'flash', 'instant']).toContain(f.after);
      expect(f.seconds).toBeGreaterThan(0);
    }
  });

  it('frames stay within 35 characters with worst-case values', () => {
    setVars(WORST);
    for (const f of PLANE_SCENE.frames) {
      const text = fillValues(f.text);
      expect(text).not.toMatch(/[{}]/);
      expect(encodeText(text).codes.length, text).toBeLessThanOrEqual(TEXT_MAX);
      expect(drawable(text), text).toBe(true);
    }
  });

  it('reads well with nothing in range (no "--" frames)', () => {
    setVars(NONE);
    expect(PLANE_SCENE.frames.map((f) => fillValues(f.text))).toEqual(['NO PLANES', 'QUIET SKIES', 'NONE WITHIN 10 NM']);
  });

  it('fits with the designer\'s own worst-case filler too', () => {
    for (const f of PLANE_SCENE.frames) expect(encodeText(worstCase(f.text)).codes.length).toBeLessThanOrEqual(TEXT_MAX);
  });
});
