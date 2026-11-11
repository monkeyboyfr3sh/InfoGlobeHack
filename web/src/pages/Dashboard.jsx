import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { encodeText, TEXT_MAX } from '../lib/protocol.js';
import { makeScene } from '../lib/sim.js';
import { cleanScene } from '../lib/scenes.js';
import * as api from '../lib/api.js';
import { Screen } from '../components/Screen.jsx';
import { Mini } from '../components/Mini.jsx';
import { WidthMeter, EFFECTS } from '../components/FrameEditor.jsx';
import { BUILT_IN, HIDDEN, sceneSeconds, setInRotation, setRotationSeconds } from '../lib/rotation.js';
import { weatherScene } from '../lib/feeds/weather.js';
import { Switch } from '../components/Common.jsx';

const MODE_LABEL = { playlist: 'Rotation', scene: 'Playing once', manual: 'Manual text' };
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
          {now && now.mode === 'manual' && <span class="muted">Text you sent. The rotation is paused.</span>}
        </div>
        <div class="row">
          {now && now.mode !== 'playlist' && <button class="btn primary" onClick={() => act(api.resume, 'Back to the rotation')}>Back to the rotation</button>}
          {canNext && <button class="btn" onClick={() => act(api.next, 'Skipped to the next item')} title="Skip to the next thing">Next ⏭</button>}
          <button class="btn" onClick={() => act(api.clearGlobe, 'Globe cleared')}>Clear</button>
        </div>
      </div>
      {progress !== null && (
        <div class="progress" title={`Next item in ${now.itemRemaining} s`}>
          {/* the countdown arrives every second over the live link */}
          <div class="bar"><div style={{ width: `${Math.min(100, Math.max(0, progress * 100))}%`, transitionDuration: '1s' }} /></div>
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
const DURATIONS = [[30, '30 s'], [60, '1 min'], [300, '5 min'], [900, '15 min'], [0, 'Until I change it']];

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
      if (secs) { await api.playFrames([frame], secs); toast(true, `Showing for ${DURATIONS.find((d) => d[0] === secs)[1]}, then back to the rotation`); }
      else { await api.sendFrame({ text: full, entrance: L.frame.entrance, after: L.frame.after }); toast(true, 'Showing until you go back to the rotation'); }
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

/* ---------- on the globe: what takes turns, and what interrupts ---------- */

/* How long a thing stays up. Typing or stepping changes the box at once; the
 * globe is told once you pause, so stepping from 30 to 40 is one save, not ten. */
function Seconds({ value, name, onSave }) {
  const [draft, setDraft] = useState(null);         // what is in the box while it differs from the globe
  const timer = useRef(0);
  useEffect(() => () => clearTimeout(timer.current), []);
  const change = (e) => {
    const text = e.currentTarget.value;
    setDraft(text);
    clearTimeout(timer.current);
    const v = Math.round(Number(text));
    if (!(v >= 1 && v <= 86400)) return;            // wait for something sensible
    timer.current = setTimeout(async () => { await onSave(v); setDraft(null); }, 700);
  };
  return (
    <label class={`psecs${draft !== null ? ' saving' : ''}`}>
      <input type="number" min="1" max="86400" value={draft !== null ? draft : value} aria-label={`Seconds for ${name}`}
        onInput={change} onBlur={() => { if (draft !== null && !(Math.round(Number(draft)) >= 1)) setDraft(null); }} /> s
    </label>
  );
}

function TurnRow({ row, item, many, playing, frame, onToggle, onSeconds, toast }) {
  const on = !!item;
  return (
    <li class={`turn${playing ? ' playing' : ''}`}>
      <Switch on={on} onChange={onToggle} label={`Show ${row.name} on the globe`} />
      <Mini frame={frame} />
      <div class="turn-main">
        <b>{row.name}</b>
        <span class="muted small">{playing ? 'showing now' : row.note}</span>
      </div>
      {on && many
        ? <Seconds value={item.seconds} name={row.name} onSave={onSeconds} />
        : <span />}
      <div class="row">
        {row.exists && <button class="btn small" title="Play it once now" onClick={() => api.playScene(row.id, 0).then(() => toast(true, `Playing ${row.name} once`), (e) => toast(false, e.message))}>Play</button>}
        <a class="btn small" href={row.page}>{row.builtin ? 'Set up' : 'Edit'}</a>
      </div>
    </li>
  );
}

function OnTheGlobe({ scenes, details, items, settings, now, toast, onChanged, onItems, onSettings }) {
  const have = new Map(scenes.map((s) => [s.id, s]));
  const rows = [
    ...['clock', 'weather'].map((id) => ({ id, name: BUILT_IN[id].name, page: BUILT_IN[id].page, builtin: true, exists: have.has(id) || id === 'clock',
      note: id === 'clock' ? 'time, date and extras' : 'temperature, sky and forecast' })),
    ...scenes.filter((s) => !BUILT_IN[s.id] || s.id === 'plane').map((s) => ({ id: s.id, name: s.id === 'plane' ? 'Nearest plane' : s.name, page: `#/scenes/${s.id}`, exists: true,
      note: s.id === 'plane' ? 'takes a turn even when none is close' : `your scene · ${s.frames} frame${s.frames === 1 ? '' : 's'}` })),
  ];
  const many = items.length > 1;
  const total = items.reduce((a, it) => a + (it.seconds || 0), 0);
  const watching = !!settings && settings.aircraft === true;

  const toggle = (row) => async (next) => {
    try {
      if (row.id === 'weather' && next && settings && settings.weather === false) { await api.saveSettings({ weather: true }); onSettings({ weather: true }); }
      const d = details[row.id];
      onItems(await setInRotation(row.id, next, d ? sceneSeconds(d) : undefined));
      if (next && !row.exists) onChanged();            // a scene was just made: fetch it for its preview
      toast(true, next ? `${row.name} now takes a turn on the globe` : `${row.name} taken off the globe`);
    } catch (e) { toast(false, e.message); }
  };
  const seconds = (row) => async (v) => {
    try { onItems(await setRotationSeconds(row.id, v)); } catch (e) { toast(false, e.message); }
  };
  const watch = async (next) => {
    try {
      const fresh = next && !have.has('plane');
      if (fresh) await api.saveScene('plane', BUILT_IN.plane.make());
      await api.saveSettings({ aircraft: next });
      onSettings({ aircraft: next });
      if (fresh) onChanged();
      toast(true, next ? 'Watching for planes' : 'Stopped watching for planes');
    } catch (e) { toast(false, e.message); }
  };

  return (
    <section class="card">
      <div class="card-head">
        <h2>On the globe</h2>
        <span class="muted small">{many ? `one round: ${fmtSecs(total)}` : 'one thing, shown all the time'}</span>
      </div>
      <h3 class="group">Takes turns</h3>
      <ul class="turns">
        {rows.map((row) => {
          const i = items.findIndex((it) => it.scene === row.id);
          const playing = i >= 0 && now && now.mode === 'playlist' && (many ? now.item === i : now.scene === row.id);
          const d = details[row.id];
          const frame = d ? d.frames[0] : row.id === 'weather' ? weatherScene().frames[0] : null;
          return <TurnRow key={row.id} row={row} item={items[i]} many={many} playing={playing} frame={frame} toast={toast}
            onToggle={toggle(row)} onSeconds={seconds(row)} />;
        })}
      </ul>
      <h3 class="group">Interrupts</h3>
      <ul class="turns">
        <li class="turn">
          <Switch on={watching} onChange={watch} label="Watch for planes overhead" disabled={!settings} />
          <span />
          <div class="turn-main">
            <b>Plane overhead</b>
            <span class="muted small">{watching ? 'cuts in when a plane flies close, then goes back' : 'off'}</span>
          </div>
          <span />
          <div class="row"><a class="btn small" href="#/planes">Set up</a></div>
        </li>
      </ul>
      <div class="card-foot">
        <span class="muted small" style="flex:1">It runs on the globe itself, so this page can be closed.</span>
        <a class="btn" href="#/scenes">+ Make your own scene</a>
      </div>
    </section>
  );
}

export function Dashboard({ toast, now, online }) {
  const [scenes, setScenes] = useState([]);
  const [details, setDetails] = useState({});
  const [items, setItems] = useState([{ scene: 'clock', seconds: 0 }]);
  const [settings, setSettings] = useState(null);
  const refresh = async () => {
    try {
      const list = (await api.getScenes()).filter((s) => !HIDDEN.includes(s.id));
      setScenes(list);
      const p = await api.getPlaylist();
      setItems(p.items && p.items.length ? p.items : [{ scene: 'clock', seconds: 0 }]);
      setSettings(await api.getSettings());
      const d = {};
      await Promise.all(list.map(async (s) => { try { d[s.id] = cleanScene(await api.getScene(s.id)); } catch { /* skip */ } }));
      setDetails(d);
    } catch { /* offline: the status chip says so */ }
  };
  useEffect(() => { refresh(); }, []);
  const canNext = !!now && ((now.mode === 'playlist' && items.length > 1) || now.mode === 'scene');
  return (
    <div class="dash">
      <div class="dash-main">
        <NowShowing now={now} online={online} toast={toast} canNext={canNext} />
        <QuickMessage toast={toast} />
      </div>
      <div class="dash-side">
        <OnTheGlobe scenes={scenes} details={details} items={items} settings={settings} now={now} toast={toast} onChanged={refresh}
          onItems={setItems} onSettings={(patch) => setSettings((s) => ({ ...s, ...patch }))} />
      </div>
    </div>
  );
}
