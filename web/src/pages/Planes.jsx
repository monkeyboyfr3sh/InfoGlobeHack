import { useEffect, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { fillValues, setVars } from '../lib/values.js';
import { VALUES, PLANE_SCENE } from '../lib/feeds/aircraft.js';
import { ensureScene, loadRotation, setInRotation } from '../lib/rotation.js';
import { Switch, Choice, Row, LocationPicker, hasLocation } from '../components/Common.jsx';

// Planes: one switch makes the globe watch the sky (feed_aircraft.c). When a
// plane comes close it interrupts with the "plane" scene; optionally the
// nearest plane also takes a turn in the rotation. Every option saves as you pick it.

const RANGES = [3, 5, 10, 15, 25];
// when a plane interrupts (feed_aircraft.c "aircraftNotice"): it looks about a minute along each plane's track
const NOTICE = [[0, 'Never'], [1, 'Right overhead'], [2, 'I’ll see or hear it'], [3, 'Anything in my sky']];
const NOTICE_HINT = ['', 'Only planes about to pass almost straight over the globe.',
  'Planes about to pass high enough in your sky to spot, or close enough to hear. It goes off about a minute before they get there.',
  'Also planes lower on the horizon or further off: more interruptions.'];
// "UAL123 2.4" + a Unix time, as the globe notes its last alert and its closest approach
const noted = (text, at) => {
  const m = /^(.+) (\d+(?:\.\d+)?)$/.exec(text || ''), t = Number(at);
  if (!m) return null;
  const when = t > 1e9 ? new Date(t * 1000) : null;
  const sameDay = when && when.toDateString() === new Date().toDateString();
  return { who: m[1], nm: m[2], when: when ? `${sameDay ? '' : `${when.toLocaleDateString([], { weekday: 'short' })} `}${when.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' })}` : '' };
};
const CEILINGS = [[0, 'Any height'], [20000, 'Below 20,000 ft'], [10000, 'Below 10,000 ft'], [5000, 'Below 5,000 ft']];
const km = (nm) => Math.round(nm * 1.852);
const num = (v, d) => (Number.isFinite(v) ? v : d);

export function Planes({ toast }) {
  const [settings, setSettings] = useState(null);
  const [vals, setVals] = useState({});
  const [inList, setInList] = useState(null);

  useEffect(() => {
    api.getSettings().then(setSettings, () => setSettings({}));
    loadRotation().then((r) => setInList(r.items.some((it) => it.scene === 'plane')), () => {});
    return api.subscribeValues((v) => { setVals(v || {}); setVars(v); });
  }, []);

  if (!settings) return <div class="page"><section class="card"><h2>Planes</h2><p class="muted">Loading…</p></section></div>;

  const on = settings.aircraft === true;
  const radius = num(settings.aircraftRadius, 10);
  const notice = Number.isFinite(settings.aircraftNotice) ? settings.aircraftNotice : settings.aircraftAlert === 0 ? 0 : 2;
  const lastAlert = noted(vals.plane_alert, vals.plane_alert_at), closest = noted(vals.plane_closest, vals.plane_close_at);
  const alertFt = num(settings.aircraftAlertFt, 0);
  const hasLoc = hasLocation(settings);
  const st = vals.plane_status;
  const n = parseInt(vals.planes, 10);

  const save = async (patch, msg) => {
    try { await api.saveSettings(patch); setSettings((s) => ({ ...s, ...patch })); if (msg) toast(true, msg); return true; }
    catch (e) { toast(false, `Could not save: ${e.message}`); return false; }
  };
  const toggle = async (next) => {
    try {
      if (next) await ensureScene('plane');          // so the overhead alert has something to play
      if (await save({ aircraft: next })) toast(true, next ? (hasLoc ? 'Watching the sky. The first planes show up in a few seconds' : 'On. Choose a location below and it starts') : 'Stopped watching for planes');
    } catch (e) { toast(false, e.message); }
  };
  const toggleList = async (next) => {
    try { await setInRotation('plane', next); setInList(next); toast(true, next ? 'The nearest plane now takes a turn in the rotation' : 'Taken out of the rotation'); }
    catch (e) { toast(false, e.message); }
  };

  return (
    <div class="page">
      <section class="card">
        <div class="feature-head">
          <div>
            <h2>Planes</h2>
            <p class="muted">Shows the aircraft nearest to the globe, like <span class="mono">UAL123 SFO-ORD B738</span>, and can interrupt when one flies over.</p>
          </div>
          <label class="feature-switch"><span>{on ? 'Watching' : 'Off'}</span><Switch on={on} onChange={toggle} label="Watch for planes" /></label>
        </div>

        {on && !hasLoc && <div class="problem">Choose the globe’s location below and it starts looking.</div>}
        {on && hasLoc && st === 'OFFLINE' && <div class="problem">The flight trackers aren’t answering right now. The globe keeps trying.</div>}
        {on && st === 'OK' && (
          <div class="now-box plain">
            <div>
              <b class="mono">{vals.planeline}</b>
              {n > 0 && <span class="small">{vals.planetrip} · {vals.planewhere}</span>}
              <span class="muted small">{n > 0 ? `${n} in the air within ${radius} nm (${km(radius)} km)` : `Nothing in the air within ${radius} nm right now`}</span>
            </div>
          </div>
        )}

        <Row label="Location" hint="Also used by Weather and the clock’s sunrise">
          <LocationPicker settings={settings} toast={toast} onSaved={(p) => setSettings((s) => ({ ...s, ...p }))} />
        </Row>
        <Row label="Look within" hint="nautical miles around the globe">
          <Choice label="Look within" value={radius} options={RANGES.map((r) => [r, `${r} nm`])} onPick={(v) => save({ aircraftRadius: v })} />
        </Row>
        <Row label="Interrupt for" hint="plays the plane scene once, then goes back">
          <Choice label="Interrupt for" value={notice} options={NOTICE} onPick={(v) => save({ aircraftNotice: v })} />
          {notice > 0 && <p class="muted small" style="margin:8px 0 0">{NOTICE_HINT[notice]}{notice > 0 && radius < 10 ? ' “Look within” 10 nm or more gives it time to see fast jets coming.' : ''}</p>}
          {on && notice > 0 && (
            <p class="muted small" style="margin:8px 0 0">
              {lastAlert ? <>Last interrupted {lastAlert.when ? `at ${lastAlert.when} ` : ''}for <b class="mono">{lastAlert.who}</b>, {lastAlert.nm} nm away. </>
                : 'It hasn’t interrupted since the globe last started. '}
              {closest && <>Closest any plane has come: <b class="mono">{closest.who}</b>, {closest.nm} nm{closest.when ? ` (${closest.when})` : ''}.</>}
            </p>
          )}
        </Row>
        {notice > 0 && (
          <Row label="Only interrupt for planes">
            <Choice label="Only interrupt for planes" value={alertFt} options={CEILINGS} onPick={(v) => save({ aircraftAlertFt: v })} />
          </Row>
        )}
        <Row label="Also take a turn in the rotation" hint="the nearest plane, between your other things">
          <Switch on={inList === true} onChange={toggleList} label="Show the nearest plane in the rotation" disabled={inList === null} />
        </Row>
        <Row label="What it shows">
          <ol class="recipe small">{PLANE_SCENE.frames.map((f, i) => <li key={i}><span class="mono">{fillValues(f.text)}</span></li>)}</ol>
          {on && <div class="row" style="margin-top:8px">
            <a class="btn small" href="#/scenes/plane">Change what it shows</a>
            <button class="btn small" onClick={() => api.playScene('plane', 0).then(() => toast(true, 'Playing the plane scene once'), (e) => toast(false, e.message))}>Play it now</button>
          </div>}
        </Row>
      </section>

      <details class="card more">
        <summary>Use planes in your own scenes</summary>
        <p class="muted small">Write any of these in a scene’s text. They are also in the “Insert live value” menu of the scene editor.</p>
        <table class="values left">
          <thead><tr><th>Write</th><th>Now</th><th>What</th></tr></thead>
          <tbody>
            {VALUES.map((v) => (
              <tr key={v.name}><td><code>{`{var:${v.name}}`}</code></td><td class="mono">{vals[v.name] ?? '–'}</td><td class="muted small">{v.label}</td></tr>
            ))}
          </tbody>
        </table>
        <p class="muted small">
          Aircraft from <a href="https://adsb.lol" target="_blank" rel="noopener noreferrer">adsb.lol</a> (ODbL)
          or <a href="https://adsb.fi" target="_blank" rel="noopener noreferrer">adsb.fi</a>, routes
          from <a href="https://www.adsbdb.com" target="_blank" rel="noopener noreferrer">adsbdb</a>. They get the globe’s location, rounded to about 1 km. Routes are often missing; the globe shows one only when the plane is plausibly flying it.
        </p>
      </details>
    </div>
  );
}
