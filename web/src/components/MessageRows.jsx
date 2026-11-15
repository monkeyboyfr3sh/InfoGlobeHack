import { useEffect, useRef } from 'preact/hooks';
import { EFFECTS, STARTERS } from './FrameEditor.jsx';
import { GROUP_ORDER } from './Gallery.jsx';
import { ANIMS } from '../lib/anim.js';
import { encodeText, TEXT_MAX } from '../lib/protocol.js';
import { textWidth, WINDOW } from '../lib/sim.js';
import { VALUES, worstCase, hasValues, fillValues } from '../lib/values.js';
import { newFrame } from '../lib/scenes.js';

// Every message of a scene at once, one block each: what it says, for how
// long, how it arrives, what it does then, and its place in the order. Home
// and the scene editor both use it, so a message is set up the same way everywhere.

const FX = Object.entries(EFFECTS).map(([id, e]) => ({ id: Number(id), ...e })).filter((e) => e.sameAs === undefined);
const GROUPS = [...GROUP_ORDER, ...new Set(FX.map((e) => e.group).filter((g) => !GROUP_ORDER.includes(g)))];
const LIVE_GROUPS = [...new Set(VALUES.map((v) => v.group))];
const secs = (ms) => `${Math.round(ms / 100) / 10} s`;
const THEN = [['spin', 'circles the globe'], ['hold', 'holds still'], ['flash', 'flashes']];
export const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
const p2 = (n) => String(n).padStart(2, '0');
// a countdown to a day somewhere in the text: {countdown:12-25} or {until:12-25}
const DAY_RE = /\{(countdown|until):(\d{1,2})-(\d{1,2})\}/;
/** The text with its countdown pointed at another day. */
const withDay = (text, month, day) => text.replace(DAY_RE, (m, kind) => `{${kind}:${p2(Math.min(12, Math.max(1, month)))}-${p2(Math.min(31, Math.max(1, day)))}}`);

/** The "Arrives" choice of a frame as one value: none, fx:<effect id> or anim:<type>. */
function arriveValue(f) {
  if (f.anim) return `anim:${f.anim.type}`;
  const id = f.entrance ?? -1;
  if (id < 0) return 'none';
  const e = EFFECTS[String(id)];
  return `fx:${e && e.sameAs !== undefined ? e.sameAs : id}`;       // a twin shows as the effect it repeats
}
function arrivePatch(v, f) {
  if (v === 'none') return { anim: undefined, entrance: null };
  if (v.startsWith('fx:')) return { anim: undefined, entrance: Number(v.slice(3)) };
  const type = v.slice(5);
  return f.anim && f.anim.type === type ? {} : { entrance: null, anim: { type, ...(STARTERS[type] || {}) } };
}

/** Something about this message that will not look right on the globe, or null. */
function trouble(f) {
  if (!f.anim && !f.text.trim()) return 'Empty: the globe shows nothing for this long.';
  const codes = encodeText(worstCase(f.text)).codes;
  if (codes.length > TEXT_MAX) return `Too long: ${TEXT_MAX} characters fit on the globe, this is ${codes.length}.`;
  const fx = !f.anim && (f.entrance ?? -1) >= 0 ? EFFECTS[String(f.entrance)] : null;
  if (fx && f.seconds > 0 && f.seconds * 1000 < fx.durationMs) return `${fx.name} takes ${secs(fx.durationMs)}, so ${f.seconds} s cuts it short.`;
  if (!f.anim && f.after !== 'spin' && textWidth(codes) > WINDOW) return 'Wider than the front window. “Circles the globe” shows all of it.';
  return null;
}

/**
 * frames: the scene's messages. onChange(frames, index) gets the new list and
 * the message the change was about. With onSelect, the block in use is marked
 * (the preview starts from it; the scene editor shows its full options).
 */
