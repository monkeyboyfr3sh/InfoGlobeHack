import { describe, it, expect } from 'vitest';
import { withScene, withSeconds, sceneSeconds } from './rotationcore.js';

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
  it('counts a scene as the globe does', () => {
    expect(sceneSeconds({ frames: [{ seconds: 8 }, { seconds: 0 }, {}] })).toBe(18);
  });
});
