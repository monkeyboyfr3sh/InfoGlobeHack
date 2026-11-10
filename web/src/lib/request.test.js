import { describe, it, expect, vi } from 'vitest';
import { createCall, authHeaders, generateToken, TOKEN_RE } from './request.js';

const TOKEN = 'correct-horse';

// A fake globe: writes need TOKEN (Authorization: Bearer), reads are open.
function fakeGlobe({ token = TOKEN } = {}) {
  const log = [];
  const fetch = vi.fn(async (url, opts = {}) => {
    const auth = (opts.headers && opts.headers.Authorization) || '';
    log.push({ url, method: opts.method, auth });
    const write = opts.method !== 'GET';
    const ok = !write || !token || auth === `Bearer ${token}`;
    const status = ok ? 200 : 401;
    const body = ok ? JSON.stringify({ ok: true }) : JSON.stringify({ ok: false, error: auth ? 'Wrong access token' : 'needs token' });
    return { status, ok, text: async () => body };
  });
  return { fetch, log };
}
function store(initial = '') {
  let t = initial;
  return { get: () => t, set: vi.fn((v) => { t = v; }), peek: () => t };
}

describe('token headers', () => {
  it('sends Authorization: Bearer only when there is a token', () => {
    expect(authHeaders('')).toEqual({});
    expect(authHeaders('abcdefgh')).toEqual({ Authorization: 'Bearer abcdefgh' });
  });
  it('sends it with changes but not with reads', async () => {
    const g = fakeGlobe();
    const call = createCall({ base: 'http://g', fetch: g.fetch, tokens: store(TOKEN) });
    await call('GET', '/api/v1/now');
    await call('POST', '/api/v1/clear', {});
    await call('DELETE', '/api/v1/scenes/x');
    expect(g.log.map((l) => [l.method, l.auth])).toEqual([['GET', ''], ['POST', `Bearer ${TOKEN}`], ['DELETE', `Bearer ${TOKEN}`]]);
    expect(g.log[0].url).toBe('http://g/api/v1/now');
  });
});

describe('401 flow', () => {
  it('asks once, retries with the answer and keeps it', async () => {
    const g = fakeGlobe();
    const tokens = store('');
    const ask = vi.fn(async () => TOKEN);
    const call = createCall({ fetch: g.fetch, tokens, ask });
    await expect(call('POST', '/api/v1/frame', { text: 'HI' })).resolves.toEqual({ ok: true });
    expect(ask).toHaveBeenCalledWith('needed');
    expect(g.log.map((l) => l.auth)).toEqual(['', `Bearer ${TOKEN}`]);
    expect(tokens.peek()).toBe(TOKEN);
    await call('POST', '/api/v1/next', {});            // remembered: no second question
    expect(ask).toHaveBeenCalledTimes(1);
  });

  it('says the saved token was refused', async () => {
    const g = fakeGlobe();
    const ask = vi.fn(async () => TOKEN);
    const call = createCall({ fetch: g.fetch, tokens: store('old-token-1'), ask });
    await call('POST', '/api/v1/clear', {});
    expect(ask).toHaveBeenCalledWith('wrong');
  });

  it('gives a clear error when the answer is wrong too, and forgets it', async () => {
    const g = fakeGlobe();
    const tokens = store('');
    const call = createCall({ fetch: g.fetch, tokens, ask: async () => 'not-the-token' });
    const err = await call('POST', '/api/v1/clear', {}).catch((e) => e);
    expect(err.status).toBe(401);
    expect(err.message).toMatch(/not accepted/);
    expect(tokens.peek()).toBe('');
    expect(g.fetch).toHaveBeenCalledTimes(2);           // asked once, retried once
  });

  it('cancelling the dialog fails the change without a retry', async () => {
    const g = fakeGlobe();
    const call = createCall({ fetch: g.fetch, tokens: store(''), ask: async () => null });
    const err = await call('POST', '/api/v1/clear', {}).catch((e) => e);
    expect(err.status).toBe(401);
    expect(err.message).toMatch(/needs the globe's access token/);
    expect(g.fetch).toHaveBeenCalledTimes(1);
  });

  it('shares one question between changes that hit 401 together', async () => {
    const g = fakeGlobe();
    let answer;
    const ask = vi.fn(() => new Promise((r) => { answer = r; }));
    const call = createCall({ fetch: g.fetch, tokens: store(''), ask });
    const a = call('POST', '/api/v1/clear', {});
    const b = call('POST', '/api/v1/next', {});
    await vi.waitFor(() => expect(ask).toHaveBeenCalled());
    await new Promise((r) => setTimeout(r, 0));
    answer(TOKEN);
    await expect(Promise.all([a, b])).resolves.toHaveLength(2);
    expect(ask).toHaveBeenCalledTimes(1);
  });

  it('works unchanged against a globe without a token', async () => {
    const g = fakeGlobe({ token: '' });
    const ask = vi.fn();
    const call = createCall({ fetch: g.fetch, tokens: store(''), ask });
    await call('POST', '/api/v1/clear', {});
    expect(ask).not.toHaveBeenCalled();
  });

  it('keeps the existing error shape for other failures', async () => {
    const fetch = async () => ({ status: 400, ok: false, text: async () => '{"ok":false,"error":"Body must be JSON"}' });
    const call = createCall({ fetch, tokens: store('') });
    const err = await call('POST', '/api/v1/frame', {}).catch((e) => e);
    expect([err.status, err.message]).toEqual([400, 'Body must be JSON']);
  });

  it('can check a given token without asking (GET /api/v1/auth)', async () => {
    const g = fakeGlobe();
    const ask = vi.fn();
    const call = createCall({ fetch: g.fetch, tokens: store(''), ask });
    await call('GET', '/api/v1/auth', undefined, { auth: true, token: 'try-this-one' });
    expect(g.log[0].auth).toBe('Bearer try-this-one');
    expect(ask).not.toHaveBeenCalled();
  });
});

describe('generateToken', () => {
  it('makes 20 letters and digits the firmware accepts', () => {
    for (let i = 0; i < 50; i++) {
      const t = generateToken();
      expect(t).toMatch(/^[A-Za-z0-9]{20}$/);
      expect(TOKEN_RE.test(t)).toBe(true);
    }
  });
  it('skips biased bytes', () => {
    const bytes = [255, 250, 248, 0, 61, 62];           // 248+ dropped; 62 % 62 = 0 -> 'A'
    const t = generateToken((n) => Uint8Array.from({ length: n }, (_, i) => bytes[i % bytes.length]));
    expect(t.startsWith('A9A')).toBe(true);
  });
  it('matches the firmware rules', () => {
    expect(TOKEN_RE.test('short')).toBe(false);
    expect(TOKEN_RE.test('has space in it')).toBe(false);
    expect(TOKEN_RE.test('ok.token_with~dash-1')).toBe(true);
    expect(TOKEN_RE.test('x'.repeat(65))).toBe(false);
  });
});
