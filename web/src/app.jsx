import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import effectsDoc from '../../docs/effects.json';
import { encodeText, compileFrame, hex, SUGGEST, TEXT_MAX } from './lib/protocol.js';
import { makeScene, textWidth, WINDOW, BAND } from './lib/sim.js';
import { Screen } from './components/Screen.jsx';
import { Gallery } from './components/Gallery.jsx';
import { sendFrame, clearGlobe, resumeClock, GLOBE } from './lib/api.js';

const EFFECTS = effectsDoc.effects;
const ACCENT_KEYS = 'à á â ä è é ê ë ì í î ï ò ó ô ö ù ú û ü Ç ñ É ✉'.split(' ');
const AFTER_OPTIONS = [
  ['spin', 'Spin', 'Text circles the globe (readable from any side)'],
  ['hold', 'Hold still', 'Stays in front; can blink the start of the text'],
  ['flash', 'Flash', 'Whole text flashes on and off'],
  ['instant', 'Instant', 'Still and left-aligned, no transition'],
];
const DEFAULT = { text: 'SERVER OK', entrance: 2, after: 'spin', blink: 0, shift: -1, live: false };

function load() {
  try { return { ...DEFAULT, ...JSON.parse(localStorage.getItem('designer') || '{}') }; } catch { return DEFAULT; }
}

function ThemeButton() {
  const isDark = () => {
    const t = document.documentElement.dataset.theme;
    return t ? t === 'dark' : matchMedia('(prefers-color-scheme: dark)').matches;
  };
  const [dark, setDark] = useState(isDark());
  const toggle = () => {
    const next = dark ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('theme', next); } catch { /* private mode */ }
    setDark(!dark);
  };
  const label = dark ? 'Switch to light mode' : 'Switch to dark mode';
  return (
    <button class="btn icon" onClick={toggle} aria-label={label} title={label}>
      {dark
        ? <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><circle cx="8" cy="8" r="3.2" /><g stroke="currentColor" stroke-width="1.4" stroke-linecap="round"><path d="M8 1v1.6M8 13.4V15M1 8h1.6M13.4 8H15M3 3l1.1 1.1M11.9 11.9 13 13M3 13l1.1-1.1M11.9 4.1 13 3" /></g></svg>
        : <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M13.5 10.4A6 6 0 0 1 5.6 2.5a6 6 0 1 0 7.9 7.9z" /></svg>}
    </button>
  );
}

function WidthMeter({ codes }) {
  const w = textWidth(codes);
  const n = codes.length;
  let cls = 'ok', msg = `Fits the front window (${n} characters)`;
  if (n > TEXT_MAX) { cls = 'bad'; msg = `${n} of ${TEXT_MAX} characters: too long for one frame`; }
  else if (w > WINDOW) { cls = 'warn'; msg = `${n} characters: wider than the front window, so it reads while spinning`; }
  const pct = Math.min(100, (w / (BAND * 0.95)) * 100);
  return (
    <div>
      <div class={`meter ${cls}`}><div style={{ width: `${pct}%` }} /></div>
      <div class={`meter-text ${cls}`}>{msg}</div>
    </div>
  );
}

