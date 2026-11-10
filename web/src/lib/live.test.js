import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { wsUrl, backoffMs, createLive } from './live.js';

describe('wsUrl', () => {
  const page = { protocol: 'http:', host: '192.168.0.71' };
  it('uses the page origin when served by the globe', () => {
    expect(wsUrl('', page)).toBe('ws://192.168.0.71/api/v1/ws');
    expect(wsUrl('', { protocol: 'https:', host: 'globe.lan:8443' })).toBe('wss://globe.lan:8443/api/v1/ws');
  });
  it('follows GLOBE during development', () => {
    expect(wsUrl('http://192.168.0.71', { protocol: 'http:', host: 'localhost:5173' })).toBe('ws://192.168.0.71/api/v1/ws');
    expect(wsUrl('https://example.org/globe', page)).toBe('wss://example.org/globe/api/v1/ws');
    expect(wsUrl('192.168.0.71', page)).toBe('ws://192.168.0.71/api/v1/ws');
  });
  it('gives up where there is no server', () => {
    expect(wsUrl('', { protocol: 'file:', host: '' })).toBe(null);
  });
});

describe('backoffMs', () => {
  const mid = () => 0.5;
  it('doubles from 1 s up to 30 s', () => {
    expect([1, 2, 3, 4, 5, 6, 9].map((f) => backoffMs(f, 1006, mid))).toEqual([1000, 2000, 4000, 8000, 16000, 30000, 30000]);
  });
  it('waits 30 s when the globe is full (1013)', () => {
    expect(backoffMs(1, 1013, mid)).toBe(30000);
  });
  it('adds jitter of +-20 %', () => {
    expect(backoffMs(3, 1006, () => 0)).toBe(3200);
    expect(backoffMs(3, 1006, () => 1)).toBe(4800);
  });
});

/* ---- the connection state machine, with fake timers and a fake socket ---- */

class FakeWS {
  static all = [];
  constructor(url) { this.url = url; this.closed = false; FakeWS.all.push(this); }
  close(code = 1005) { if (!this.closed) { this.closed = true; this.onclose && this.onclose({ code }); } }
  open() { this.onopen && this.onopen(); }
  send(o) { this.onmessage({ data: JSON.stringify(o) }); }
  drop(code) { this.closed = true; this.onclose({ code }); }   // server side closes
}
const latest = () => FakeWS.all[FakeWS.all.length - 1];

function setup({ WS = FakeWS, url = 'ws://g/api/v1/ws' } = {}) {
  const getNow = vi.fn(async () => ({ mode: 'playlist', text: 'polled' }));
  const getValues = vi.fn(async () => ({ cpu: '1' }));
  const doc = { hidden: false, listeners: {}, addEventListener(t, f) { this.listeners[t] = f; }, removeEventListener(t) { delete this.listeners[t]; } };
  const live = createLive({
    url, WS, getNow, getValues, doc,
    timers: { setTimeout: (f, ms) => setTimeout(f, ms), clearTimeout: (t) => clearTimeout(t) },
    now: () => Date.now(),
  });
  return { live, getNow, getValues, doc };
}

beforeEach(() => { vi.useFakeTimers(); FakeWS.all = []; });
afterEach(() => { vi.useRealTimers(); });

