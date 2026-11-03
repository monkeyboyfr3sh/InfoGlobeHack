import { useEffect, useMemo, useState } from 'preact/hooks';
import { encodeText, TEXT_MAX } from '../lib/protocol.js';
import { makeScene } from '../lib/sim.js';
import { cleanScene } from '../lib/scenes.js';
import * as api from '../lib/api.js';
import { Screen } from '../components/Screen.jsx';
import { Mini } from '../components/Mini.jsx';
import { WidthMeter, EFFECTS } from '../components/FrameEditor.jsx';

const MODE_LABEL = { playlist: 'Playlist', scene: 'Playing once', manual: 'Manual text' };
const fmtSecs = (s) => (s >= 3600 ? `${Math.round(s / 360) / 10} h` : s >= 60 ? `${Math.floor(s / 60)} min${s % 60 ? ` ${s % 60} s` : ''}` : `${s} s`);

/* ---------- now showing ---------- */

function NowShowing({ now, online, toast, canNext }) {
  const text = now && now.text ? now.text : '';
  const scene = useMemo(() => makeScene(null, { codes: encodeText(text).codes.slice(0, TEXT_MAX), style: { entrance: -1, after: 'hold', blink: 0, shift: -1 } }, EFFECTS), [text]);
  const act = (fn, msg) => fn().then(() => toast(true, msg), (e) => toast(false, e.message));
  const progress = now && now.itemSeconds ? 1 - now.itemRemaining / now.itemSeconds : null;
  return (
    <section class="card hero">
      <div class="card-head">
        <h2>Now showing</h2>
        {now && <span class={`pill ${now.mode}`}>{MODE_LABEL[now.mode] || now.mode}</span>}
      </div>
      {!online && <div class="problem">The globe isn't answering. Check it's powered and on the network.</div>}
      <Screen scene={scene} runKey={0} holdMs={1000} />
      <div class="now-meta">
        <div class="now-what">
          {now && now.mode !== 'manual' && <><b>{now.name}</b>{now.frames > 1 && <span class="muted"> · frame {now.frame + 1} of {now.frames}</span>}</>}
          {now && now.mode === 'manual' && <span class="muted">Text sent from the designer or control page. The playlist is paused.</span>}
        </div>
        <div class="row">
          {now && now.mode !== 'playlist' && <button class="btn primary" onClick={() => act(api.resume, 'Playlist resumed')}>Resume playlist</button>}
          {canNext && <button class="btn" onClick={() => act(api.next, 'Skipped to the next item')} title="Skip to the next playlist item">Next ⏭</button>}
          <button class="btn" onClick={() => act(api.clearGlobe, 'Globe cleared')}>Clear</button>
        </div>
      </div>
      {progress !== null && (
        <div class="progress" title={`Next item in ${now.itemRemaining} s`}>
          <div class="bar"><div style={{ width: `${Math.min(100, Math.max(0, progress * 100))}%` }} /></div>
          <span class="muted small">next in {now.itemRemaining} s</span>
        </div>
      )}
    </section>
  );
}

