import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { encodeText, TEXT_MAX } from '../lib/protocol.js';
import { makeScene } from '../lib/sim.js';
import { cleanScene, toStored, slug, messageFrame, sceneSim, TEMPLATES } from '../lib/scenes.js';
import * as api from '../lib/api.js';
import { Screen } from '../components/Screen.jsx';
import { Mini } from '../components/Mini.jsx';
import { WidthMeter, EFFECTS, animName } from '../components/FrameEditor.jsx';
import { MessageRows, MONTHS } from '../components/MessageRows.jsx';
import { READY_ROWS } from '../lib/clockstyles.js';
import { fillValues, setVars } from '../lib/values.js';
import { BUILT_IN, HIDDEN, sceneSeconds, setInRotation, setRotationSeconds, swapInRotation, withSwapped, fitRotationSeconds,
  deleteScene, removeTimer, freshId, showTime } from '../lib/rotation.js';
import { weatherScene } from '../lib/feeds/weather.js';
import { Switch } from '../components/Common.jsx';
import { TimerChip } from '../components/WhenItShows.jsx';

// Home. One list is the whole story: what the globe shows, in what order and
// for how long. A message of your own is added, worded, timed, reordered and
// deleted right in that list; the bigger features (clock, weather, planes)
// have a page each and a "Set up" link here.

const MODE_LABEL = { playlist: 'Rotation', scene: 'Playing once', manual: 'Manual text', off: 'Display off' };
const fmtSecs = (s) => (s >= 3600 ? `${Math.round(s / 360) / 10} h` : s >= 60 ? `${Math.floor(s / 60)} min${s % 60 ? ` ${s % 60} s` : ''}` : `${s} s`);
/** A name for the list from a message's words, its live parts ({time}, {var:temp}) left out. */
const plainName = (text) => text.replace(/\{[^{}]*\}/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 39);
/** What each of a scene's messages says right now, for its row in the list (the clock's date and countdown included). */
const partsOf = (scene) => scene.frames.map((f) => fillValues(f.text).trim() || (f.anim ? animName(f.anim) || 'Animation' : '(empty)'));
const plural = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;

/* ---------- now showing (or, while a message is being edited, how it will look) ---------- */

function NowShowing({ now, online, toast, canNext, display, editing, focus = 0, settings }) {
  const off = !!now && now.mode === 'off';
  const text = now && now.text ? now.text : '';
  const live = useMemo(() => makeScene(null, { codes: encodeText(text).codes.slice(0, TEXT_MAX), style: { entrance: -1, after: 'hold', blink: 0, shift: -1 } }, EFFECTS), [text]);
  // the preview of a scene being edited starts from the message in hand, so a new entrance shows at once
  const from = editing ? Math.min(focus, editing.frames.length - 1) : 0;
  const editKey = editing ? `${from}${JSON.stringify(editing.frames)}` : '';
  const sim = useMemo(() => (editing ? sceneSim([...editing.frames.slice(from), ...editing.frames.slice(0, from)], EFFECTS) : null), [editKey]);
  const act = (fn, msg) => fn().then(() => toast(true, msg), (e) => toast(false, e.message));
  const progress = now && now.itemSeconds ? 1 - now.itemRemaining / now.itemSeconds : null;

  if (editing) {
    return (
      <section class="card hero">
        <div class="card-head"><h2>Preview</h2><span class="pill scene">{editing.name || 'Message'}</span></div>
        <Screen scene={sim} runKey={editKey} holdMs={0} badge="preview" />
        <div class="now-meta">
          <span class="muted small">How it will look{editing.frames.length > 1 ? `, starting at message ${from + 1}` : ''} ({fmtSecs(sceneSeconds(editing))} in all). It follows what you change on the right.</span>
        </div>
      </section>
    );
  }
  return (
    <section class="card hero">
      <div class="card-head">
        <h2>Now showing</h2>
        {now && <span class={`pill ${now.mode}`}>{MODE_LABEL[now.mode] || now.mode}</span>}
      </div>
      {!online && <div class="problem">The globe isn't answering. Check it's powered and on the network.</div>}
      {off ? (
        <div class="screen off">
          <b>The display is off</b>
          <span>The globe is dark. Everything below is kept and carries on when you switch it back on.
            {settings && settings.displayOnAt && settings.displayOffAt ? ` The night schedule switches it on at ${showTime(settings.displayOnAt)}.` : ''}</span>
          <button class="btn primary" onClick={() => display.set(true).catch(() => {})}>Switch the display on</button>
        </div>
      ) : <Screen scene={live} runKey={0} holdMs={1000} />}
      {!off && (
        <div class="now-meta">
          <div class="now-what">
            {now && now.mode !== 'manual' && <><b>{now.name}</b>{now.frames > 1 && <span class="muted"> · message {now.frame + 1} of {now.frames}</span>}</>}
            {now && now.mode === 'manual' && <span class="muted">Text you sent. The rotation is paused.</span>}
          </div>
          <div class="row">
            {now && now.mode !== 'playlist' && <button class="btn primary" onClick={() => act(api.resume, 'Back to the rotation')}>Back to the rotation</button>}
            {canNext && <button class="btn" onClick={() => act(api.next, 'Skipped to the next one')} title="Skip to the next one in the list">Next ⏭</button>}
          </div>
        </div>
      )}
      {!off && progress !== null && (
        <div class="progress" title={`Next one in ${now.itemRemaining} s`}>
          {/* the countdown arrives every second over the live link */}
          <div class="bar"><div style={{ width: `${Math.min(100, Math.max(0, progress * 100))}%`, transitionDuration: '1s' }} /></div>
          <span class="muted small">next in {now.itemRemaining} s</span>
        </div>
      )}
    </section>
  );
}

