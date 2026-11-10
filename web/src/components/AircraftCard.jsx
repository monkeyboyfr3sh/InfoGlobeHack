import { useEffect, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { fillValues } from '../lib/values.js';
import { VALUES, PLANE_SCENE } from '../lib/feeds/aircraft.js';

// Aircraft overhead (feed_aircraft.c, docs/aircraft.md): switch it on, how far
// to look, when to interrupt the playlist, what the globe sees right now, and
// the "plane" scene the overhead alert plays.

const RANGES = [3, 5, 10, 15, 25];
const ALERTS = [[0, 'Never'], [1, '1 nm'], [2, '2 nm'], [3, '3 nm'], [5, '5 nm']];
const CEILINGS = [[0, 'Any height'], [20000, 'Below 20,000 ft'], [10000, 'Below 10,000 ft'], [5000, 'Below 5,000 ft']];
const km = (nm) => Math.round(nm * 1.852);
const num = (v, d) => (Number.isFinite(v) ? v : d);

function fromSettings(s) {
  return { on: s.aircraft === true, radius: num(s.aircraftRadius, 10), alert: num(s.aircraftAlert, 3), alertFt: num(s.aircraftAlertFt, 0) };
}

function Seg({ label, options, value, onPick, title }) {
  return (
    <div class="seg" role="radiogroup" aria-label={label}>
      {options.map(([v, l]) => (
        <button key={v} role="radio" aria-checked={value === v} aria-pressed={value === v} title={title ? title(v) : undefined} onClick={() => onPick(v)}>{l}</button>
      ))}
    </div>
  );
}

export function AircraftCard({ toast }) {
  const [cfg, setCfg] = useState(null);         // as edited
  const [saved, setSaved] = useState('');       // as on the globe
  const [hasLoc, setHasLoc] = useState(true);
  const [vals, setVals] = useState({});
  const [scene, setScene] = useState(null);     // does the "plane" scene exist? (null = don't know yet)
  const [inList, setInList] = useState(false);
  const [busy, setBusy] = useState(false);
  const set = (patch) => setCfg((c) => ({ ...c, ...patch }));

  const loadScene = async () => {
    try {
      setScene((await api.getScenes()).some((s) => s.id === 'plane'));
      setInList(((await api.getPlaylist()).items || []).some((it) => it.scene === 'plane'));
    } catch { /* offline: the status chip says so */ }
  };
  useEffect(() => {
    api.getSettings().then((s) => {
      const c = fromSettings(s);
      setCfg(c); setSaved(JSON.stringify(c));
      setHasLoc(Number.isFinite(s.lat) && Number.isFinite(s.lon));
    }, () => {});
    loadScene();
    const poll = () => api.getValues().then((v) => setVals(v || {}), () => {});
    poll();
    const t = setInterval(poll, 5000);
    return () => clearInterval(t);
  }, []);

  if (!cfg) return <section class="card"><div class="card-head"><h2>Aircraft overhead</h2></div><p class="muted">Loading…</p></section>;

  const on = JSON.parse(saved || '{}').on;     // what the globe is doing, not the unsaved choice
  const dirty = saved !== JSON.stringify(cfg);
  const st = vals.plane_status;
  const n = parseInt(vals.planes, 10);
  const pill = !on ? ['', 'Off'] : st === 'OK' ? ['on', 'Watching the sky'] : st === 'NO LOCATION' || (!st && !hasLoc) ? ['warn', 'No location']
    : st === 'OFFLINE' ? ['warn', 'No answer'] : ['', 'Starting…'];

  const save = async () => {
    const c = { ...cfg, alert: Math.min(cfg.alert, cfg.radius) };
    try {
      await api.saveSettings({ aircraft: c.on, aircraftRadius: c.radius, aircraftAlert: c.alert, aircraftAlertFt: c.alertFt });
      setCfg(c); setSaved(JSON.stringify(c));
      toast(true, c.on ? 'Saved. The globe looks for aircraft within a few seconds' : 'Saved. The globe stops looking for aircraft');
    } catch (e) { toast(false, `Could not save: ${e.message}`); }
  };

  const makeScene = async () => {
    if (scene && !confirm('Replace your “plane” scene with the default one?')) return;
    setBusy(true);
    try {
      await api.saveScene('plane', PLANE_SCENE);
      toast(true, scene ? 'Plane scene reset to the default' : 'Plane scene created. It plays when a plane comes close');
      setScene(true);
    } catch (e) { toast(false, `Could not save the scene: ${e.message}`); }
    finally { setBusy(false); }
  };

  const togglePlaylist = async () => {
    setBusy(true);
    try {
      if (!inList && !scene) { await api.saveScene('plane', PLANE_SCENE); setScene(true); }
      const p = await api.getPlaylist();
      let items = p.items && p.items.length ? p.items : [{ scene: 'clock', seconds: 0 }];
      if (inList) items = items.filter((it) => it.scene !== 'plane');
      else items = [...items.map((it) => ({ ...it, seconds: it.seconds || 30 })), { scene: 'plane', seconds: 30 }];
      if (!items.length) items = [{ scene: 'clock', seconds: 0 }];
      await api.savePlaylist(items);
      toast(true, inList ? 'Taken out of the playlist' : 'Added to the end of the playlist, 30 s');
      setInList(!inList);
    } catch (e) { toast(false, `Could not change the playlist: ${e.message}`); }
    finally { setBusy(false); }
  };

  const playNow = () => api.playScene('plane', 0).then(() => toast(true, 'Playing the plane scene once'), (e) => toast(false, e.message));

  return (
    <section class="card">
      <div class="card-head"><h2>Aircraft overhead</h2><span class={`pill ${pill[0]}`}>{pill[1]}</span></div>
      <p class="muted small">
        The globe asks a free, volunteer-run flight tracker every 15 seconds which aircraft are near its location and
        shows the nearest one in the air: <code>{'{planeline}'}</code> reads like <span class="mono">UAL123 SFO-ORD B738</span>.
      </p>

      {on && (st === 'NO LOCATION' || (!st && !hasLoc)) && (
        <div class="problem">The globe doesn't know where it is yet. Save its location on the <a href="#/clock">Clock</a> page
          (tick <i>Sunrise and sunset</i> under Also show) or in the weather card.</div>
      )}
      {on && st === 'OFFLINE' && <div class="problem">The flight trackers aren't answering right now. The globe keeps trying, less often while they're down.</div>}
      {on && st === 'OK' && (
        <div class="air-now">
          <div class="air-line mono">{vals.planeline}</div>
          {n > 0 && <div class="small">{vals.planetrip} · {vals.planewhere}</div>}
          <div class="muted small">{n > 0 ? `${n} aircraft in the air within ${cfg.radius} nm (${km(cfg.radius)} km)` : `Nothing in the air within ${cfg.radius} nm right now`}</div>
        </div>
      )}

      <div class="air-grid">
        <span class="label">Watch for aircraft</span>
        <Seg label="Watch for aircraft" options={[[false, 'Off'], [true, 'On']]} value={cfg.on} onPick={(v) => set({ on: v })} />
        <span class="label">Look within</span>
        <Seg label="Look within" options={RANGES.map((r) => [r, `${r} nm`])} value={cfg.radius} onPick={(v) => set({ radius: v })} title={(v) => `${km(v)} km`} />
        <span class="label">Interrupt the playlist closer than</span>
        <Seg label="Interrupt the playlist closer than" options={ALERTS.filter(([v]) => v <= cfg.radius)} value={Math.min(cfg.alert, cfg.radius)}
          onPick={(v) => set({ alert: v })} title={(v) => (v ? `${km(v)} km` : 'Only show planes in scenes you choose')} />
        {cfg.alert > 0 && <>
          <span class="label">Only planes</span>
          <Seg label="Only planes" options={CEILINGS} value={cfg.alertFt} onPick={(v) => set({ alertFt: v })} />
        </>}
      </div>
      <p class="hint small" style="margin-top:8px">
        {cfg.alert > 0
          ? `When a plane comes within ${Math.min(cfg.alert, cfg.radius)} nm, the globe plays the “plane” scene once, then goes back to the playlist. Each plane only once every 10 minutes, and never over a message you put up.`
          : 'The playlist is never interrupted; show planes with the values below in any scene.'}
      </p>

      <div class="air-sub">
        <h3>Plane scene</h3>
        {scene === false && <p class="muted small">There is no “plane” scene yet, so the alert has nothing to play. The default shows:</p>}
        {scene !== false && <p class="muted small">The alert plays your scene called “plane”. The default one shows:</p>}
        <ol class="recipe small">
          {PLANE_SCENE.frames.map((f, i) => <li key={i}><span class="mono">{fillValues(f.text)}</span></li>)}
        </ol>
        <div class="row" style="margin-top:10px">
          <button class="btn" onClick={makeScene} disabled={busy}>{scene ? 'Reset to the default' : 'Create the plane scene'}</button>
          {scene && <button class="btn" onClick={playNow}>Play now</button>}
          {scene && <a class="btn" href="#/designer/plane">Edit</a>}
          <span style="flex:1" />
          <button class="btn" onClick={togglePlaylist} disabled={busy} title="Show the nearest plane between your other scenes">
            {inList ? 'Take out of the playlist' : 'Also show it in the playlist'}
          </button>
        </div>
      </div>

      <details style="margin-top:14px">
        <summary>Values for your own scenes</summary>
        <table class="values air-values" style="margin-top:8px">
          <thead><tr><th>Write</th><th>Now</th><th>What</th></tr></thead>
          <tbody>
            {VALUES.map((v) => (
              <tr key={v.name}><td><code>{`{${v.name}}`}</code></td><td class="mono">{vals[v.name] ?? '–'}</td><td class="muted small">{v.label}</td></tr>
            ))}
          </tbody>
        </table>
      </details>

      <div class="card-foot">
        <span class="muted small" style="flex:1">
          Aircraft from <a href="https://adsb.lol" target="_blank" rel="noopener noreferrer">adsb.lol</a> (ODbL)
          or <a href="https://adsb.fi" target="_blank" rel="noopener noreferrer">adsb.fi</a>, routes
          from <a href="https://www.adsbdb.com" target="_blank" rel="noopener noreferrer">adsbdb</a>. They get the globe's location, rounded to about 1 km.
        </span>
        <button class="btn primary" onClick={save} disabled={!dirty}>{dirty ? 'Save' : 'Saved'}</button>
      </div>
    </section>
  );
}