export function App() {
  const [s, setS] = useState(load);
  const [hover, setHover] = useState(null);
  const [prev, setPrev] = useState({ codes: encodeText('12:00').codes, style: { entrance: -1, after: 'spin' } });
  const [runKey, setRunKey] = useState(0);
  const [status, setStatus] = useState(null);
  const [lastReply, setLastReply] = useState(null);
  const [busy, setBusy] = useState(false);
  const inputRef = useRef(null);
  const set = (patch) => setS((o) => ({ ...o, ...patch }));

  useEffect(() => { try { localStorage.setItem('designer', JSON.stringify(s)); } catch { /* ignore */ } }, [s]);

  const { codes, bad } = useMemo(() => encodeText(s.text), [s.text]);
  const style = { entrance: s.entrance, after: s.after, blink: s.blink, shift: s.shift };
  const shownEntrance = hover !== null ? hover : s.entrance;
  const scene = useMemo(() => makeScene(prev, { codes: codes.slice(0, TEXT_MAX), style: { ...style, entrance: shownEntrance } }, EFFECTS),
    [prev, codes, s.after, s.blink, s.shift, shownEntrance]);
  let compiled = null, compileError = null;
  try { compiled = compileFrame(codes, style); } catch (e) { compileError = e.message; }

  const send = async (quiet) => {
    if (compileError) return;
    setBusy(true);
    try {
      const body = { text: s.text, entrance: s.entrance >= 0 ? s.entrance : null, after: s.after };
      if (s.after === 'hold') { body.blink = s.blink; body.shift = s.shift; }
      const r = await sendFrame(body);
      setLastReply(r);
      setPrev({ codes, style });
      if (!quiet) setStatus({ ok: true, msg: 'Sent to the globe' + (r.then ? ` (still frame follows after ${(r.thenAfterMs / 1000).toFixed(1)} s)` : '') });
    } catch (e) {
      setStatus({ ok: false, msg: `Could not send: ${e.message}` });
    } finally { setBusy(false); }
  };

  // Live mode: push every change to the globe, debounced.
  useEffect(() => {
    if (!s.live) return undefined;
    const t = setTimeout(() => send(true), 400);
    return () => clearTimeout(t);
  }, [s.live, s.text, s.entrance, s.after, s.blink, s.shift]);

  const insert = (ch) => {
    const el = inputRef.current;
    const a = el ? el.selectionStart : s.text.length, b = el ? el.selectionEnd : s.text.length;
    set({ text: s.text.slice(0, a) + ch + s.text.slice(b) });
    requestAnimationFrame(() => { if (el) { el.focus(); el.setSelectionRange(a + ch.length, a + ch.length); } });
  };
  const fixAll = () => set({ text: [...s.text].map((ch) => (SUGGEST[ch] !== undefined && encodeText(ch).bad.length ? SUGGEST[ch] : ch)).join('') });
  const fxName = (id) => (id < 0 ? 'None' : `${EFFECTS[id].name} (FX ${id})`);

  return (
    <div class="wrap">
      <header>
        <h1>InfoGlobe</h1>
        <div class="head-right">
          {GLOBE && <span class="muted">globe: {GLOBE}</span>}
          <ThemeButton />
        </div>
      </header>
      <nav class="tabs">
        <a href={GLOBE + "/"}>Control &amp; Explorer</a>
        <a href="#" aria-current="page">Designer</a>
      </nav>

      <div class="layout">
        <div class="sticky">
          <Screen scene={scene} runKey={runKey} badge="≈ preview" />
          <div class="row controls">
            <button class="btn" onClick={() => setRunKey((k) => k + 1)}>↻ Replay</button>
            <span class="muted">
              {hover !== null ? <>Previewing <b>{fxName(hover)}</b></> : <>From “{String.fromCharCode(...prev.codes.filter((c) => c >= 32 && c < 127))}” with {fxName(s.entrance)}</>}
            </span>
          </div>
          <details style="margin-top:16px">
            <summary>Show commands (advanced)</summary>
            {compiled && (
              <div style="margin-top:8px">
                <div class="bytes">{hex(compiled.first)}</div>
                {compiled.then && <div class="bytes" style="margin-top:6px">then {hex(compiled.then)}</div>}
                {lastReply && <p class="hint" style="margin-top:6px">Globe last compiled: <span class="mono">{lastReply.bytes}</span></p>}
              </div>
            )}
          </details>
        </div>

        <div>
          <div class="section">
            <h2>Text</h2>
            <input ref={inputRef} class="big" type="text" value={s.text} maxLength={60} spellcheck={false}
              onInput={(e) => set({ text: e.currentTarget.value })} aria-label="Text to show" />
            <WidthMeter codes={codes} />
            {bad.length > 0 && (
              <div class="problem">
                The globe can't draw {[...new Set(bad.map((b) => `“${b.ch}”`))].join(' ')}; they show as blanks.
                {bad.some((b) => SUGGEST[b.ch] !== undefined) && <> <button class="btn small" onClick={fixAll}>Replace with suggestions</button></>}
              </div>
            )}
            <div class="chips" style="margin-top:10px" aria-label="Insert a special character">
              {ACCENT_KEYS.map((ch) => <button key={ch} class="chip char" onClick={() => insert(ch)} title={`Insert ${ch}`}>{ch}</button>)}
            </div>
          </div>

          <div class="section">
            <h2>Entrance</h2>
            <p class="hint">Hover a tile to preview it with your text; click to choose.</p>
            <Gallery effects={EFFECTS} codes={codes.slice(0, TEXT_MAX)} prevCodes={prev.codes} selected={s.entrance}
              onSelect={(id) => { set({ entrance: id }); setRunKey((k) => k + 1); }} onHover={setHover} />
          </div>

          <div class="section">
            <h2>Afterwards</h2>
            <div class="chips">
              {AFTER_OPTIONS.map(([v, label, tip]) => (
                <button key={v} class="chip" aria-pressed={s.after === v} title={tip} onClick={() => set({ after: v })}>{label}</button>
              ))}
            </div>
            <p class="hint" style="margin-top:6px">{AFTER_OPTIONS.find((o) => o[0] === s.after)[2]}.</p>
            {s.after === 'hold' && (
              <div style="margin-top:8px">
                <div class="row">
                  <label class="row" style="flex:1">Blink the first
                    <input type="number" min="0" max={codes.length} value={s.blink}
                      onInput={(e) => set({ blink: Math.max(0, Math.min(codes.length, e.currentTarget.valueAsNumber || 0)) })} />
                    characters
                  </label>
                  {codes.length > 0 && <button class="btn small" onClick={() => set({ blink: codes.length })}>All</button>}
                  <button class="btn small" onClick={() => set({ blink: 0 })}>None</button>
                </div>
                <div class="row" style="margin-top:10px">
                  <label class="check"><input type="checkbox" checked={s.shift < 0} onChange={(e) => set({ shift: e.currentTarget.checked ? -1 : codes.length })} />Centred</label>
                  {s.shift >= 0 && <>
                    <span class="muted">right</span>
                    <input type="range" min="0" max={Math.max(16, codes.length * 2)} value={s.shift} onInput={(e) => set({ shift: e.currentTarget.valueAsNumber })} />
                    <span class="muted">left</span>
                  </>}
                </div>
                {s.entrance >= 0 && <p class="hint" style="margin-top:6px">The entrance plays first, then the globe switches to still text.</p>}
              </div>
            )}
          </div>

          <div class="actions">
            <div class="row">
              <button class="btn primary" disabled={busy || !!compileError} onClick={() => send(false)}>Send to globe ▶</button>
              <label class="check"><input type="checkbox" checked={s.live} onChange={(e) => set({ live: e.currentTarget.checked })} />Live</label>
              <span style="flex:1" />
              <button class="btn" onClick={() => clearGlobe().then(() => setStatus({ ok: true, msg: 'Globe cleared' }), (e) => setStatus({ ok: false, msg: e.message }))}>Clear globe</button>
              <button class="btn" onClick={() => resumeClock().then(() => setStatus({ ok: true, msg: 'Clock resumed' }), (e) => setStatus({ ok: false, msg: e.message }))}>Resume clock</button>
            </div>
            {compileError && <div class="toast bad">{compileError}</div>}
            {status && <div class={`toast ${status.ok ? 'ok' : 'bad'}`}>{status.msg}</div>}
          </div>
        </div>
      </div>
    </div>
  );
}