/* ---------- kitchen timer: counts down on the globe, then flashes TIME'S UP ---------- */

const TIMER_PRESETS = [1, 3, 5, 10, 15, 30];
const clockOf = (s) => (s >= 3600 ? `${Math.floor(s / 3600)}:${String(Math.floor(s / 60) % 60).padStart(2, '0')}` : `${Math.floor(s / 60)}`) + `:${String(s % 60).padStart(2, '0')}`;

/** Seconds left, counted down here between the globe's own updates so the figure never sticks. */
function useCountdown(seconds) {
  const [, tick] = useState(0);
  const end = useRef(0);
  if (seconds > 0) { const e = Date.now() + seconds * 1000; if (Math.abs(e - end.current) > 1500) end.current = e; } else end.current = 0;
  const running = seconds > 0;
  useEffect(() => {
    if (!running) return undefined;
    const t = setInterval(() => tick((n) => n + 1), 250);
    return () => clearInterval(t);
  }, [running]);
  return end.current ? Math.max(1, Math.ceil((end.current - Date.now()) / 1000)) : 0;
}

function Timer({ now, toast, off }) {
  const [label, setLabel] = useState('');
  const [mins, setMins] = useState('');
  const left = useCountdown(now && now.timer ? now.timer : 0);
  const up = !!now && now.mode === 'scene' && now.scene === 'timer-done';
  const custom = Math.round(Number(mins) * 60);
  const start = (seconds, msg) => api.startTimer(seconds, label.trim().slice(0, 16)).then(
    () => toast(true, msg || `Timer started: ${clockOf(seconds)}. The globe counts down, then flashes TIME’S UP`), (e) => toast(false, `Could not start the timer: ${e.message}`));
  const stop = (msg) => api.stopTimer().then(() => toast(true, msg), (e) => toast(false, e.message));
  return (
    <section class="card timer">
      <div class="card-head"><h2>Timer</h2><span class="muted small">counts down on the globe, then flashes TIME’S UP</span></div>
      {left > 0 ? (
        <div class="timer-run">
          <b class="mono">{clockOf(left)}</b>
          <span class="muted small">left{label.trim() ? ` · ${label.trim()}` : ''}</span>
          <span style="flex:1" />
          <button class="btn small" onClick={() => start(left + 60, 'One more minute')}>+1 min</button>
          <button class="btn small danger" onClick={() => stop('Timer cancelled')}>Cancel</button>
        </div>
      ) : up ? (
        <div class="timer-run">
          <b>Time’s up</b>
          <span class="muted small">flashing on the globe, back to the rotation by itself in a few seconds</span>
          <span style="flex:1" />
          <button class="btn small primary" onClick={() => stop('Back to the rotation')}>Dismiss</button>
        </div>
      ) : (
        <form class="timer-set" onSubmit={(e) => { e.preventDefault(); if (custom >= 1 && !off) start(custom); }}>
          <div class="chips" role="group" aria-label="Start a timer">
            {TIMER_PRESETS.map((m) => <button key={m} type="button" class="chip" disabled={off} onClick={() => start(m * 60)} title={`Start ${m} minute${m === 1 ? '' : 's'} now`}>{m} min</button>)}
          </div>
          <div class="row">
            <label class="row">or <input type="number" min="0.1" max="1439" step="any" value={mins} placeholder="–" aria-label="Minutes" onInput={(e) => setMins(e.currentTarget.value)} /> min</label>
            <input type="text" class="timer-label" value={label} maxLength={16} spellcheck={false} placeholder="Label, e.g. TEA (optional)" aria-label="Label shown beside the time" onInput={(e) => setLabel(e.currentTarget.value)} />
            <button class="btn small primary" type="submit" disabled={!(custom >= 1) || off}>Start</button>
          </div>
          {off && <span class="muted small">The display is off</span>}
        </form>
      )}
    </section>
  );
}

