import { useEffect, useMemo, useState } from 'preact/hooks';
import { encodeText, TEXT_MAX } from '../lib/protocol.js';
import { makeScene } from '../lib/sim.js';
import * as api from '../lib/api.js';
import { Screen } from '../components/Screen.jsx';
import { WidthMeter, EFFECTS } from '../components/FrameEditor.jsx';

const MODE_LABEL = { playlist: 'Playing the playlist', scene: 'Playing a scene once', manual: 'Showing text from the designer or control page' };

function NowShowing({ toast }) {
  const [now, setNow] = useState(null);
  const [err, setErr] = useState(null);
  useEffect(() => {
    let stop = false, t = 0;
    const poll = async () => {
      try { const n = await api.getNow(); if (!stop) { setNow(n); setErr(null); } }
      catch (e) { if (!stop) setErr(e.message); }
      if (!stop) t = setTimeout(poll, 1500);
    };
    poll();
    return () => { stop = true; clearTimeout(t); };
  }, []);
  const text = now && now.text ? now.text : '';
  // A still picture of what is on the globe (the text in the front window).
  const scene = useMemo(() => makeScene(null, { codes: encodeText(text).codes.slice(0, TEXT_MAX), style: { entrance: -1, after: 'hold', blink: 0, shift: -1 } }, EFFECTS), [text]);
  return (
    <div class="section">
      <h2>Now showing</h2>
      {err && <p class="toast bad">Can't reach the globe: {err}</p>}
      <Screen scene={scene} runKey={0} holdMs={1000} />
      {now && (
        <div class="row" style="margin-top:8px">
          <span><b>{MODE_LABEL[now.mode] || now.mode}</b>
            {now.name && <> · {now.name}{now.frames > 1 ? ` (frame ${now.frame + 1} of ${now.frames})` : ''}</>}
            {now.itemRemaining !== undefined && <span class="muted"> · next in {now.itemRemaining} s</span>}
          </span>
          <span style="flex:1" />
          {now.mode !== 'playlist' && <button class="btn primary" onClick={() => api.resume().then(() => toast(true, 'Playlist resumed'), (e) => toast(false, e.message))}>Resume playlist</button>}
          <button class="btn" onClick={() => api.clearGlobe().then(() => toast(true, 'Globe cleared'), (e) => toast(false, e.message))}>Clear</button>
        </div>
      )}
    </div>
  );
}

function Playlist({ scenes, toast }) {
  const [items, setItems] = useState(null);
  const [saved, setSaved] = useState('');
  useEffect(() => {
    api.getPlaylist().then((p) => { setItems(p.items || []); setSaved(JSON.stringify(p.items || [])); }, (e) => toast(false, e.message));
  }, []);
  if (!items) return <div class="section"><h2>Playlist</h2><p class="muted">Loading…</p></div>;
  const dirty = JSON.stringify(items) !== saved;
  const upd = (i, patch) => setItems(items.map((it, k) => (k === i ? { ...it, ...patch } : it)));
  const move = (i, d) => { const j = i + d; if (j < 0 || j >= items.length) return; const n = [...items]; [n[i], n[j]] = [n[j], n[i]]; setItems(n); };
  const total = items.reduce((a, it) => a + (it.seconds || 0), 0);
  const save = async () => {
    try { await api.savePlaylist(items); setSaved(JSON.stringify(items)); toast(true, 'Playlist saved; the globe is playing it'); }
    catch (e) { toast(false, `Could not save: ${e.message}`); }
  };
  const known = new Set(scenes.map((s) => s.id));
  return (
    <div class="section">
      <h2>Playlist</h2>
      <p class="hint">What the globe shows when nothing else is up. It runs on the globe, so your PC can be off.</p>
      <div class="plist">
        {items.map((it, i) => (
          <div class="row prow" key={i}>
            <span class="muted mono">{i + 1}</span>
            <select class="select" value={it.scene} onChange={(e) => upd(i, { scene: e.currentTarget.value })} aria-label="Scene">
              {!known.has(it.scene) && <option value={it.scene}>{it.scene} (missing)</option>}
              {scenes.map((s) => <option key={s.id} value={s.id}>{s.name}</option>)}
            </select>
            <label class="row">for <input type="number" min="0" max="86400" value={it.seconds}
              onInput={(e) => upd(i, { seconds: Math.max(0, e.currentTarget.valueAsNumber || 0) })} /> s</label>
            <button class="btn icon" onClick={() => move(i, -1)} disabled={i === 0} aria-label="Move up">↑</button>
            <button class="btn icon" onClick={() => move(i, 1)} disabled={i === items.length - 1} aria-label="Move down">↓</button>
            <button class="btn icon" onClick={() => setItems(items.filter((_, k) => k !== i))} disabled={items.length < 2} aria-label="Remove">✕</button>
          </div>
        ))}
      </div>
      <div class="row" style="margin-top:8px">
        <button class="btn" onClick={() => setItems([...items, { scene: scenes[0] ? scenes[0].id : 'clock', seconds: 10 }])} disabled={items.length >= 24}>+ Add scene</button>
        <span class="muted small">{items.length > 1 ? `One round takes ${total} s` : 'A single scene stays on until something else is shown'}
          {items.length > 1 && items.some((it) => !it.seconds) ? ' · an item set to 0 s never hands over to the next' : ''}</span>
        <span style="flex:1" />
        <button class="btn primary" onClick={save} disabled={!dirty}>{dirty ? 'Save playlist' : 'Saved ✓'}</button>
      </div>
    </div>
  );
}

const LOOKS = [
  { key: 'scroll', label: 'Scroll', tip: 'Slides in, then circles the globe', frame: { entrance: 0, after: 'spin' } },
  { key: 'still', label: 'Still', tip: 'Rises into the front window and stays', frame: { entrance: 16, after: 'hold' } },
  { key: 'flash', label: 'Flashing', tip: 'Stays in front and blinks', frame: { entrance: 37, after: 'flash' } },
  { key: 'alert', label: 'Alert ✉', tip: 'Envelope in front, whole message blinks', frame: { entrance: 37, after: 'flash' }, prefix: '✉ ' },
];
const DURATIONS = [[30, '30 s'], [60, '1 min'], [300, '5 min'], [900, '15 min'], [0, 'Until I resume']];

function QuickMessage({ toast }) {
  const [text, setText] = useState('DINNER IS READY');
  const [look, setLook] = useState('flash');
  const [secs, setSecs] = useState(60);
  const L = LOOKS.find((l) => l.key === look);
  const full = (L.prefix || '') + text;
  const codes = encodeText(full).codes.slice(0, TEXT_MAX);
  const preview = useMemo(() => makeScene({ codes: encodeText('12:00').codes, style: { entrance: -1, after: 'spin' } },
    { codes, style: { blink: 0, shift: -1, ...L.frame } }, EFFECTS), [full, look]);
  const send = async () => {
    const frame = { text: full, blink: 0, shift: -1, seconds: 0, ...L.frame };
    try {
      if (secs) { await api.playFrames([frame], secs); toast(true, `Showing for ${DURATIONS.find((d) => d[0] === secs)[1]}, then back to the playlist`); }
      else { await api.sendFrame({ text: full, entrance: L.frame.entrance, after: L.frame.after }); toast(true, 'Showing until you resume the playlist'); }
    } catch (e) { toast(false, `Could not send: ${e.message}`); }
  };
  return (
    <div class="section">
      <h2>Quick message</h2>
      <input class="big" type="text" value={text} maxLength={60} onInput={(e) => setText(e.currentTarget.value)}
        onKeyDown={(e) => { if (e.key === 'Enter') send(); }} aria-label="Message" />
      <WidthMeter codes={codes} still={L.frame.after !== 'spin'} />
      <div class="row" style="margin-top:10px">
        <span class="muted" style="width:70px">Look</span>
        <div class="chips">{LOOKS.map((l) => <button key={l.key} class="chip" title={l.tip} aria-pressed={look === l.key} onClick={() => setLook(l.key)}>{l.label}</button>)}</div>
      </div>
      <div class="row" style="margin-top:8px">
        <span class="muted" style="width:70px">Show for</span>
        <div class="chips">{DURATIONS.map(([v, lbl]) => <button key={v} class="chip" aria-pressed={secs === v} onClick={() => setSecs(v)}>{lbl}</button>)}</div>
      </div>
      <div style="margin-top:12px"><Screen scene={preview} runKey={look} holdMs={4000} badge="preview" /></div>
      <div class="row" style="margin-top:10px">
        <span class="muted small">Want a different transition or several frames? Make a scene in the <a href="#/designer">Designer</a>.</span>
        <span style="flex:1" />
        <button class="btn primary" onClick={send} disabled={!text.trim()}>Show on globe ▶</button>
      </div>
    </div>
  );
}

export function Dashboard({ toast }) {
  const [scenes, setScenes] = useState([]);
  useEffect(() => { api.getScenes().then(setScenes, () => setScenes([])); }, []);
  return (
    <div class="layout dash">
      <div>
        <NowShowing toast={toast} />
        <QuickMessage toast={toast} />
      </div>
      <div>
        <Playlist scenes={scenes} toast={toast} />
        <div class="section">
          <h2>Scenes</h2>
          {scenes.filter((s) => s.id !== 'quick').map((s) => (
            <div class="row prow" key={s.id}>
              <span style="flex:1">{s.name} <span class="muted small">{s.frames} frame{s.frames === 1 ? '' : 's'}{s.builtin ? ' · built in' : ''}</span></span>
              <button class="btn small" onClick={() => api.playScene(s.id, 0).then(() => toast(true, `Playing “${s.name}” once`), (e) => toast(false, e.message))}>Play now</button>
              <a class="btn small" href={`#/designer/${s.id}`}>Edit</a>
            </div>
          ))}
          <div class="row" style="margin-top:8px"><a class="btn" href="#/designer">+ New scene</a></div>
        </div>
      </div>
    </div>
  );
}
