// Talks to the globe. Served from the globe itself, the base is the same
// origin; during development (npm run dev) pass ?globe=http://192.168.0.71
// or set VITE_GLOBE.

const params = new URLSearchParams(location.search);
export const GLOBE = (params.get('globe') || import.meta.env.VITE_GLOBE || '').replace(/\/$/, '');

async function call(method, path, body) {
  const r = await fetch(GLOBE + path, {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    body: body ? JSON.stringify(body) : undefined,
  });
  const text = await r.text();
  let data;
  try { data = JSON.parse(text); } catch { data = { ok: r.ok, text }; }
  if (!r.ok || (data && data.ok === false)) throw new Error((data && data.error) || text || `HTTP ${r.status}`);
  return data;
}

export const sendFrame = (frame) => call('POST', '/api/v1/frame', frame);
export const clearGlobe = () => call('POST', '/api/v1/clear', {});
export const resume = () => call('POST', '/api/v1/resume', {});
export const getNow = () => call('GET', '/api/v1/now');
export const getScenes = () => call('GET', '/api/v1/scenes');
export const getScene = (id) => call('GET', `/api/v1/scenes/${encodeURIComponent(id)}`);
export const saveScene = (id, scene) => call('POST', `/api/v1/scenes/${encodeURIComponent(id)}`, scene);
export const deleteScene = (id) => call('DELETE', `/api/v1/scenes/${encodeURIComponent(id)}`);
export const getPlaylist = () => call('GET', '/api/v1/playlist');
export const savePlaylist = (items) => call('POST', '/api/v1/playlist', { items });
export const playScene = (scene, seconds = 0) => call('POST', '/api/v1/play', { scene, seconds });
export const playFrames = (frames, seconds = 0) => call('POST', '/api/v1/play', { frames, seconds });
