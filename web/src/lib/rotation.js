// The rotation: what the globe shows in turn when nothing else is up (the
// playlist on the globe). Every page that has a "Show on the globe" switch
// goes through here, so they all behave the same way.

import * as api from './api.js';
import { weatherScene } from './feeds/weather.js';
import { PLANE_SCENE } from './feeds/aircraft.js';
import { withScene, withSeconds } from './rotationcore.js';

// Scenes the app makes for you the first time they are switched on.
export const BUILT_IN = {
  clock: { name: 'Clock', page: '#/clock', seconds: 30 },
  weather: { name: 'Weather', page: '#/weather', seconds: 30, make: weatherScene },
  plane: { name: 'Planes', page: '#/planes', seconds: 30, make: () => PLANE_SCENE },
};
// Scenes that are never offered in the rotation (the app plays them itself).
export const HIDDEN = ['quick', 'clock-chime'];

export { sceneSeconds, withScene, withSeconds } from './rotationcore.js';

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
