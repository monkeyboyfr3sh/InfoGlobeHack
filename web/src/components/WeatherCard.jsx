import { useEffect, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { fillValues, setVars, setLocation } from '../lib/values.js';
import { VALUES, STATUS, weatherScene, searchPlaces, ago } from '../lib/feeds/weather.js';
import { Mini } from './Mini.jsx';

// Weather card (Data page): turn the globe's weather feed on, pick units and
// the place, see what the globe holds now, and make a ready-made scene.
// The location is the same lat/lon the Clock page uses for sunrise.

const SCENE_ID = 'weather';
const UNIT = { temp: '°', feels: '°', hi: '°', lo: '°', thi: '°', tlo: '°', humid: '%', rain: '%' };

export function WeatherCard({ toast }) {
  const [settings, setSettings] = useState(null);
  const [values, setValues] = useState(null);
  const [query, setQuery] = useState('');
  const [places, setPlaces] = useState(null);           // search results, null = no search yet
  const [searching, setSearching] = useState(false);
  const [coords, setCoords] = useState({ lat: '', lon: '' });
  const [scene, setScene] = useState({ exists: false, listed: false });
  const [busy, setBusy] = useState(false);

  const loadValues = async () => {
    try { const v = await api.getValues(); setValues(v); setVars(v); } catch { /* offline: the status chip says so */ }
  };
  const loadScene = async () => {
    try {
      const [list, p] = await Promise.all([api.getScenes(), api.getPlaylist()]);
      setScene({ exists: list.some((s) => s.id === SCENE_ID), listed: (p.items || []).some((it) => it.scene === SCENE_ID) });
    } catch { /* offline */ }
  };
  useEffect(() => {
    api.getSettings().then((s) => {
      setSettings(s);
      if (Number.isFinite(s.lat) && Number.isFinite(s.lon)) { setCoords({ lat: String(s.lat), lon: String(s.lon) }); setLocation({ lat: s.lat, lon: s.lon }); }
    }, () => setSettings({}));
    loadScene();
    loadValues();
    const t = setInterval(() => { if (!document.hidden) loadValues(); }, 5000);
    return () => clearInterval(t);
  }, []);

  const on = !settings || settings.weather !== false;     // on unless turned off
  const units = settings && settings.units === 'c' ? 'c' : 'f';
  const hasLoc = !!settings && Number.isFinite(settings.lat) && Number.isFinite(settings.lon);
  const place = hasLoc && settings.place && settings.place.lat === settings.lat && settings.place.lon === settings.lon ? settings.place.name : null;
  const status = values ? values.wx_status : undefined;
  const st = STATUS[status] || (values ? { label: 'Waiting', cls: '', why: 'The globe hasn’t reported on the weather yet. It starts a few seconds after its clock has synced.' } : null);

  const save = async (patch, msg) => {
    try {
      await api.saveSettings(patch);
      setSettings((s) => ({ ...s, ...patch }));
      toast(true, msg);
      setTimeout(loadValues, 3000);                      // the globe re-fetches within a second or two
      return true;
    } catch (e) { toast(false, `Could not save: ${e.message}`); return false; }
  };

  const search = async (e) => {
    e.preventDefault();
    if (!query.trim()) return;
    setSearching(true);
    try { setPlaces(await searchPlaces(query.trim(), (navigator.language || 'en').slice(0, 2))); }
    catch { toast(false, 'Couldn’t reach the place search (this browser needs internet). Enter the coordinates instead.'); }
    finally { setSearching(false); }
  };
  const pick = async (p) => {
    if (await save({ lat: p.lat, lon: p.lon, place: p }, `Location set to ${p.name}`)) {
      setCoords({ lat: String(p.lat), lon: String(p.lon) });
      setLocation({ lat: p.lat, lon: p.lon });
      setPlaces(null);
      setQuery('');
    }
  };
  const saveCoords = async () => {
    const lat = parseFloat(coords.lat), lon = parseFloat(coords.lon);
    if (!Number.isFinite(lat) || !Number.isFinite(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) { toast(false, 'Latitude −90…90 and longitude −180…180, e.g. 41.88 and −87.63'); return; }
    if (await save({ lat, lon }, 'Location saved on the globe')) setLocation({ lat, lon });
  };

  const sc = weatherScene();
  const roundSecs = sc.frames.reduce((a, f) => a + f.seconds, 0);
  const makeScene = async () => {
    if (scene.exists && !confirm('Replace your weather scene with the ready-made one?')) return;
    setBusy(true);
    try {
      await api.saveScene(SCENE_ID, sc);
      setScene((s) => ({ ...s, exists: true }));
      toast(true, scene.listed ? 'Weather scene saved; the playlist shows it' : 'Weather scene saved. Add it to the playlist to show it between the others');
    } catch (e) { toast(false, `Could not save: ${e.message}`); }
    finally { setBusy(false); }
  };
  const addToPlaylist = async () => {
    setBusy(true);
    try {
      const p = await api.getPlaylist();
      let items = p.items && p.items.length ? p.items : [{ scene: 'clock', seconds: 0 }];
      if (!items.some((it) => it.scene === SCENE_ID)) {
        // with more than one item every item needs a time; the clock keeps its own if it has one
        items = [...items.map((it) => ({ ...it, seconds: it.seconds || 30 })), { scene: SCENE_ID, seconds: roundSecs }];
        await api.savePlaylist(items);
      }
      setScene((s) => ({ ...s, listed: true }));
      toast(true, `Added to the playlist: ${roundSecs} s of weather each round`);
    } catch (e) { toast(false, `Could not add it: ${e.message}`); }
    finally { setBusy(false); }
  };
  const playOnce = () => api.playScene(SCENE_ID, 0).then(() => toast(true, 'Playing the weather scene once'), (e) => toast(false, e.message));

  const unitOf = (name) => (name === 'wind' ? (units === 'c' ? 'km/h' : 'mph') : UNIT[name] === '°' ? `°${units.toUpperCase()}` : UNIT[name]);
  const held = VALUES.filter((v) => values && values[v.name] !== undefined).length;

  return (
    <section class="card wx-card">
      <div class="card-head">
        <h2>Weather</h2>
        {st && <span class={`pill wx-${st.cls || 'off'}`}>{st.label}</span>}
      </div>
      <p class="muted small">
        The globe fetches the weather for its location every 15 minutes from Open-Meteo (free, no sign-up).
        Write <code>{'{weather}'}</code> in a frame’s text to show <b>61 CLOUDY</b>, or <code>{'{temp}'}</code> for just the number.
      </p>
      {st && st.why && on && status !== 'OFF' && <div class="problem">{st.why}</div>}

      {on && values && values.weather && (
        <div class="wx-now">
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

      <div class="form-grid">
        <span class="label">Weather</span>
        <div class="seg" role="radiogroup" aria-label="Weather">
          <button role="radio" aria-checked={on} aria-pressed={on} onClick={() => !on && save({ weather: true }, 'Weather on: the globe fetches it now')}>On</button>
          <button role="radio" aria-checked={!on} aria-pressed={!on} onClick={() => on && save({ weather: false }, 'Weather off')}>Off</button>
        </div>
        <span class="label">Units</span>
        <div class="seg" role="radiogroup" aria-label="Units">
          <button role="radio" aria-checked={units === 'f'} aria-pressed={units === 'f'} onClick={() => units !== 'f' && save({ units: 'f' }, 'Fahrenheit and mph')}>°F and mph</button>
          <button role="radio" aria-checked={units === 'c'} aria-pressed={units === 'c'} onClick={() => units !== 'c' && save({ units: 'c' }, 'Celsius and km/h')}>°C and km/h</button>
        </div>
        <span class="label wx-top">Location</span>
        <div class="wx-loc">
          <div>
            {hasLoc
              ? <><b>{place || 'Saved coordinates'}</b> <span class="muted small mono">{settings.lat}, {settings.lon}</span></>
              : <span class="muted">{settings ? 'Not set yet' : 'Loading…'}</span>}
          </div>
          <form class="row wx-search" onSubmit={search}>
            <input type="text" value={query} onInput={(e) => setQuery(e.currentTarget.value)} placeholder="Town or city, e.g. Chicago" aria-label="Find a place" />
            <button class="btn" type="submit" disabled={searching || !query.trim()}>{searching ? 'Searching…' : 'Find'}</button>
          </form>
          {places && (places.length
            ? <div class="wx-places">{places.map((p) => <button key={`${p.lat},${p.lon}`} class="btn small" onClick={() => pick(p)}>{p.name}</button>)}</div>
            : <span class="muted small">No place by that name. Try the nearest town, or enter coordinates.</span>)}
          <details class="wx-coords">
            <summary class="small">Or enter coordinates</summary>
            <div class="row" style="margin-top:8px">
              <label class="row">Lat <input type="text" value={coords.lat} onInput={(e) => setCoords({ ...coords, lat: e.currentTarget.value })} placeholder="41.88" /></label>
              <label class="row">Lon <input type="text" value={coords.lon} onInput={(e) => setCoords({ ...coords, lon: e.currentTarget.value })} placeholder="-87.63" /></label>
              <button class="btn small" onClick={saveCoords}>Save location</button>
            </div>
          </details>
          <span class="muted small">Also used for the clock’s sunrise and sunset.</span>
        </div>
      </div>

      <details class="wx-values">
        <summary>{held ? `All weather values on the globe (${held})` : 'All weather values'}</summary>
        <table class="values">
          <thead><tr><th>Value</th><th>Now</th><th>Use in text</th></tr></thead>
          <tbody>
            {VALUES.map((v) => (
              <tr key={v.name}>
                <td>{v.label.replace(/ \(.*\)$/, '')}{unitOf(v.name) ? <span class="muted"> ({unitOf(v.name)})</span> : null}</td>
                <td>{values && values[v.name] !== undefined ? <b>{values[v.name]}</b> : <span class="muted">–</span>}</td>
                <td><code>{`{${v.name}}`}</code></td>
              </tr>
            ))}
          </tbody>
        </table>
      </details>

      <p class="muted small wx-recipe">
        Ready-made scene ({roundSecs} s): <span class="mono">{sc.frames.map((f) => fillValues(f.text, undefined, undefined, true)).join(' → ')}</span>
      </p>
      <div class="card-foot">
        <span class="muted small">{scene.exists ? (scene.listed ? 'The weather scene is in your playlist.' : 'The weather scene is saved on the globe.') : 'Make a scene that shows the weather.'}</span>
        <span style="flex:1" />
        {scene.exists && <button class="btn" onClick={playOnce}>Play once</button>}
        <button class={`btn${scene.exists ? '' : ' primary'}`} onClick={makeScene} disabled={busy}>{scene.exists ? 'Re-create scene' : 'Create weather scene'}</button>
        {scene.exists && !scene.listed && <button class="btn primary" onClick={addToPlaylist} disabled={busy}>Add to playlist</button>}
      </div>
    </section>
  );
}