/* ---------- quick message: something to show right now, once ---------- */

const LOOKS = [
  { key: 'scroll', label: 'Scroll', tip: 'Slides in, then circles the globe', frame: { entrance: 0, after: 'spin' } },
  { key: 'still', label: 'Still', tip: 'Rises into the front window and stays', frame: { entrance: 16, after: 'hold' } },
  { key: 'flash', label: 'Flashing', tip: 'Stays in front and blinks', frame: { entrance: 37, after: 'flash' } },
  { key: 'alert', label: 'Alert ✉', tip: 'Envelope in front, whole message blinks', frame: { entrance: 37, after: 'flash' }, prefix: '✉ ' },
];
const DURATIONS = [[30, '30 s'], [60, '1 min'], [300, '5 min'], [900, '15 min'], [0, 'Until I change it']];

function QuickMessage({ toast, off }) {
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
    if (!text.trim() || off) return;
    const frame = { text: full, blink: 0, shift: -1, seconds: 0, ...L.frame };
    try {
      if (secs) { await api.playFrames([frame], secs); toast(true, `Showing for ${DURATIONS.find((d) => d[0] === secs)[1]}, then back to the rotation`); }
      else { await api.sendFrame({ text: full, entrance: L.frame.entrance, after: L.frame.after }); toast(true, 'Showing until you go back to the rotation'); }
    } catch (e) { toast(false, `Could not send: ${e.message}`); }
  };
  return (
    <section class="card">
      <div class="card-head"><h2>Quick message</h2><span class="muted small">shown right now, once, over the rotation</span></div>
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
        {off && <span class="muted small">The display is off</span>}
        <button class="btn primary" onClick={send} disabled={!text.trim() || off}>Show on globe</button>
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
    <label class={`psecs${draft !== null ? ' saving' : ''}`} title={`How long ${name} stays up each turn`}>
      <input type="number" min="1" max="86400" value={draft !== null ? draft : value} aria-label={`Seconds for ${name}`}
        onInput={change} onBlur={() => { if (draft !== null && !(Math.round(Number(draft)) >= 1)) setDraft(null); }} /> s
    </label>
  );
}