describe('createLive', () => {
  it('uses the WebSocket and does not poll while it delivers', async () => {
    const { live, getNow } = setup();
    const seen = [], status = [];
    live.subscribe('status', (s) => status.push(s.via));
    const off = live.subscribe('now', (n) => seen.push(n.text));
    expect(FakeWS.all).toHaveLength(1);
    latest().open();
    latest().send({ type: 'now', now: { mode: 'playlist', text: '12:00' } });
    latest().send({ type: 'now', now: { mode: 'playlist', text: '12 00' } });
    await vi.advanceTimersByTimeAsync(10000);
    expect(seen).toEqual(['12:00', '12 00']);
    expect(getNow).not.toHaveBeenCalled();
    expect(status).toContain('ws');
    off();
    expect(latest().closed).toBe(true);
    expect(live.state().running).toBe(false);
  });

  it('polls at once when the socket drops, then reconnects with backoff', async () => {
    const { live, getNow } = setup();
    const seen = [];
    live.subscribe('now', (n) => seen.push(n.text));
    latest().open();
    latest().send({ type: 'now', now: { text: 'ws' } });
    latest().drop(1006);
    await vi.advanceTimersByTimeAsync(0);
    expect(getNow).toHaveBeenCalledTimes(1);          // no waiting for the next poll
    expect(seen).toEqual(['ws', 'polled']);
    expect(live.state().status.via).toBe('poll');
    await vi.advanceTimersByTimeAsync(1300);            // 1 s backoff (with jitter up to 1.2 s)
    expect(FakeWS.all).toHaveLength(2);
    latest().open();
    latest().send({ type: 'now', now: { text: 'back' } });
    expect(live.state().status.via).toBe('ws');
    const calls = getNow.mock.calls.length;
    await vi.advanceTimersByTimeAsync(10000);
    expect(getNow.mock.calls.length).toBe(calls);       // live again: polling idles
  });

  it('falls back to polling when there is no WebSocket at all', async () => {
    const { live, getNow } = setup({ WS: null });
    live.subscribe('now', () => {});
    await vi.advanceTimersByTimeAsync(0);
    expect(getNow).toHaveBeenCalledTimes(1);
    await vi.advanceTimersByTimeAsync(1500 * 3);
    expect(getNow).toHaveBeenCalledTimes(4);
  });

  it('keeps polling and backs off when the firmware has no /api/v1/ws', async () => {
    const { live, getNow } = setup();
    live.subscribe('now', () => {});
    for (let i = 0; i < 6; i++) {                      // every attempt fails before any message
      latest().drop(1006);
      await vi.advanceTimersByTimeAsync(40000);
    }
    expect(live.state().fails).toBeGreaterThanOrEqual(6);
    expect(getNow.mock.calls.length).toBeGreaterThan(20);   // the page stayed fresh by polling
    expect(live.state().status).toEqual({ online: true, via: 'poll' });
  });

  it('waits 30 s after "too many clients" (1013)', async () => {
    const { live } = setup();
    live.subscribe('now', () => {});
    latest().open();
    latest().drop(1013);
    await vi.advanceTimersByTimeAsync(29000);
    expect(FakeWS.all).toHaveLength(1);
    await vi.advanceTimersByTimeAsync(1500);
    expect(FakeWS.all).toHaveLength(2);
  });

  it('reports offline when polling fails too', async () => {
    const { live, getNow } = setup({ WS: null });
    getNow.mockRejectedValue(new Error('down'));
    const status = [];
    live.subscribe('status', (s) => status.push(s.online));
    live.subscribe('now', () => {});
    await vi.advanceTimersByTimeAsync(0);
    expect(status.at(-1)).toBe(false);
  });

  it('closes a silent socket (40 s) and reconnects', async () => {
    const { live } = setup();
    live.subscribe('now', () => {});
    latest().open();
    latest().send({ type: 'now', now: { text: 'a' } });
    await vi.advanceTimersByTimeAsync(39000);
    expect(latest().closed).toBe(false);
    await vi.advanceTimersByTimeAsync(2000);
    expect(FakeWS.all[0].closed).toBe(true);
    await vi.advanceTimersByTimeAsync(1300);
    expect(FakeWS.all).toHaveLength(2);
  });

  it('gives the slot back after a minute in a hidden tab, reconnects when visible', async () => {
    const { live, doc } = setup();
    live.subscribe('now', () => {});
    latest().open();
    latest().send({ type: 'now', now: { text: 'a' } });
    doc.hidden = true; doc.listeners.visibilitychange();
    await vi.advanceTimersByTimeAsync(61000);
    expect(FakeWS.all[0].closed).toBe(true);
    await vi.advanceTimersByTimeAsync(120000);
    expect(FakeWS.all).toHaveLength(1);                 // no reconnecting while hidden
    doc.hidden = false; doc.listeners.visibilitychange();
    expect(FakeWS.all).toHaveLength(2);
  });

  it('shares one socket for now and values and hands a late subscriber the last values', async () => {
    const { live, getValues } = setup();
    live.subscribe('now', () => {});
    latest().open();
    latest().send({ type: 'values', values: { cpu: '42' } });
    const got = [];
    const off = live.subscribe('values', (v) => got.push(v.cpu));
    expect(got).toEqual(['42']);
    latest().send({ type: 'values', values: { cpu: '43' } });
    expect(got).toEqual(['42', '43']);
    expect(FakeWS.all).toHaveLength(1);
    expect(getValues).not.toHaveBeenCalled();
    off();
    expect(live.state().running).toBe(true);           // "now" still listens
  });

  it('polls values every 3 s without a socket', async () => {
    const { live, getValues } = setup({ WS: null });
    live.subscribe('values', () => {});
    await vi.advanceTimersByTimeAsync(0);
    await vi.advanceTimersByTimeAsync(9000);
    expect(getValues.mock.calls.length).toBeGreaterThanOrEqual(3);
    expect(getValues.mock.calls.length).toBeLessThanOrEqual(4);
  });
});