/* ---------- quick message ---------- */

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
  const [showPreview, setShowPreview] = useState(false);
  const L = LOOKS.find((l) => l.key === look);
  const full = (L.prefix || '') + text;
  const codes = encodeText(full).codes.slice(0, TEXT_MAX);
  const preview = useMemo(() => makeScene({ codes: encodeText('12:00').codes, style: { entrance: -1, after: 'spin' } },
    { codes, style: { blink: 0, shift: -1, ...L.frame } }, EFFECTS), [full, look]);
  const send = async () => {
    if (!text.trim()) return;
    const frame = { text: full, blink: 0, shift: -1, seconds: 0, ...L.frame };
    try {
      if (secs) { await api.playFrames([frame], secs); toast(true, `Showing for ${DURATIONS.find((d) => d[0] === secs)[1]}, then back to the playlist`); }
      else { await api.sendFrame({ text: full, entrance: L.frame.entrance, after: L.frame.after }); toast(true, 'Showing until you resume the playlist'); }
    } catch (e) { toast(false, `Could not send: ${e.message}`); }
  };
  return (
    <section class="card">
      <div class="card-head"><h2>Quick message</h2></div>
      <input class="big" type="text" value={text} maxLength={60} onInput={(e) => setText(e.currentTarget.value)}
        onKeyDown={(e) => { if (e.key === 'Enter') send(); }} aria-label="Message" placeholder="Type a message" />
      <WidthMeter codes={codes} still={L.frame.after !== 'spin'} />
      <div class="form-grid">
        <span class="label">Look</span>
        <div class="seg" role="radiogroup" aria-label="Look">
          {LOOKS.map((l) => <button key={l.key} role="radio" aria-checked={look === l.key} aria-pressed={look === l.key} title={l.tip} onClick={() => setLook(l.key)}>{l.label}</button>)}
        </div>
        <span class="label">Show for</span>
        <div class="seg" role="radiogroup" aria-label="Show for">
          {DURATIONS.map(([v, lbl]) => <button key={v} role="radio" aria-checked={secs === v} aria-pressed={secs === v} onClick={() => setSecs(v)}>{lbl}</button>)}
        </div>
      </div>
      {showPreview && <div style="margin-top:12px"><Screen scene={preview} runKey={look} holdMs={4000} badge="preview" /></div>}
      <div class="card-foot">
        <button class="btn link" onClick={() => setShowPreview(!showPreview)}>{showPreview ? 'Hide preview' : 'Preview'}</button>
        <span style="flex:1" />
        <button class="btn primary" onClick={send} disabled={!text.trim()}>Show on globe</button>
      </div>
    </section>
  );
}

/* ---------- playlist ---------- */

function Playlist({ scenes, details, now, toast, onSaved }) {
  const [items, setItems] = useState(null);
  const [saved, setSaved] = useState('');
  const load = () => api.getPlaylist().then((p) => { setItems(p.items || []); setSaved(JSON.stringify(p.items || [])); }, (e) => toast(false, e.message));
  useEffect(() => { load(); }, []);
  if (!items) return <section class="card"><div class="card-head"><h2>Playlist</h2></div><p class="muted">Loading…</p></section>;
  const dirty = JSON.stringify(items) !== saved;
  const upd = (i, patch) => setItems(items.map((it, k) => (k === i ? { ...it, ...patch } : it)));
  const move = (i, d) => { const j = i + d; if (j < 0 || j >= items.length) return; const n = [...items]; [n[i], n[j]] = [n[j], n[i]]; setItems(n); };
  const total = items.reduce((a, it) => a + (it.seconds || 0), 0);
  const save = async () => {
    const fixed = items.length > 1 ? items.map((it) => ({ ...it, seconds: it.seconds || 10 })) : items;
    try { await api.savePlaylist(fixed); setItems(fixed); setSaved(JSON.stringify(fixed)); onSaved(); toast(true, 'Playlist saved; the globe is playing it'); }
    catch (e) { toast(false, `Could not save: ${e.message}`); }
  };
  const known = new Map(scenes.map((s) => [s.id, s]));
  const playing = (i) => !dirty && now && now.mode === 'playlist' && (items.length === 1 ? now.scene === items[0].scene : now.item === i);
  return (
    <section class="card">
      <div class="card-head">
        <h2>Playlist</h2>
        <span class="muted small">{items.length > 1 ? `one round: ${fmtSecs(total)}` : 'one scene, shown continuously'}</span>
      </div>
      <p class="hint">What the globe shows when nothing else is up. It runs on the globe, so your PC can be off.</p>
      <ol class="plist">
        {items.map((it, i) => (
          <li class={`prow${playing(i) ? ' playing' : ''}`} key={i}>
            <span class="pnum">{playing(i) ? '▶' : i + 1}</span>
            <Mini frame={details[it.scene] && details[it.scene].frames[0]} />
            <div class="pmain">
              <select class="select" value={it.scene} onChange={(e) => upd(i, { scene: e.currentTarget.value })} aria-label="Scene">
                {!known.has(it.scene) && <option value={it.scene}>{it.scene} (missing)</option>}
                {scenes.map((s) => <option key={s.id} value={s.id}>{s.name}</option>)}
              </select>
            </div>
            {items.length > 1 && (
              <label class="psecs">
                <input type="number" min="1" max="86400" value={it.seconds} aria-label="Seconds"
                  onInput={(e) => upd(i, { seconds: Math.max(1, e.currentTarget.valueAsNumber || 1) })} /> s
              </label>
            )}
            <div class="pbtns">
              <button class="btn icon" onClick={() => move(i, -1)} disabled={i === 0} aria-label="Move up" title="Move up">↑</button>
              <button class="btn icon" onClick={() => move(i, 1)} disabled={i === items.length - 1} aria-label="Move down" title="Move down">↓</button>
              <button class="btn icon" onClick={() => setItems(items.filter((_, k) => k !== i))} disabled={items.length < 2} aria-label="Remove" title="Remove">✕</button>
            </div>
          </li>
        ))}
      </ol>
      <div class="card-foot">
        <button class="btn" onClick={() => setItems([...items.map((it) => ({ ...it, seconds: it.seconds || 20 })), { scene: scenes[0] ? scenes[0].id : 'clock', seconds: 10 }])} disabled={items.length >= 24}>+ Add scene</button>
        <span style="flex:1" />
        {dirty && <button class="btn" onClick={load}>Discard</button>}
        <button class="btn primary" onClick={save} disabled={!dirty}>{dirty ? 'Save playlist' : 'Saved'}</button>
      </div>
    </section>
  );
}