/** One of your own scenes, opened in the list: its messages, each with its words, entrance, what it does and its time. */
function EditPanel({ scene, saveState, focus, onFocus, onChange, onDelete, onMore, toast }) {
  const n = scene.frames.length;
  const tryIt = () => api.playFrames(toStored(scene).frames, 0).then(
    () => toast(true, 'Playing it once on the globe, then the globe carries on'), (e) => toast(false, e.message));
  return (
    <div class="turn-edit">
      <MessageRows frames={scene.frames} sel={n > 1 ? Math.min(focus, n - 1) : -1} onSelect={onFocus}
        onChange={(frames, i) => { onFocus(i); onChange({ ...scene, frames }); }} />
      <p class="muted small edit-hint">
        {n > 1 ? `These ${n} play one after another, ${fmtSecs(sceneSeconds(scene))} in all, then the globe moves on to the next thing in the list.`
          : 'To show something else after it, add a message here, or add another one to the list below.'}
      </p>
      <label class="edit-name"><span class="muted small">Name in the list</span>
        <input type="text" value={scene.name} maxLength={39} onInput={(e) => onChange({ ...scene, name: e.currentTarget.value })} aria-label="Name in the list" />
      </label>
      <div class="edit-foot">
        <button class="btn small" onClick={tryIt} title="Play it once on the globe now">▶ Try it on the globe</button>
        <a class="btn small" href={`#/scenes/${scene.id}`} onClick={onMore} title="Every transition with a preview, animation settings, live values such as the time or weather, set times">More options</a>
        <span class={`save-state${saveState === 'failed' ? ' dirty' : ''}`} role="status">{saveState === 'saving' ? 'Saving…' : saveState === 'saved' ? 'Saved on the globe' : saveState === 'failed' ? 'Not saved' : ''}</span>
        <span style="flex:1" />
        <button class="btn small danger" onClick={onDelete}>Delete</button>
      </div>
    </div>
  );
}

function TurnRow({ row, item, pos, many, playing, frame, parts, at, secs, open, onOpen, onToggle, onSeconds, onUp, onDown, toast, children }) {
  const on = !!item;
  const play = () => api.playScene(row.id, 0).then(() => toast(true, `Playing ${row.name} once`), (e) => toast(false, e.message));
  return (
    <li class={`turn${playing ? ' playing' : ''}${open ? ' open' : ''}`}>
      <Switch on={on} onChange={onToggle} label={`Show ${row.name} on the globe`} />
      <Mini frame={frame} />
      <div class="turn-main">
        <b>{pos ? <span class="pos">{pos}</span> : null}{row.name}</b>
        {parts ? (
          <span class="parts" aria-label={`${row.name} shows, in order`}>
            {parts.slice(0, 6).map((p, k) => <span key={k} class={k === at ? 'on' : ''} title={k === at ? 'On the globe now' : `Part ${k + 1} of ${parts.length}`}>{p}</span>)}
            {parts.length > 6 && <span>+{parts.length - 6} more</span>}
          </span>
        ) : <span class="muted small">{playing ? 'showing now' : row.note}</span>}
      </div>
      {row.mine ? <span class="turn-secs muted small">{secs ? fmtSecs(secs) : ''}</span>
        : on && many ? <Seconds value={item.seconds} name={row.name} onSave={onSeconds} /> : <span />}
      <div class="turn-tools">
        {on && many && (
          <span class="updown">
            <button type="button" disabled={!onUp} onClick={onUp} title="Earlier in the order" aria-label={`Move ${row.name} earlier`}>▲</button>
            <button type="button" disabled={!onDown} onClick={onDown} title="Later in the order" aria-label={`Move ${row.name} later`}>▼</button>
          </span>
        )}
        {row.exists && <button class="btn small icon" title="Play it once now" aria-label={`Play ${row.name} once now`} onClick={play}>▶</button>}
        {row.mine ? <button class="btn small" aria-expanded={!!open} onClick={onOpen}>{open ? 'Close' : 'Edit'}</button>
          : row.page ? <a class="btn small" href={row.page}>Set up</a> : null}
      </div>
      {children}
    </li>
  );
}

// Ready-made starting points for the add row (the clock ones are the Clock page's job)
const READY = TEMPLATES.filter((t) => !['blank', 'clock', 'clockdate'].includes(t.key))
  .map((t) => { const n = t.name.replace(/^Animation: /, ''); return { ...t, name: n[0].toUpperCase() + n.slice(1) }; });

