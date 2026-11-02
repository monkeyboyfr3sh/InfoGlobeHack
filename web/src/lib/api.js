// Talks to the globe. Served from the globe itself, the base is the same
// origin; during development (npm run dev) pass ?globe=http://192.168.0.71
// or set VITE_GLOBE.

const params = new URLSearchParams(location.search);
export const GLOBE = (params.get('globe') || import.meta.env.VITE_GLOBE || '').replace(/\/$/, '');

async function post(path, body) {
  const r = await fetch(GLOBE + path, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  });
  const text = await r.text();
  let data;
  try { data = JSON.parse(text); } catch { data = { ok: r.ok, text }; }
  if (!r.ok || data.ok === false) throw new Error(data.error || text || `HTTP ${r.status}`);
  return data;
}

export const sendFrame = (frame) => post('/api/v1/frame', frame);
export const clearGlobe = () => post('/api/v1/clear', {});
export const resumeClock = () => post('/mode', { time_mode: true });
