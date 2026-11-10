// HTTP calls to the globe with its optional access token (docs/api.md).
// Changes (POST/DELETE) carry the token when this browser has one; GETs
// don't, so they stay simple cross-origin requests during development. On a
// 401 the app asks for the token once, retries, and keeps it if it works.
// No browser globals here, so the tests can drive it with a fake fetch.

export const TOKEN_RE = /^[A-Za-z0-9._~-]{8,64}$/;
export const TOKEN_RULES = '8-64 characters: letters, digits and . _ ~ -';

export const authHeaders = (token) => (token ? { Authorization: `Bearer ${token}` } : {});

export class GlobeError extends Error {
  constructor(message, status) { super(message); this.status = status; }
}

// 20 random letters and digits (about 119 bits).
export function generateToken(rand = (n) => crypto.getRandomValues(new Uint8Array(n))) {
  const abc = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
  let out = '';
  while (out.length < 20) {
    for (const b of rand(32)) if (b < 248 && out.length < 20) out += abc[b % 62];   // 248 = 4 * 62: no bias
  }
  return out;
}

/**
 * fetch: fetch-like. tokens: {get() -> string, set(string)} ('' forgets it).
 * ask(reason) -> Promise<token | null>; reason is 'needed' (none was sent) or
 * 'wrong' (the saved one was refused). Returns call(method, path, body, {auth}).
 */
export function createCall({ base = '', fetch, tokens, ask }) {
  let asking = null;                     // one dialog at a time, shared by requests that hit 401 together
  const askOnce = (reason) => {
    if (!asking) asking = Promise.resolve(ask ? ask(reason) : null).finally(() => { asking = null; });
    return asking;
  };

  async function send(method, path, body, token) {
    const headers = { ...(body ? { 'Content-Type': 'application/json' } : {}), ...authHeaders(token) };
    const r = await fetch(base + path, {
      method,
      headers: Object.keys(headers).length ? headers : undefined,
      body: body ? JSON.stringify(body) : undefined,
    });
    const text = await r.text();
    let data;
    try { data = JSON.parse(text); } catch { data = { ok: r.ok, text }; }
    return { r, data, text };
  }

  return async function call(method, path, body, { auth = method !== 'GET', token } = {}) {
    const sent = auth ? (token ?? tokens.get()) : '';
    let { r, data, text } = await send(method, path, body, sent);
    if (r.status === 401 && auth && token === undefined) {
      const fresh = (await askOnce(sent ? 'wrong' : 'needed') || '').trim();
      if (!fresh) throw new GlobeError("This change needs the globe's access token.", 401);
      ({ r, data, text } = await send(method, path, body, fresh));
      if (r.status === 401) {
        tokens.set('');
        throw new GlobeError('That access token was not accepted. Check it under Settings, Access token.', 401);
      }
      tokens.set(fresh);
    }
    if (!r.ok || (data && data.ok === false)) throw new GlobeError((data && data.error) || text || `HTTP ${r.status}`, r.status);
    return data;
  };
}