/** The last row of the list: type a message (or name a day to count down to, or pick a ready-made one), and it has a turn. */
function AddMessage({ onAdd, onReady }) {
  const [kind, setKind] = useState('message');
  const [text, setText] = useState('');
  const [label, setLabel] = useState('');
  const [month, setMonth] = useState(12);
  const [day, setDay] = useState(25);
  const [busy, setBusy] = useState(false);
  const p2 = (n) => String(n).padStart(2, '0');
  const countdown = `${label.trim()} {countdown:${p2(month)}-${p2(Math.min(31, Math.max(1, day || 1)))}}`;
  const submit = async (e) => {
    e.preventDefault();
    const t = kind === 'message' ? text.trim() : label.trim();
    if (!t || busy) return;
    setBusy(true);
    try {
      const ok = kind === 'message' ? await onAdd(t) : await onAdd(countdown, `${t} countdown`);
      if (ok) { setText(''); setLabel(''); setKind('message'); }
    } finally { setBusy(false); }
  };
  return (
    <li class="turn new">
      {kind === 'message' ? (
        <form class="add-msg" onSubmit={submit}>
          <span class="plus" aria-hidden="true">+</span>
          <input type="text" value={text} maxLength={95} spellcheck={false} placeholder="Add a message: type it and press Enter"
            aria-label="Add a message to the list" onInput={(e) => setText(e.currentTarget.value)} />
          <button class="btn small primary" type="submit" disabled={!text.trim() || busy}>Add</button>
        </form>
      ) : (
        <form class="add-msg" onSubmit={submit}>
          <span class="plus" aria-hidden="true">+</span>
          <input type="text" value={label} maxLength={16} spellcheck={false} placeholder="Counting down to what? XMAS, BDAY…"
            aria-label="What the countdown is for" ref={(el) => { if (el && !el.dataset.seen) { el.dataset.seen = '1'; el.focus(); } }} onInput={(e) => setLabel(e.currentTarget.value)} />
          <select class="select" value={month} onChange={(e) => setMonth(+e.currentTarget.value)} aria-label="Month">
            {MONTHS.map((m, k) => <option key={m} value={k + 1}>{m}</option>)}
          </select>
          <input type="number" min="1" max="31" step="1" value={day} aria-label="Day of the month" onInput={(e) => setDay(Math.round(e.currentTarget.valueAsNumber) || 0)} />
          <button class="btn small primary" type="submit" disabled={!label.trim() || !(day >= 1 && day <= 31) || busy}>Add</button>
        </form>
      )}
      <div class="add-more">
        <div class="seg" role="radiogroup" aria-label="What to add">
          <button type="button" role="radio" aria-checked={kind === 'message'} aria-pressed={kind === 'message'} onClick={() => setKind('message')}>Message</button>
          <button type="button" role="radio" aria-checked={kind === 'countdown'} aria-pressed={kind === 'countdown'} onClick={() => setKind('countdown')}>Countdown to a day</button>
        </div>
        {kind === 'countdown'
          ? <span class="muted small">{label.trim() ? <>shows as <b class="mono">{fillValues(countdown)}</b>, then TOMORROW, then TODAY</> : 'every year, so it suits birthdays too'}</span>
          : (
            <select class="select ready" value="" aria-label="Add a ready-made one"
              onChange={(e) => { const t = READY.find((x) => x.key === e.currentTarget.value); e.currentTarget.value = ''; if (t) onReady(t); }}>
              <option value="">or a ready-made one…</option>
              {READY.map((t) => <option key={t.key} value={t.key}>{t.name} · {t.frames.length} message{t.frames.length === 1 ? '' : 's'}</option>)}
            </select>
          )}
      </div>
    </li>
  );
}

