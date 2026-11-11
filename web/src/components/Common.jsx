import { useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { setLocation } from '../lib/values.js';
import { searchPlaces } from '../lib/feeds/weather.js';

// Pieces every page uses, so switches and option rows look and behave the same everywhere.

/** An on/off switch. `onChange(next)` may be async; the switch waits for it. */
export function Switch({ on, onChange, label, disabled }) {
  const [busy, setBusy] = useState(false);
  const click = async () => {
    if (busy || disabled) return;
    setBusy(true);
    try { await onChange(!on); } finally { setBusy(false); }
  };
  return (
    <button type="button" class="switch" role="switch" aria-checked={!!on} aria-label={label} disabled={disabled || busy} onClick={click}>
      <span class="knob" />
    </button>
  );
}

/** One choice out of a few: [[value, label], ...]. */
export function Choice({ label, options, value, onPick }) {
  return (
    <div class="seg" role="radiogroup" aria-label={label}>
      {options.map(([v, l]) => (
        <button key={String(v)} type="button" role="radio" aria-checked={value === v} aria-pressed={value === v} onClick={() => value !== v && onPick(v)}>{l}</button>
      ))}
    </div>
  );
}

/** A labelled option row inside a card. */
export function Row({ label, hint, children }) {
  return (
    <div class="opt">
      <div class="opt-label">{label}{hint && <span class="muted small">{hint}</span>}</div>
      <div class="opt-ctl">{children}</div>
    </div>
  );
}

export const hasLocation = (s) => !!s && Number.isFinite(s.lat) && Number.isFinite(s.lon);

/** The globe's location: used by weather, planes and the clock's sunrise. One place to set it. */
export function LocationPicker({ settings, onSaved, toast }) {
  const [query, setQuery] = useState('');
  const [places, setPlaces] = useState(null);       // search results, null = no search yet
  const [searching, setSearching] = useState(false);
  const [coords, setCoords] = useState(null);       // typed coordinates, null = show the saved ones
  const has = hasLocation(settings);
  const place = has && settings.place && settings.place.lat === settings.lat && settings.place.lon === settings.lon ? settings.place.name : null;
  const lat = coords ? coords.lat : has ? String(settings.lat) : '';
  const lon = coords ? coords.lon : has ? String(settings.lon) : '';

  const save = async (patch, msg) => {
    try {
      await api.saveSettings(patch);
      setLocation({ lat: patch.lat, lon: patch.lon });
      onSaved(patch);
      setPlaces(null); setQuery(''); setCoords(null);
      toast(true, msg);
    } catch (e) { toast(false, `Could not save: ${e.message}`); }
  };
  const search = async (e) => {
    e.preventDefault();
    if (!query.trim()) return;
    setSearching(true);
    try { setPlaces(await searchPlaces(query.trim(), (navigator.language || 'en').slice(0, 2))); }
    catch { toast(false, 'Couldn’t reach the place search (this browser needs internet). Enter coordinates instead.'); }
    finally { setSearching(false); }
  };
  const saveCoords = () => {
    const la = parseFloat(lat), lo = parseFloat(lon);
    if (!Number.isFinite(la) || !Number.isFinite(lo) || Math.abs(la) > 90 || Math.abs(lo) > 180) { toast(false, 'Latitude −90…90 and longitude −180…180, e.g. 41.88 and −87.63'); return; }
    save({ lat: la, lon: lo }, 'Location saved');
  };

  return (
    <div class="loc">
      <div>
        {has ? <><b>{place || 'Saved coordinates'}</b> <span class="muted small mono">{settings.lat}, {settings.lon}</span></>
          : <span class="muted">{settings ? 'Not set yet' : 'Loading…'}</span>}
      </div>
      <form class="row loc-search" onSubmit={search}>
        <input type="text" value={query} onInput={(e) => setQuery(e.currentTarget.value)} placeholder="Town or city, e.g. Chicago" aria-label="Find a place" />
        <button class="btn" type="submit" disabled={searching || !query.trim()}>{searching ? 'Searching…' : 'Find'}</button>
      </form>
      {places && (places.length
        ? <div class="loc-places">{places.map((p) => <button key={`${p.lat},${p.lon}`} class="btn small" onClick={() => save({ lat: p.lat, lon: p.lon, place: p }, `Location set to ${p.name}`)}>{p.name}</button>)}</div>
        : <span class="muted small">No place by that name. Try the nearest town, or enter coordinates.</span>)}
      <details>
        <summary class="small">Or enter coordinates</summary>
        <div class="row" style="margin-top:8px">
          <label class="row">Lat <input type="text" style="width:100px" value={lat} onInput={(e) => setCoords({ lat: e.currentTarget.value, lon })} placeholder="41.88" /></label>
          <label class="row">Lon <input type="text" style="width:100px" value={lon} onInput={(e) => setCoords({ lat, lon: e.currentTarget.value })} placeholder="-87.63" /></label>
          <button class="btn small" onClick={saveCoords}>Save</button>
        </div>
      </details>
    </div>
  );
}
