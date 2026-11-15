import { useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { DAYS, showTime, setInRotation, addTimer, removeTimer } from '../lib/rotation.js';
import { Switch, Choice, Row } from './Common.jsx';

// The three ways a scene gets onto the globe, in one place: it takes a turn in
// the rotation, it cuts in at a set time, or something asks for it right now
// (you, a phone shortcut, a script, home automation).

const LENGTHS = [[0, 'Once through'], [60, '1 min'], [300, '5 min'], [900, '15 min'], [3600, '1 hour']];
const dayWord = (d) => (DAYS.find(([k]) => k === d) || DAYS[0])[1].toLowerCase();
const lengthWord = (s) => (s ? (LENGTHS.find(([v]) => v === s) || [0, `${s} s`])[1] : 'once through');

/** One set time as a removable chip. */
export function TimerChip({ timer, name, onRemove }) {
  return (
    <span class="timer-chip">
      <b>{showTime(timer.at)}</b> {dayWord(timer.days)} · {name ? `${name} · ` : ''}{lengthWord(timer.seconds)}
      {onRemove && <button type="button" aria-label={`Remove the ${showTime(timer.at)} time`} title="Remove" onClick={onRemove}>✕</button>}
    </span>
  );
}

/** A scene's set times: its chips, and a line to add one ("at 7:00 every day for 1 min"). */
export function SetTimes({ scene, timers, onTimers, toast }) {
  const [at, setAt] = useState('07:00');
  const [days, setDays] = useState('all');
  const [length, setLength] = useState(60);
  const mine = timers.filter((t) => t.scene === scene.id);
  const add = async (e) => {
    e.preventDefault();
    try { onTimers(await addTimer({ scene: scene.id, at, days, seconds: length })); toast(true, `“${scene.name}” will show at ${showTime(at)}, ${dayWord(days)}`); }
    catch (err) { toast(false, err.message); }
  };
  const remove = async (t) => {
    try { onTimers(await removeTimer(t)); } catch (err) { toast(false, err.message); }
  };
  return (
    <div class="set-times">
      {mine.length > 0 && <div class="timer-chips">{mine.map((t) => <TimerChip key={t.at} timer={t} onRemove={() => remove(t)} />)}</div>}
      <form class="row timer-add" onSubmit={add}>
        <input type="time" value={at} onInput={(e) => setAt(e.currentTarget.value)} aria-label="Time of day" required />
        <Choice label="Days" value={days} options={DAYS} onPick={setDays} />
        <select class="select" value={length} onChange={(e) => setLength(+e.currentTarget.value)} aria-label="For how long">
          {LENGTHS.map(([v, l]) => <option key={v} value={v}>{v ? `for ${l}` : l}</option>)}
        </select>
        <button class="btn small" type="submit">+ Add time</button>
      </form>
    </div>
  );
}

export function WhenItShows({ scene, seconds, items, timers, onItems, onTimers, toast }) {
  const host = api.GLOBE || location.origin;

  if (!scene.id) {
    return (
      <section class="card when" id="when">
        <div class="card-head"><h2>When it shows</h2></div>
        <p class="muted">Save the scene first. Then you can give it a turn on the globe, set it for a time of day, or play it on demand.</p>
      </section>
    );
  }

  const inRotation = items.some((it) => it.scene === scene.id);
  const toggle = async (next) => {
    try { onItems(await setInRotation(scene.id, next, seconds)); toast(true, next ? `“${scene.name}” now takes a turn on the globe` : `“${scene.name}” taken out of the rotation`); }
    catch (e) { toast(false, e.message); }
  };
  const play = (secs) => api.playScene(scene.id, secs).then(
    () => toast(true, secs ? `Showing “${scene.name}” for ${lengthWord(secs)}, then the globe goes back` : `Playing “${scene.name}” once`),
    (e) => toast(false, e.message));

  return (
    <section class="card when" id="when">
      <div class="card-head"><h2>When it shows</h2><span class="muted small">a scene does nothing until one of these is set</span></div>
      <Row label="Takes a turn" hint="in the rotation, with the clock and your other things">
        <label class="feature-switch"><Switch on={inRotation} onChange={toggle} label="Give this scene a turn on the globe" /><span class="muted">{inRotation ? <>On: it comes round every cycle. Set its place in the order on <a href="#/home">Home</a>.</> : 'Off'}</span></label>
      </Row>
      <Row label="At a set time" hint="it cuts in, then the globe goes back to the rotation">
        <SetTimes scene={scene} timers={timers} onTimers={onTimers} toast={toast} />
      </Row>
      <Row label="Right now" hint="over whatever the globe is showing">
        <div class="row">
          <button class="btn" onClick={() => play(0)}>Play once ▶</button>
          <button class="btn" onClick={() => play(300)}>Keep it up for 5 min</button>
          <button class="btn" onClick={() => play(3600)}>For an hour</button>
        </div>
        <details class="token-more">
          <summary>Trigger it from a phone shortcut, a script or home automation</summary>
          <p class="muted small">Send this and the globe plays the scene once (add <code>"seconds":300</code> to keep it up). A doorbell, a finished build or a button on your phone can all do it.</p>
          <pre class="bytes">{`curl -X POST ${host}/api/v1/play -d '{"scene":"${scene.id}"}'`}</pre>
          <p class="muted small">If you set an access token under Settings, add <code>?token=…</code> to the address.</p>
        </details>
      </Row>
    </section>
  );
}