function OnTheGlobe({ scenes, details, items, timers, settings, now, toast, open, saveState, focus, onFocus, onOpen, onEdit, onAdd, onReady, onDelete, onMore, onChanged, onItems, onSettings, onTimers }) {
  const have = new Map(scenes.map((s) => [s.id, s]));
  const rows = [
    ...['clock', 'weather', 'plane'].filter((id) => id !== 'plane' || have.has('plane')).map((id) => ({
      id, name: id === 'plane' ? 'Nearest plane' : BUILT_IN[id].name, page: BUILT_IN[id].page, exists: have.has(id) || id === 'clock',
      note: id === 'clock' ? 'the time, styled on the Clock page' : id === 'weather' ? 'temperature, sky and forecast' : 'even when none is close' })),
    ...scenes.filter((s) => !BUILT_IN[s.id]).map((s) => ({ id: s.id, name: s.name, exists: true, mine: true,
      note: plural(details[s.id] ? details[s.id].frames.length : s.frames, 'message') })),
    // ready to switch on; once on they are yours like any other row (and come back here if deleted)
    ...READY_ROWS.filter((r) => !have.has(r.id)).map((r) => ({ id: r.id, name: r.name, exists: false, ready: r,
      note: fillValues(r.frames[0].text) })),
  ];
  const many = items.length > 1;
  const byId = new Map(rows.map((r) => [r.id, r]));
  const onRows = items.map((it) => byId.get(it.scene)).filter(Boolean);     // in the order the globe shows them
  const offRows = rows.filter((r) => !items.some((it) => it.scene === r.id));
  const total = items.reduce((a, it) => a + (it.seconds || 0), 0);
  const watching = !!settings && settings.aircraft === true;

  const toggle = (row) => async (next) => {
    try {
      if (row.id === 'weather' && next && settings && settings.weather === false) { await api.saveSettings({ weather: true }); onSettings({ weather: true }); }
      if (next && row.ready) await api.saveScene(row.id, { name: row.name, frames: row.ready.frames });
      const d = details[row.id] || row.ready;
      onItems(await setInRotation(row.id, next, d ? sceneSeconds(d) : undefined));
      if (next && !row.exists) onChanged();            // a scene was just made: fetch it for its preview
      toast(true, next ? `${row.name} now takes a turn on the globe` : `${row.name} switched off. It is kept below`);
    } catch (e) { toast(false, e.message); }
  };
  const seconds = (row) => async (v) => {
    try { onItems(await setRotationSeconds(row.id, v)); } catch (e) { toast(false, e.message); }
  };
  // The list moves at once; the globe is told behind, and the list goes back if that fails.
  const swap = (a, b) => async () => {
    const before = items;
    onItems(withSwapped(items, a, b));
    try { onItems(await swapInRotation(a, b)); } catch (e) { onItems(before); toast(false, e.message); }
  };
  const frameOf = (row) => (details[row.id] ? details[row.id].frames[0] : row.ready ? row.ready.frames[0] : row.id === 'weather' ? weatherScene().frames[0] : null);
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
  const turnRow = (row, k) => {
    const i = items.findIndex((it) => it.scene === row.id);
    const on = i >= 0;
    const playing = on && now && now.mode === 'playlist' && (many ? now.item === i : now.scene === row.id);
    const d = details[row.id];
    const isOpen = open === row.id && row.mine && !!d;
    const at = playing && d && d.frames[now.frame] ? now.frame : -1;       // which of its messages is on the globe
    return (
      <TurnRow key={row.id} row={row} item={items[i]} pos={on && many ? k + 1 : 0} many={many} playing={playing} toast={toast}
        frame={at >= 0 ? d.frames[at] : frameOf(row)} parts={d && !isOpen ? partsOf(d) : null} at={at}
        secs={d ? sceneSeconds(d) : 0} open={isOpen} onOpen={() => onOpen(isOpen ? null : row.id)}
        onToggle={toggle(row)} onSeconds={seconds(row)}
        onUp={on && k > 0 ? swap(row.id, onRows[k - 1].id) : null} onDown={on && k < onRows.length - 1 ? swap(row.id, onRows[k + 1].id) : null}>
        {isOpen && <EditPanel scene={d} saveState={saveState} toast={toast} focus={focus} onFocus={onFocus} onChange={(s) => onEdit(row.id, s)}
          onDelete={() => onDelete(row)} onMore={(e) => onMore(e, row.id)} />}
      </TurnRow>
    );
  };

  return (
    <section class="card">
      <div class="card-head">
        <h2>On the globe</h2>
        <span class="muted small">{many ? `one round takes ${fmtSecs(total)}` : 'one thing, shown all the time'}</span>
      </div>
      <h3 class="group">{many ? 'Takes turns, in this order' : 'Showing'}</h3>
      <ul class="turns">
        {onRows.map(turnRow)}
        <AddMessage onAdd={onAdd} onReady={onReady} />
      </ul>
      <p class="muted small list-hint">
{many ? '▲ ▼ change the order. ' : ''}The lit part of a row is on the globe now.
      </p>
      {offRows.length > 0 && <>
        <h3 class="group">Switched off</h3>
        <ul class="turns">{offRows.map(turnRow)}</ul>
      </>}
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
          <div class="turn-tools"><a class="btn small" href="#/settings/planes">Set up</a></div>
        </li>
      </ul>
      <h3 class="group">At set times</h3>
      {timers.length ? (
        <div class="timer-chips">
          {timers.map((t) => <TimerChip key={`${t.scene}${t.at}`} timer={t} name={(have.get(t.scene) || {}).name || t.scene}
            onRemove={() => removeTimer(t).then(onTimers, (e) => toast(false, e.message))} />)}
        </div>
      ) : <p class="muted small" style="margin:2px 0 0">Nothing yet. To show a message at a time of day (GOOD MORNING at 7:00), open it with Edit, then More options.</p>}
      <div class="card-foot">
        <span class="muted small" style="flex:1">It runs on the globe itself, so this page can be closed.</span>
      </div>
    </section>
  );
}

