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

/** Pure: the playlist items with two scenes' places exchanged (the order they take their turns in). */
export function withSwapped(items, a, b) {
  const i = items.findIndex((it) => it.scene === a), j = items.findIndex((it) => it.scene === b);
  if (i < 0 || j < 0 || i === j) return items;
  const out = [...items];
  [out[i], out[j]] = [out[j], out[i]];
  return out;
}

/** Pure: an id no scene has yet: "dinner", then "dinner-2", "dinner-3" (ids are at most 23 long). */
export function freshId(base, taken) {
  let id = base;
  for (let n = 2; taken.includes(id); n++) id = `${base.slice(0, 23 - String(n).length - 1)}-${n}`;
  return id;
}

export const DAYS = [['all', 'Every day'], ['weekdays', 'Weekdays'], ['weekends', 'Weekends']];

/** "07:05" from whatever a time box gives; null when it isn't a time. */
export function cleanTime(text) {
  const m = String(text || '').trim().match(/^(\d{1,2}):(\d{2})$/);
  if (!m || +m[1] > 23 || +m[2] > 59) return null;
  return `${String(+m[1]).padStart(2, '0')}:${m[2]}`;
}

/** "7:05 AM" for people. */
export function showTime(at) {
  const t = cleanTime(at);
  if (!t) return at;
  const h = +t.slice(0, 2);
  return `${h % 12 || 12}:${t.slice(3)} ${h < 12 ? 'AM' : 'PM'}`;
}

/** Pure: the timers with one added (same scene and time replaces), earliest first. Throws on a bad time or a full list. */
export function withTimer(timers, timer, max = 12) {
  const at = cleanTime(timer.at);
  if (!at) throw new Error('Pick a time first');
  const rest = (timers || []).filter((t) => !(t.scene === timer.scene && t.at === at));
  if (rest.length >= max) throw new Error(`The globe keeps up to ${max} set times. Remove one first.`);
  const days = DAYS.some(([d]) => d === timer.days) ? timer.days : 'all';
  return [...rest, { scene: timer.scene, at, days, seconds: Math.max(0, Math.round(timer.seconds) || 0) }].sort((a, b) => a.at.localeCompare(b.at));
}

/** Pure: the timers without one. */
export const withoutTimer = (timers, timer) => (timers || []).filter((t) => !(t.scene === timer.scene && t.at === timer.at));
