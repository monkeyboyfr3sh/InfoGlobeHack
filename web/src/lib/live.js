// Live link to the globe: one WebSocket per tab (GET /api/v1/ws) for "now
// showing" and the pushed values, polling as the fallback (older firmware,
// the globe's two-client limit, a proxy that doesn't pass WebSockets).
// No browser globals at import time, so the tests can drive it with fakes.

// ws:// URL for a globe base like '' (same origin), 'http://192.168.0.71' or 'https://x/sub'.
export function wsUrl(globe, loc) {
  const base = globe || (loc && /^https?:$/.test(loc.protocol) ? `${loc.protocol}//${loc.host}` : '');
  if (!base) return null;                                  // file:// and the like: poll only
  return (/^https?:\/\//i.test(base) ? base.replace(/^http/i, 'ws') : `ws://${base}`) + '/api/v1/ws';
}

// Wait before the next WebSocket attempt. 1013 = the globe already has its
// live clients: poll for a while instead of knocking every second.
export function backoffMs(fails, code, rand = Math.random) {
  if (code === 1013) return 30000;
  const base = Math.min(30000, 1000 * 2 ** Math.max(0, fails - 1));
  return Math.round(base * (0.8 + 0.4 * rand()));
}

export const POLL_MS = 1500, POLL_HIDDEN_MS = 5000, VALUES_POLL_MS = 3000;
export const OPEN_TIMEOUT_MS = 5000, SILENCE_MS = 40000, HIDDEN_CLOSE_MS = 60000;

/**
 * deps: { url, WS (WebSocket constructor or null), getNow, getValues,
 *         timers: {setTimeout, clearTimeout}, doc (document-like or null), now: () => ms }
 * subscribe('now'|'values'|'status', fn) -> unsubscribe. 'status' gets
 * {online, via: 'ws'|'poll'|null}. A new subscriber gets the latest value at once.
 */
export function createLive({ url, WS, getNow, getValues, timers = globalThis, doc = null, now = () => Date.now() }) {
  const subs = { now: new Set(), values: new Set(), status: new Set() };
  const last = { now: undefined, values: undefined };
  let status = { online: true, via: null };
  let running = false, ws = null, live = false, fails = 0, polling = false, lastValuesPoll = 0;
  let retryT = 0, pollT = 0, dogT = 0, openT = 0, hideT = 0;
  const hidden = () => !!(doc && doc.hidden);

  const emit = (kind, data) => { last[kind] = data; subs[kind].forEach((f) => f(data)); };
  const setStatus = (online, via) => {
    if (status.online === online && status.via === via) return;
    status = { online, via };
    subs.status.forEach((f) => f(status));
  };

  // ---- WebSocket ----
  function connect() {
    timers.clearTimeout(retryT); retryT = 0;
    if (!running || ws || !url || !WS || hidden()) return;
    let sock;
    try { sock = new WS(url); } catch { failed(0); return; }
    ws = sock;
    openT = timers.setTimeout(() => sock.close(), OPEN_TIMEOUT_MS);
    sock.onopen = () => { timers.clearTimeout(openT); watchdog(); };
    sock.onmessage = (e) => {
      if (ws !== sock) return;
      let m;
      try { m = JSON.parse(e.data); } catch { return; }
      live = true; fails = 0;
      watchdog();
      setStatus(true, 'ws');
      if (m.type === 'now' && m.now) emit('now', m.now);
      else if (m.type === 'values' && m.values) emit('values', m.values);
    };
    sock.onerror = () => {};             // onclose follows
    sock.onclose = (e) => {
      if (ws !== sock) return;           // we let go of it on purpose
      ws = null; live = false;
      timers.clearTimeout(openT); timers.clearTimeout(dogT);
      failed(e && e.code);
    };
  }
  // The globe repeats "now" every 15 s; silence means a dead link.
  function watchdog() { timers.clearTimeout(dogT); dogT = timers.setTimeout(() => ws && ws.close(), SILENCE_MS); }
  function failed(code) {
    fails++;
    if (!running) return;
    poll();                              // keep the page fresh while we wait
    if (!hidden()) retryT = timers.setTimeout(connect, backoffMs(fails, code));
  }
  function letGo() {                     // close without counting it as a failure
    const s = ws; ws = null; live = false;
    timers.clearTimeout(openT); timers.clearTimeout(dogT);
    if (s) { try { s.close(1000); } catch { /* already closed */ } }
  }

  // ---- polling (whenever the WebSocket isn't delivering) ----
  async function poll() {
    timers.clearTimeout(pollT); pollT = 0;
    if (!running || polling) return;
    if (!live) {
      const jobs = [];
      if (subs.now.size) jobs.push(getNow().then((n) => { if (!live) emit('now', n); return true; }, () => false));
      if (subs.values.size && now() - lastValuesPoll >= VALUES_POLL_MS) {
        lastValuesPoll = now();
        jobs.push(getValues().then((v) => { if (!live) emit('values', v); return true; }, () => false));
      }
      if (jobs.length) {
        polling = true;
        const ok = await Promise.all(jobs);
        polling = false;
        if (running && !live) setStatus(ok.every(Boolean), 'poll');
      }
    }
    if (running) pollT = timers.setTimeout(poll, hidden() ? POLL_HIDDEN_MS : POLL_MS);
  }

  // ---- tab visibility: a hidden tab gives its live slot back after a minute ----
  function onVisibility() {
    timers.clearTimeout(hideT); hideT = 0;
    if (hidden()) {
      hideT = timers.setTimeout(() => { if (hidden()) letGo(); }, HIDDEN_CLOSE_MS);
    } else {
      fails = 0;
      connect();
      poll();
    }
  }

  function start() {
    running = true;
    if (doc) doc.addEventListener('visibilitychange', onVisibility);
    connect();
    // The socket's first message normally beats this; without one, poll at once.
    pollT = timers.setTimeout(poll, ws ? 1000 : 0);
  }
  function stop() {
    running = false;
    letGo();
    [retryT, pollT, hideT].forEach((t) => timers.clearTimeout(t));
    retryT = pollT = hideT = 0;
    if (doc) doc.removeEventListener('visibilitychange', onVisibility);
  }

  function subscribe(kind, fn) {
    const wasIdle = !subs.now.size && !subs.values.size;
    subs[kind].add(fn);
    if (kind === 'status') fn(status);
    else if (last[kind] !== undefined) fn(last[kind]);
    if (kind !== 'status' && wasIdle) start();
    else if (kind === 'values' && running && !live && last.values === undefined) { lastValuesPoll = 0; poll(); }
    return () => {
      subs[kind].delete(fn);
      if (running && !subs.now.size && !subs.values.size) stop();
    };
  }

  return { subscribe, state: () => ({ running, live, fails, connected: !!ws, status }) };
}
