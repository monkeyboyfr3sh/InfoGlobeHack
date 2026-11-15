import { describe, it, expect } from 'vitest';
import { withScene, withSeconds, withSwapped, freshId, sceneSeconds, withTimer, withoutTimer, cleanTime, showTime } from './rotationcore.js';

describe('rotation (the playlist behind every "show on the globe" switch)', () => {
  it('adds a scene and gives every item a time', () => {
    expect(withScene([{ scene: 'clock', seconds: 0 }], 'weather', true, 30)).toEqual([{ scene: 'clock', seconds: 30 }, { scene: 'weather', seconds: 30 }]);
    expect(withScene(undefined, 'weather', true, 20)).toEqual([{ scene: 'clock', seconds: 30 }, { scene: 'weather', seconds: 20 }]);
  });
  it('leaves the list alone when nothing changes', () => {
    const items = [{ scene: 'clock', seconds: 0 }];
    expect(withScene(items, 'clock', true)).toBe(items);
    expect(withScene(items, 'weather', false)).toBe(items);
  });
  it('removes a scene but never the last one', () => {
    expect(withScene([{ scene: 'clock', seconds: 30 }, { scene: 'weather', seconds: 30 }], 'clock', false)).toEqual([{ scene: 'weather', seconds: 30 }]);
    expect(() => withScene([{ scene: 'clock', seconds: 0 }], 'clock', false)).toThrow(/at least one/);
  });
  it('changes one time, at least a second', () => {
    expect(withSeconds([{ scene: 'a', seconds: 5 }, { scene: 'b', seconds: 5 }], 'b', 12.4)).toEqual([{ scene: 'a', seconds: 5 }, { scene: 'b', seconds: 12 }]);
    expect(withSeconds([{ scene: 'a', seconds: 5 }], 'a', 0)[0].seconds).toBe(1);
  });
  it('exchanges two places, and leaves the list alone when it cannot', () => {
    const items = [{ scene: 'a', seconds: 5 }, { scene: 'b', seconds: 6 }, { scene: 'c', seconds: 7 }];
    expect(withSwapped(items, 'c', 'a').map((it) => it.scene)).toEqual(['c', 'b', 'a']);
    expect(withSwapped(items, 'a', 'b')[0]).toEqual({ scene: 'b', seconds: 6 });
    expect(withSwapped(items, 'a', 'nope')).toBe(items);
    expect(withSwapped(items, 'a', 'a')).toBe(items);
  });
  it('finds an id nothing else has, within 23 characters', () => {
    expect(freshId('dinner', ['clock'])).toBe('dinner');
    expect(freshId('dinner', ['dinner'])).toBe('dinner-2');
    expect(freshId('dinner', ['dinner', 'dinner-2'])).toBe('dinner-3');
    const long = 'a'.repeat(23);
    expect(freshId(long, [long])).toBe(`${'a'.repeat(21)}-2`);
    expect(freshId(long, [long]).length).toBe(23);
  });
  it('counts a scene as the globe does', () => {
    expect(sceneSeconds({ frames: [{ seconds: 8 }, { seconds: 0 }, {}] })).toBe(18);
  });
});

describe('set times', () => {
  it('reads and shows times', () => {
    expect(cleanTime('7:05')).toBe('07:05');
    expect(cleanTime('24:00')).toBeNull();
    expect(cleanTime('soon')).toBeNull();
    expect(showTime('07:05')).toBe('7:05 AM');
    expect(showTime('00:00')).toBe('12:00 AM');
    expect(showTime('19:30')).toBe('7:30 PM');
  });
  it('adds in time order, replaces the same scene and time, removes', () => {
    let t = withTimer([], { scene: 'b', at: '19:00', days: 'all', seconds: 60 });
    t = withTimer(t, { scene: 'a', at: '7:00', days: 'weekdays', seconds: 30.4 });
    expect(t).toEqual([{ scene: 'a', at: '07:00', days: 'weekdays', seconds: 30 }, { scene: 'b', at: '19:00', days: 'all', seconds: 60 }]);
    t = withTimer(t, { scene: 'a', at: '07:00', days: 'nonsense', seconds: 0 });
    expect(t[0]).toEqual({ scene: 'a', at: '07:00', days: 'all', seconds: 0 });
    expect(t).toHaveLength(2);
    expect(withoutTimer(t, { scene: 'a', at: '07:00' })).toEqual([t[1]]);
    expect(() => withTimer(t, { scene: 'a', at: 'later' })).toThrow(/time/);
    expect(() => withTimer(t, { scene: 'c', at: '08:00' }, 2)).toThrow(/up to 2/);
  });
});