/* ---------- scenes ---------- */

function Scenes({ scenes, details, inPlaylist, toast }) {
  return (
    <section class="card">
      <div class="card-head"><h2>Scenes</h2><a class="btn small" href="#/designer">+ New scene</a></div>
      <div class="scene-grid">
        {scenes.map((s) => (
          <div class="scene-card" key={s.id}>
            <Mini frame={details[s.id] && details[s.id].frames[0]} height={34} />
            <div class="scene-info">
              <b>{s.name}</b>
              <span class="muted small">{s.frames} frame{s.frames === 1 ? '' : 's'}{s.builtin ? ' · built in' : ''}{inPlaylist.has(s.id) ? ' · in playlist' : ''}</span>
            </div>
            <div class="row">
              <button class="btn small" onClick={() => api.playScene(s.id, 0).then(() => toast(true, `Playing “${s.name}” once`), (e) => toast(false, e.message))}>Play</button>
              <a class="btn small" href={`#/designer/${s.id}`}>Edit</a>
            </div>
          </div>
        ))}
      </div>
    </section>
  );
}

export function Dashboard({ toast, now, online }) {
  const [scenes, setScenes] = useState([]);
  const [details, setDetails] = useState({});
  const [plist, setPlist] = useState([]);
  const refresh = async () => {
    try {
      const list = (await api.getScenes()).filter((s) => s.id !== 'quick');
      setScenes(list);
      const d = {};
      await Promise.all(list.map(async (s) => { try { d[s.id] = cleanScene(await api.getScene(s.id)); } catch { /* skip */ } }));
      setDetails(d);
      const p = await api.getPlaylist();
      setPlist(p.items || []);
    } catch { /* offline: the status chip says so */ }
  };
  useEffect(() => { refresh(); }, []);
  const inPlaylist = new Set(plist.map((it) => it.scene));
  const canNext = !!now && ((now.mode === 'playlist' && plist.length > 1) || now.mode === 'scene');
  return (
    <div class="dash">
      <div class="dash-main">
        <NowShowing now={now} online={online} toast={toast} canNext={canNext} />
        <QuickMessage toast={toast} />
      </div>
      <div class="dash-side">
        <Playlist scenes={scenes} details={details} now={now} toast={toast} onSaved={refresh} />
        <Scenes scenes={scenes} details={details} inPlaylist={inPlaylist} toast={toast} />
      </div>
    </div>
  );
}
