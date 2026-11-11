import { useEffect, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { fillValues, setVars, setLocation } from '../lib/values.js';
import { VALUES, STATUS, weatherScene, ago } from '../lib/feeds/weather.js';
import { BUILT_IN, loadRotation, setInRotation } from '../lib/rotation.js';
import { Switch, Choice, Row, LocationPicker, hasLocation } from '../components/Common.jsx';
import { Mini } from '../components/Mini.jsx';

// Weather: one switch puts it on the globe. The globe fetches it itself
// (feed_weather.c); this page only chooses where, in which units, and shows
// what the globe holds now.

const UNIT = { temp: '°', feels: '°', hi: '°', lo: '°', thi: '°', tlo: '°', humid: '%', rain: '%' };

export function Weather({ toast }) {
  const [settings, setSettings] = useState(null);
  const [values, setValues] = useState(null);
  const [shown, setShown] = useState(null);         // in the rotation? null = don't know yet

  useEffect(() => {
    api.getSettings().then((s) => { setSettings(s); if (hasLocation(s)) setLocation({ lat: s.lat, lon: s.lon }); }, () => setSettings({}));
    loadRotation().then((r) => setShown(r.items.some((it) => it.scene === 'weather')), () => {});
    return api.subscribeValues((v) => { setValues(v); setVars(v); });
  }, []);

  const feedOn = !settings || settings.weather !== false;
  const units = settings && settings.units === 'c' ? 'c' : 'f';
  const hasLoc = hasLocation(settings);
  const status = values ? values.wx_status : undefined;
  const st = STATUS[status];
  const on = feedOn && shown === true;

  const save = async (patch, msg) => {
    try { await api.saveSettings(patch); setSettings((s) => ({ ...s, ...patch })); if (msg) toast(true, msg); return true; }
    catch (e) { toast(false, `Could not save: ${e.message}`); return false; }
  };
  const toggle = async (next) => {
    try {
      if (next && !feedOn && !(await save({ weather: true }))) return;
      await setInRotation('weather', next);
      setShown(next);
      toast(true, next ? (hasLoc ? 'Weather is on the globe, between your other things' : 'Weather is on. Choose a location below and it starts') : 'Weather taken off the globe');
    } catch (e) { toast(false, e.message); }
  };

  const sc = weatherScene();
  const unitOf = (name) => (name === 'wind' ? (units === 'c' ? 'km/h' : 'mph') : UNIT[name] === '°' ? `°${units.toUpperCase()}` : UNIT[name]);

  return (
    <div class="page">
      <section class="card">
        <div class="feature-head">
          <div>
            <h2>Weather</h2>
            <p class="muted">Temperature, sky and forecast for where the globe is. It updates by itself every 15 minutes.</p>
          </div>
          <label class="feature-switch"><span>{on ? 'On the globe' : 'Off'}</span><Switch on={on} onChange={toggle} label="Show weather on the globe" disabled={shown === null} /></label>
        </div>

        {on && !hasLoc && <div class="problem">Choose the globe’s location below and the weather starts straight away.</div>}
        {on && hasLoc && st && st.why && status !== 'OK' && status !== 'NO LOCATION' && <div class="problem">{st.why}</div>}
        {values && values.weather && feedOn && (
          <div class="now-box">
            <Mini frame={{ text: values.weather, after: 'hold', shift: -1 }} height={34} />
            <div>
              <b class="mono">{values.weather}</b>
              <span class="muted small">
                {values.hi !== undefined && `High ${values.hi}, low ${values.lo}. `}
                {values.wx_updated && `Updated ${ago(values.wx_updated)}.`}
              </span>
            </div>
          </div>
        )}

        <Row label="Location" hint="Also used by Planes and the clock’s sunrise">
          <LocationPicker settings={settings} toast={toast} onSaved={(p) => setSettings((s) => ({ ...s, ...p }))} />
        </Row>
        <Row label="Units">
          <Choice label="Units" value={units} options={[['f', '°F and mph'], ['c', '°C and km/h']]} onPick={(v) => save({ units: v }, v === 'f' ? 'Fahrenheit and mph' : 'Celsius and km/h')} />
        </Row>
        <Row label="What it shows" hint={`${BUILT_IN.weather.seconds} s each round`}>
          <ol class="recipe small">{sc.frames.map((f, i) => <li key={i}><span class="mono">{fillValues(f.text, undefined, undefined, true)}</span></li>)}</ol>
          {shown && <div class="row" style="margin-top:8px">
            <a class="btn small" href="#/scenes/weather">Change what it shows</a>
            <button class="btn small" onClick={() => api.playScene('weather', 0).then(() => toast(true, 'Playing the weather once'), (e) => toast(false, e.message))}>Play it now</button>
          </div>}
        </Row>
      </section>

      <details class="card more">
        <summary>Use weather in your own scenes</summary>
        <p class="muted small">Write any of these in a scene’s text. They are also in the “Insert live value” menu of the scene editor.</p>
        <table class="values">
          <thead><tr><th>Value</th><th>Now</th><th>Write</th></tr></thead>
          <tbody>
            {VALUES.map((v) => (
              <tr key={v.name}>
                <td>{v.label.replace(/ \(.*\)$/, '')}{unitOf(v.name) ? <span class="muted"> ({unitOf(v.name)})</span> : null}</td>
                <td>{values && values[v.name] !== undefined ? <b>{values[v.name]}</b> : <span class="muted">–</span>}</td>
                <td><code>{`{var:${v.name}}`}</code></td>
              </tr>
            ))}
          </tbody>
        </table>
        <p class="muted small">Weather from Open-Meteo (free, no sign-up). It gets the globe’s location.</p>
      </details>
    </div>
  );
}
