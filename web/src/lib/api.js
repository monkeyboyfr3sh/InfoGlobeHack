// Talks to the globe. Served from the globe itself, the base is the same
// origin; during development (npm run dev) pass ?globe=http://192.168.0.71
// or set VITE_GLOBE.

import { createCall } from './request.js';
import { createLive, wsUrl } from './live.js';

const params = new URLSearchParams(location.search);
export const GLOBE = (params.get('globe') || import.meta.env.VITE_GLOBE || '').replace(/\/$/, '');

// ---- access token (optional; see Settings) ----
// Kept per globe in this browser. On the globe itself the key is plain
// 'globe-token', which the classic page at / shares.
const TOKEN_KEY = `globe-token${GLOBE ? `:${GLOBE}` : ''}`;
export function getToken() {
  try { return localStorage.getItem(TOKEN_KEY) || ''; } catch { return ''; }
}
export function saveToken(token) {
  try { if (token) localStorage.setItem(TOKEN_KEY, token); else localStorage.removeItem(TOKEN_KEY); } catch { /* private mode */ }
}
// The app shows its token dialog through this: fn(reason) -> Promise<token | null>.
let askToken = null;
export function onTokenNeeded(fn) {
  askToken = fn;
  return () => { if (askToken === fn) askToken = null; };
}

const call = createCall({
  base: GLOBE,
  fetch: (...a) => fetch(...a),
  tokens: { get: getToken, set: saveToken },
  ask: (reason) => (askToken ? askToken(reason) : null),
});

export const sendFrame = (frame) => call('POST', '/api/v1/frame', frame);
export const clearGlobe = () => call('POST', '/api/v1/clear', {});
export const resume = () => call('POST', '/api/v1/resume', {});
export const next = () => call('POST', '/api/v1/next', {});
export const getNow = () => call('GET', '/api/v1/now');
export const getScenes = () => call('GET', '/api/v1/scenes');
export const getScene = (id) => call('GET', `/api/v1/scenes/${encodeURIComponent(id)}`);
export const saveScene = (id, scene) => call('POST', `/api/v1/scenes/${encodeURIComponent(id)}`, scene);
export const deleteScene = (id) => call('DELETE', `/api/v1/scenes/${encodeURIComponent(id)}`);
export const getPlaylist = () => call('GET', '/api/v1/playlist');
// Keeps other playlist settings (the clock chime) when only the items change.
export const savePlaylist = async (items) => {
  let cur = {};
  try { cur = await call('GET', '/api/v1/playlist'); } catch { /* new */ }
  return call('POST', '/api/v1/playlist', { ...cur, items });
};
export const savePlaylistRaw = (body) => call('POST', '/api/v1/playlist', body);
export const getSettings = () => call('GET', '/api/v1/settings');
// {version, built, idf}: the firmware on the globe. Firmware from before versions: throws (404).
export const getVersion = () => call('GET', '/api/v1/version');
export const saveSettings = (s) => call('POST', '/api/v1/settings', s);
// The display switch: off blanks the globe and holds everything until it is on again.
export const setDisplay = (on) => saveSettings({ display: !!on });
export const playScene = (scene, seconds = 0) => call('POST', '/api/v1/play', { scene, seconds });
export const playFrames = (frames, seconds = 0) => call('POST', '/api/v1/play', { frames, seconds });
// The kitchen timer: counts down on the globe, then flashes TIME'S UP. 0 seconds cancels, or dismisses the time's up.
export const startTimer = (seconds, label = '') => call('POST', '/api/v1/timer', { seconds, label });
export const stopTimer = () => call('POST', '/api/v1/timer', { seconds: 0 });
// Pushed values: {cpu: 42, build: 'PASS', old: null}; ttl = seconds until they expire (0 = never)
export const getValues = () => call('GET', '/api/v1/values');
export const setValues = (values, ttl = 0) => call('POST', `/api/v1/values${ttl ? `?ttl=${ttl}` : ''}`, values);

// {required, valid, minLength, maxLength}. valid: the token (this browser's,
// or the one given) is accepted, or none is needed. Older firmware: throws (404).
export const getAuth = (token) => call('GET', '/api/v1/auth', undefined, { auth: true, token: token ?? getToken() });
// Set, change ('new-token') or remove ('') the globe's token. Needs the current
// one when set (asks for it on a 401); this browser then keeps the new one.
export const setAuthToken = async (token) => {
  const r = await call('POST', '/api/v1/auth', { token });
  saveToken(token);
  return r;
};

// ---- live: one WebSocket per tab, polling when it can't be used ----
let live = null;
const liveLink = () => live || (live = createLive({
  url: wsUrl(GLOBE, location),
  WS: typeof WebSocket === 'function' ? WebSocket : null,
  getNow, getValues,
  timers: { setTimeout: (f, ms) => setTimeout(f, ms), clearTimeout: (t) => clearTimeout(t) },
  doc: typeof document === 'object' ? document : null,
}));
// fn(now) whenever "now showing" changes; onStatus({online, via: 'ws'|'poll'}).
// Returns a function that unsubscribes.
export function subscribeNow(fn, onStatus) {
  const offNow = liveLink().subscribe('now', fn);
  const offStatus = onStatus ? liveLink().subscribe('status', onStatus) : () => {};
  return () => { offNow(); offStatus(); };
}
// fn(values) with the pushed values ({cpu: '42', ...}) as they change: pushed
// over the WebSocket, or polled every 3 s without one. Returns unsubscribe.
export const subscribeValues = (fn) => liveLink().subscribe('values', fn);
