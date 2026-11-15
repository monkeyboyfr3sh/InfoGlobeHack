// The rotation: what the globe shows in turn when nothing else is up (the
// playlist on the globe). Every page that has a "Show on the globe" switch
// goes through here, so they all behave the same way.

import * as api from './api.js';
import { weatherScene } from './feeds/weather.js';
import { PLANE_SCENE } from './feeds/aircraft.js';
import { withScene, withSeconds, withSwapped, withTimer, withoutTimer } from './rotationcore.js';

// Scenes the app makes for you the first time they are switched on.
export const BUILT_IN = {
  clock: { name: 'Clock', page: '#/clock', seconds: 30 },
  weather: { name: 'Weather', page: '#/settings/weather', seconds: 30, make: weatherScene },
  plane: { name: 'Planes', page: '#/settings/planes', seconds: 30, make: () => PLANE_SCENE },
};
// Scenes that are never offered in the rotation (the app plays them itself).
export const HIDDEN = ['quick', 'clock-chime', 'timer', 'timer-done'];

export { sceneSeconds, withScene, withSeconds, withSwapped, freshId, DAYS, showTime, cleanTime } from './rotationcore.js';

/** {scenes: [{id,name,frames}], items: [{scene,seconds}]} as the globe has them. */
export async function loadRotation() {
  const [scenes, p] = await Promise.all([api.getScenes(), api.getPlaylist()]);
  return { scenes: scenes.filter((s) => !HIDDEN.includes(s.id)), items: p.items && p.items.length ? p.items : [{ scene: 'clock', seconds: 0 }] };
}

/** Make a built-in scene if the globe doesn't have it yet. */
export async function ensureScene(id) {
  const b = BUILT_IN[id];
  if (!b || !b.make) return;
  const scenes = await api.getScenes();
  if (!scenes.some((s) => s.id === id)) await api.saveScene(id, b.make());
}

/** Switch a scene in or out of the rotation; returns the new items. */
export async function setInRotation(id, on, seconds) {
  if (on) await ensureScene(id);
  const p = await api.getPlaylist();
  const b = BUILT_IN[id];
  const items = withScene(p.items, id, on, seconds || (b && b.seconds) || 30);
  await api.savePlaylist(items);
  return items;
}

export async function setRotationSeconds(id, seconds) {
  const p = await api.getPlaylist();
  const items = withSeconds(p.items || [], id, seconds);
  await api.savePlaylist(items);
  return items;
}

/** Exchange two scenes' places in the rotation; returns the new items. */
export async function swapInRotation(a, b) {
  const p = await api.getPlaylist();
  const items = withSwapped(p.items || [], a, b);
  await api.savePlaylist(items);
  return items;
}

/** A scene of your own keeps the display for one pass of its messages: after an
 *  edit, bring its turn in line. Returns the items (as they were when it has no turn). */
export async function fitRotationSeconds(id, seconds) {
  const p = await api.getPlaylist();
  const items = p.items || [];
  const it = items.find((x) => x.scene === id);
  if (!it || it.seconds === seconds) return items;
  const next = withSeconds(items, id, seconds);
  await api.savePlaylist(next);
  return next;
}

/** Delete a scene for good: out of the rotation and its set times first, then the scene.
 *  Returns what is left: {items, timers}. */
export async function deleteScene(id) {
  const p = await api.getPlaylist();
  const had = p.items && p.items.length ? p.items : [{ scene: 'clock', seconds: 0 }];
  const left = had.filter((it) => it.scene !== id);
  const items = left.length ? left : [{ scene: 'clock', seconds: 0 }];     // the globe always has something to show
  const timers = (p.timers || []).filter((t) => t.scene !== id);
  if (left.length !== had.length || timers.length !== (p.timers || []).length) await api.savePlaylistRaw({ ...p, items, timers });
  await api.deleteScene(id);
  return { items, timers };
}

/** Set times: [{scene, at: "07:00", days: "all|weekdays|weekends", seconds}]. They live in the playlist file on the globe. */
export async function getTimers() {
  return (await api.getPlaylist()).timers || [];
}
async function saveTimers(change) {
  const p = await api.getPlaylist();
  const timers = change(p.timers || []);
  await api.savePlaylistRaw({ ...p, items: p.items && p.items.length ? p.items : [{ scene: 'clock', seconds: 0 }], timers });
  return timers;
}
export const addTimer = (timer) => saveTimers((t) => withTimer(t, timer));
export const removeTimer = (timer) => saveTimers((t) => withoutTimer(t, timer));
