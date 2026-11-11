// The rotation's list arithmetic, with no globe access (so it can be tested on its own).

/** Seconds one pass of a scene takes (frames without a time count as 5 s, as on the globe). */
export const sceneSeconds = (scene) => Math.round((scene.frames || []).reduce((a, f) => a + (f.seconds > 0 ? f.seconds : 5), 0)) || 10;

/** Pure: the playlist items after switching one scene on or off. Throws when it would leave nothing. */
export function withScene(items, id, on, seconds = 30) {
  const cur = items && items.length ? items : [{ scene: 'clock', seconds: 0 }];
  const has = cur.some((it) => it.scene === id);
  if (on === has) return cur;
  if (!on) {
    const left = cur.filter((it) => it.scene !== id);
    if (!left.length) throw new Error('The globe needs at least one thing to show. Switch something else on first.');
    return left;
  }
  // with more than one item every item needs a time
  return [...cur.map((it) => ({ ...it, seconds: it.seconds || 30 })), { scene: id, seconds }];
}

/** Pure: the playlist items with one item's time changed. */
export const withSeconds = (items, id, seconds) => items.map((it) => (it.scene === id ? { ...it, seconds: Math.max(1, Math.round(seconds) || 1) } : it));