export function Dashboard({ toast, now, online, display }) {
  const [scenes, setScenes] = useState([]);
  const [details, setDetails] = useState({});
  const [items, setItems] = useState([{ scene: 'clock', seconds: 0 }]);
  const [settings, setSettings] = useState(null);
  const [timers, setTimers] = useState([]);
  const [open, setOpen] = useState(null);            // the scene being edited in the list
  const [saveState, setSaveState] = useState('');    // '', saving, saved, failed
  const [focus, setFocus] = useState(0);             // which of its messages is in hand (the preview starts there)
  const itemsRef = useRef(items);
  itemsRef.current = items;
  // the globe's own live values (weather, the nearest plane), so the list says what the globe says and not a sample
  const [, setLive] = useState(0);
  useEffect(() => api.subscribeValues((v) => { setVars(v); setLive((n) => n + 1); }), []);
  const waiting = useRef({});                        // id -> {scene, timer}: edits not yet on the globe
  const chain = useRef(Promise.resolve());           // saves go out one at a time, in order

  const refresh = async () => {
    try {
      const list = (await api.getScenes()).filter((s) => !HIDDEN.includes(s.id));
      setScenes(list);
      const p = await api.getPlaylist();
      setItems(p.items && p.items.length ? p.items : [{ scene: 'clock', seconds: 0 }]);
      setTimers(p.timers || []);
      setSettings(await api.getSettings());
      const d = {};
      await Promise.all(list.map(async (s) => { try { d[s.id] = cleanScene(await api.getScene(s.id)); } catch { /* skip */ } }));
      // what is being typed right now wins over what the globe still has
      setDetails((cur) => ({ ...d, ...Object.fromEntries(Object.keys(waiting.current).filter((id) => cur[id]).map((id) => [id, cur[id]])) }));
    } catch { /* offline: the status chip says so */ }
  };
  useEffect(() => { refresh(); }, []);

  // ---- editing in the list: the page changes at once, the globe a moment after you stop ----
  const flush = (id) => {
    const w = waiting.current[id];
    if (!w) return chain.current;
    clearTimeout(w.timer);
    delete waiting.current[id];
    chain.current = chain.current.then(async () => {
      try {
        const scene = { ...w.scene, name: w.scene.name.trim() || 'Message' };
        await api.saveScene(id, toStored(scene));
        const secs = sceneSeconds(scene), it = itemsRef.current.find((x) => x.scene === id);
        if (it && it.seconds !== secs) setItems(await fitRotationSeconds(id, secs));    // its turn is one pass of its messages
        if (!Object.keys(waiting.current).length) setSaveState('saved');
      } catch (e) { setSaveState('failed'); toast(false, `Could not save: ${e.message}`); }
    });
    return chain.current;
  };
  const flushAll = () => { Object.keys(waiting.current).forEach(flush); return chain.current; };
  useEffect(() => () => { flushAll(); }, []);         // leaving the page: what was waiting still goes out
  const edit = (id, next) => {
    // a name that was just the first message's words keeps following them, until you give it one of your own
    const was = details[id], auto = (s) => plainName(s.frames[0].text);
    const scene = was && was.name === auto(was) && next.name === was.name && auto(next) ? { ...next, name: auto(next) } : next;
    setDetails((d) => ({ ...d, [id]: scene }));
    setScenes((list) => list.map((s) => (s.id === id ? { ...s, name: scene.name || 'Message', frames: scene.frames.length } : s)));
    if (waiting.current[id]) clearTimeout(waiting.current[id].timer);
    waiting.current[id] = { scene, timer: setTimeout(() => flush(id), 700) };
    setSaveState('saving');
  };
  const openRow = (id) => { flushAll(); setSaveState(''); setFocus(0); setOpen(id); };
  const more = async (e, id) => {                     // to the full editor, once the globe has what was typed
    e.preventDefault();
    await flushAll();
    location.hash = `#/scenes/${id}`;
  };

  const add = (text, name = plainName(text) || 'Message') => addScene(name, [messageFrame(text)]);
  const addReady = (t) => addScene(t.name, t.frames.map((f) => ({ ...f })));
  const addScene = async (name, frames) => {
    const id = freshId(slug(name), [...scenes.map((s) => s.id), 'new', ...HIDDEN, ...Object.keys(BUILT_IN), ...READY_ROWS.map((r) => r.id)]);
    const scene = { id, name: name.slice(0, 39).trim(), frames };
    try {
      await api.saveScene(id, toStored(scene));
      const next = await setInRotation(id, true, sceneSeconds(scene));
      setScenes((list) => [...list.filter((s) => s.id !== id), { id, name: scene.name, frames: frames.length }]);
      setDetails((d) => ({ ...d, [id]: scene }));
      setItems(next);
      openRow(id);
      toast(true, next.length > 1 ? `Added. It takes its turn after ${plural(next.length - 1, 'other')}; change how it looks right here` : 'Added');
      return true;
    } catch (e) { toast(false, `Could not add it: ${e.message}`); return false; }
  };
  const remove = async (row) => {
    if (!confirm(`Delete “${row.name}” for good?`)) return;
    if (waiting.current[row.id]) { clearTimeout(waiting.current[row.id].timer); delete waiting.current[row.id]; }
    try {
      await chain.current;
      const left = await deleteScene(row.id);
      setItems(left.items);
      setTimers(left.timers);
      setScenes((list) => list.filter((s) => s.id !== row.id));
      setDetails((d) => { const { [row.id]: gone, ...rest } = d; return rest; });
      if (open === row.id) setOpen(null);
      toast(true, `Deleted “${row.name}”`);
    } catch (e) { toast(false, `Could not delete it: ${e.message}`); }
  };

  const off = !!now && now.mode === 'off';
  const canNext = !!now && ((now.mode === 'playlist' && items.length > 1) || now.mode === 'scene');
  const editing = open && details[open] && !BUILT_IN[open] ? details[open] : null;
  return (
    <div class={`dash${editing ? ' editing' : ''}`}>
      <div class="dash-main">
        <NowShowing now={now} online={online} toast={toast} canNext={canNext} display={display} editing={editing} focus={focus} settings={settings} />
        <Timer now={now} toast={toast} off={off} />
        <QuickMessage toast={toast} off={off} />
      </div>
      <div class="dash-side">
        <OnTheGlobe scenes={scenes} details={details} items={items} timers={timers} onTimers={setTimers} settings={settings} now={now} toast={toast}
          open={open} saveState={saveState} focus={focus} onFocus={setFocus} onOpen={openRow} onEdit={edit} onAdd={add} onReady={addReady} onDelete={remove} onMore={more}
          onChanged={refresh} onItems={setItems} onSettings={(patch) => setSettings((s) => ({ ...s, ...patch }))} />
      </div>
    </div>
  );
}