export function MessageRows({ frames, onChange, sel = -1, onSelect }) {
  const list = useRef(null);
  const focusNew = useRef(false);
  useEffect(() => {
    if (!focusNew.current) return;
    focusNew.current = false;
    const boxes = list.current ? list.current.querySelectorAll('input.msg-text') : [];
    if (boxes.length) boxes[boxes.length - 1].focus();
  }, [frames.length]);

  const n = frames.length;
  const set = (i, patch) => onChange(frames.map((f, k) => (k === i ? { ...f, ...patch } : f)), i);
  const move = (i, d) => {
    const j = i + d;
    if (j < 0 || j >= n) return;
    const out = [...frames];
    [out[i], out[j]] = [out[j], out[i]];
    onChange(out, j);
  };
  const remove = (i) => { if (n > 1) onChange(frames.filter((_, k) => k !== i), Math.min(i, n - 2)); };
  const add = () => { focusNew.current = true; onChange([...frames, newFrame({ text: '', entrance: 16, after: 'hold', seconds: 6 })], n); };
  const pick = (i) => { if (onSelect && i !== sel) onSelect(i); };
  // Something live ({time}, {var:temp}, a countdown) goes in where the cursor was, with a space before it if it needs one.
  const insertLive = (i, f, e) => {
    const name = e.currentTarget.value;
    e.currentTarget.value = '';
    if (!name) return;
    const box = e.currentTarget.closest('.msg').querySelector('input.msg-text');
    const at = box && typeof box.selectionStart === 'number' ? box.selectionStart : f.text.length;
    const end = box && typeof box.selectionEnd === 'number' ? box.selectionEnd : at;
    const piece = `${at > 0 && f.text[at - 1] !== ' ' ? ' ' : ''}{${name}}`;
    set(i, { text: (f.text.slice(0, at) + piece + f.text.slice(end)).slice(0, 95) });
    requestAnimationFrame(() => { if (box) { box.focus(); box.setSelectionRange(at + piece.length, at + piece.length); } });
  };

  return (
    <div class="msgs">
      <ol class="msg-list" ref={list}>
        {frames.map((f, i) => {
          const warn = trouble(f);
          const day = DAY_RE.exec(f.text);
          const live = hasValues(f.text);
          return (
            <li key={i} class={`msg${i === sel ? ' sel' : ''}`} onFocusIn={() => pick(i)} onClick={() => pick(i)}>
              <span class="msg-n" aria-hidden="true">{i + 1}</span>
              <input class="msg-text" type="text" value={f.text} maxLength={95} spellcheck={false} placeholder="Type what it says"
                aria-label={`Message ${i + 1}`} onInput={(e) => set(i, { text: e.currentTarget.value })} />
              <label class="msg-secs" title="How long this message stays up">for
                <input type="number" min="1" max="3600" step="1" value={f.seconds || ''} placeholder="5" aria-label={`Seconds for message ${i + 1}`}
                  onInput={(e) => set(i, { seconds: Math.max(0, Math.min(3600, Math.round(e.currentTarget.valueAsNumber) || 0)) })} /> s
              </label>
              <button type="button" class="msg-x" disabled={n < 2} onClick={(e) => { e.stopPropagation(); remove(i); }}
                title={n < 2 ? 'The only message' : 'Remove this message'} aria-label={`Remove message ${i + 1}`}>✕</button>
              <div class="msg-opts">
                <label>Arrives
                  <select class="select" value={arriveValue(f)} onChange={(e) => set(i, arrivePatch(e.currentTarget.value, f))} aria-label={`How message ${i + 1} arrives`}>
                    <option value="none">Just appears</option>
                    {GROUPS.map((g) => (
                      <optgroup key={g} label={g}>
                        {FX.filter((e) => e.group === g).map((e) => <option key={e.id} value={`fx:${e.id}`}>{e.name} · {secs(e.durationMs)}</option>)}
                      </optgroup>
                    ))}
                    <optgroup label="Animations">
                      {ANIMS.map((a) => <option key={a.type} value={`anim:${a.type}`}>{a.name}</option>)}
                    </optgroup>
                  </select>
                </label>
                <label>then
                  <select class="select" value={f.after === 'instant' ? 'hold' : f.after} onChange={(e) => set(i, { after: e.currentTarget.value })} aria-label={`What message ${i + 1} does then`}>
                    {THEN.map(([v, l]) => <option key={v} value={v}>{l}</option>)}
                  </select>
                </label>
                <select class="select live-add" value="" onChange={(e) => insertLive(i, f, e)} aria-label={`Add something live to message ${i + 1}`}
                  title="Put something live in the text: the time, the date, the weather, a countdown…">
                  <option value="">+ Live</option>
                  {LIVE_GROUPS.map((g) => (
                    <optgroup key={g} label={g}>{VALUES.filter((v) => v.group === g).map((v) => <option key={v.name} value={v.name}>{v.label}</option>)}</optgroup>
                  ))}
                </select>
              </div>
              {n > 1 && (
                <span class="updown">
                  <button type="button" disabled={i === 0} onClick={(e) => { e.stopPropagation(); move(i, -1); }} title="Earlier" aria-label={`Move message ${i + 1} earlier`}>▲</button>
                  <button type="button" disabled={i === n - 1} onClick={(e) => { e.stopPropagation(); move(i, 1); }} title="Later" aria-label={`Move message ${i + 1} later`}>▼</button>
                </span>
              )}
              {(day || live) && (
                <div class="msg-live">
                  {day && (
                    <label>Counts down to
                      <select class="select" value={Math.min(12, Math.max(1, +day[2]))} aria-label="Month of the day it counts down to"
                        onChange={(e) => set(i, { text: withDay(f.text, +e.currentTarget.value, +day[3]) })}>
                        {MONTHS.map((m, k) => <option key={m} value={k + 1}>{m}</option>)}
                      </select>
                      <input type="number" min="1" max="31" step="1" value={+day[3]} aria-label="Day of the month it counts down to"
                        onInput={(e) => { const v = Math.round(e.currentTarget.valueAsNumber); if (v >= 1 && v <= 31) set(i, { text: withDay(f.text, +day[2], v) }); }} />
                    </label>
                  )}
                  <span class="muted">shows now as <b class="mono">{fillValues(f.text)}</b></span>
                </div>
              )}
              {warn && <div class="msg-note">{warn}</div>}
            </li>
          );
        })}
      </ol>
      <button type="button" class="btn small msg-add" onClick={add} disabled={n >= 24}>+ Add a message after {n === 1 ? 'this one' : 'these'}</button>
    </div>
  );
}
