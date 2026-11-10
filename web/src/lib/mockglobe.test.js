// End to end over real sockets: lib/live.js and lib/request.js against a tiny
// mock globe that follows docs/api.md (WebSocket push, 2-client limit with
// close code 1013, token on writes). Uses Node's own fetch and WebSocket.
import { describe, it, expect, afterEach } from 'vitest';
import http from 'node:http';
import { createHash } from 'node:crypto';
import { createLive } from './live.js';
import { createCall } from './request.js';

function frame(op, payload) {
  const len = payload.length;
  const head = len < 126 ? Buffer.from([0x80 | op, len]) : Buffer.from([0x80 | op, 126, len >> 8, len & 255]);
  return Buffer.concat([head, payload]);
}

async function mockGlobe({ ws = true, token = '', maxClients = 2 } = {}) {
  const state = { now: { mode: 'playlist', scene: 'clock', text: '12:00' }, values: { cpu: '7' } };
  const clients = new Set(), sockets = new Set();
  const push = (type) => {
    const m = frame(1, Buffer.from(JSON.stringify({ type, [type]: state[type] })));
    clients.forEach((s) => s.write(m));
  };
  const tokenOf = (req, url) => {
    const a = req.headers.authorization;
    if (a && /^Bearer /i.test(a)) return a.slice(7).trim();
    if (req.headers['x-globe-token']) return req.headers['x-globe-token'];
    return url.searchParams.get('token');
  };
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://x');
    const json = (status, o) => {
      res.writeHead(status, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' });
      res.end(JSON.stringify(o));
    };
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      if (req.method === 'GET' && url.pathname === '/api/v1/now') return json(200, state.now);
      if (req.method === 'GET' && url.pathname === '/api/v1/values') return json(200, state.values);
      if (req.method === 'GET' && url.pathname === '/api/v1/auth') {
        return json(200, { required: !!token, valid: !token || tokenOf(req, url) === token, minLength: 8, maxLength: 64 });
      }
      if (req.method === 'POST' && url.pathname === '/api/v1/values') {
        const given = tokenOf(req, url);
        if (token && given !== token) return json(401, { ok: false, error: given ? 'Wrong access token' : 'needs token' });
        Object.assign(state.values, JSON.parse(body));
        push('values');
        return json(200, { ok: true });
      }
      json(404, { ok: false, error: 'Not found' });
    });
  });
  server.on('connection', (s) => { sockets.add(s); s.on('close', () => sockets.delete(s)); });
  server.on('upgrade', (req, sock) => {
    if (!ws || req.url !== '/api/v1/ws') { sock.end('HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n'); return; }
    const accept = createHash('sha1').update(`${req.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
    sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    if (clients.size >= maxClients) { sock.end(frame(8, Buffer.from([0x03, 0xf5, ...Buffer.from('busy')]))); return; }
    clients.add(sock);
    sock.on('close', () => clients.delete(sock));
    sock.on('error', () => clients.delete(sock));
    sock.write(frame(1, Buffer.from(JSON.stringify({ type: 'now', now: state.now }))));
    sock.write(frame(1, Buffer.from(JSON.stringify({ type: 'values', values: state.values }))));
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  return {
    base, state, clients, push,
    setNow(n) { state.now = n; push('now'); },
    close: () => new Promise((r) => { sockets.forEach((s) => s.destroy()); server.close(r); }),
  };
}

const until = async (fn, ms = 3000) => {
  const t0 = Date.now();
  while (!fn()) {
    if (Date.now() - t0 > ms) throw new Error('timed out');
    await new Promise((r) => setTimeout(r, 20));
  }
};

const cleanups = [];
afterEach(async () => { while (cleanups.length) await cleanups.pop()(); });

function link(g) {
  const call = createCall({ base: g.base, fetch, tokens: { get: () => '', set() {} } });
  return createLive({
    url: g.base.replace(/^http/, 'ws') + '/api/v1/ws',
    WS: WebSocket,
    getNow: () => call('GET', '/api/v1/now'),
    getValues: () => call('GET', '/api/v1/values'),
  });
}
function watch(live) {
  const seen = { now: [], values: [], status: [] };
  const offs = ['now', 'values', 'status'].map((k) => live.subscribe(k, (v) => seen[k].push(v)));
  cleanups.push(async () => offs.forEach((off) => off()));
  return seen;
}

describe('against a mock globe', () => {
  it('gets the state on connect and every push after it, over the WebSocket', async () => {
    const g = await mockGlobe();
    cleanups.push(g.close);
    const seen = watch(link(g));
    await until(() => seen.now.length && seen.values.length);
    expect(seen.now[0].text).toBe('12:00');
    expect(seen.values[0]).toEqual({ cpu: '7' });
    g.setNow({ mode: 'playlist', scene: 'clock', text: '12 00' });
    await until(() => seen.now.length === 2);
    expect(seen.now[1].text).toBe('12 00');
    expect(seen.status.at(-1)).toEqual({ online: true, via: 'ws' });
  });

  it('polls instead when the firmware has no WebSocket', async () => {
    const g = await mockGlobe({ ws: false });
    cleanups.push(g.close);
    const seen = watch(link(g));
    await until(() => seen.now.length > 0);
    expect(seen.now[0].text).toBe('12:00');
    expect(seen.status.at(-1)).toEqual({ online: true, via: 'poll' });
  });

  it('a third tab gets 1013 and falls back to polling; the first two stay live', async () => {
    const g = await mockGlobe();
    cleanups.push(g.close);
    const a = watch(link(g)), b = watch(link(g));
    await until(() => a.now.length && b.now.length && g.clients.size === 2);
    const c = watch(link(g));
    await until(() => c.status.at(-1) && c.status.at(-1).via === 'poll' && c.now.length > 0);
    expect(g.clients.size).toBe(2);
    g.setNow({ mode: 'manual', text: 'HELLO' });
    await until(() => a.now.at(-1).text === 'HELLO' && b.now.at(-1).text === 'HELLO');
  });

  it('a 401 asks for the token once, retries, and the value is pushed to live clients', async () => {
    const g = await mockGlobe({ token: 'correct-horse' });
    cleanups.push(g.close);
    const seen = watch(link(g));
    await until(() => seen.values.length > 0);
    let saved = '';
    const asked = [];
    const call = createCall({ base: g.base, fetch, tokens: { get: () => saved, set: (t) => { saved = t; } },
      ask: async (why) => { asked.push(why); return 'correct-horse'; } });
    await call('POST', '/api/v1/values', { cpu: 42 });
    expect(asked).toEqual(['needed']);
    expect(saved).toBe('correct-horse');
    await until(() => seen.values.at(-1).cpu === 42);
  });

  it('a wrong token is refused with a clear message; ?token= works for webhooks', async () => {
    const g = await mockGlobe({ token: 'correct-horse' });
    cleanups.push(g.close);
    const call = createCall({ base: g.base, fetch, tokens: { get: () => '', set() {} }, ask: async () => 'wrong-horse' });
    await expect(call('POST', '/api/v1/values', { cpu: 1 })).rejects.toThrow(/not accepted/);
    const r = await fetch(`${g.base}/api/v1/values?token=correct-horse`, { method: 'POST', body: '{"cpu":5}' });
    expect(r.status).toBe(200);
    const auth = await (await fetch(`${g.base}/api/v1/auth`, { headers: { 'X-Globe-Token': 'correct-horse' } })).json();
    expect(auth).toMatchObject({ required: true, valid: true });
  });
});
