import { useEffect, useRef, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { setLocation } from '../lib/values.js';
import { findPlaces } from '../lib/places.js';

// Pieces every page uses, so switches and option rows look and behave the same everywhere.

/** An on/off switch. It moves at once and saves behind; if `onChange(next)`
 *  (which may be async) fails or returns false it goes back. */
export function Switch({ on, onChange, label, disabled }) {
  const [pending, setPending] = useState(null);     // the position shown while a save is on its way
  const shown = pending === null ? !!on : pending;
  const click = async () => {
    if (pending !== null || disabled) return;
    const next = !shown;
    setPending(next);
    try { await onChange(next); } finally { setPending(null); }
  };
  return (
    <button type="button" class="switch" role="switch" aria-checked={shown} aria-busy={pending !== null} aria-label={label} disabled={disabled} onClick={click}>
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

const fmtPop = (n) => (n >= 1e6 ? `${(n / 1e6).toFixed(1)} M people` : n >= 1000 ? `${Math.round(n / 1000)}k people` : n ? `${n} people` : '');

/** A small map of the saved spot, so a wrong pick is obvious at a glance (needs internet in this browser). */
function MapPreview({ lat, lon }) {
  const d = 0.35;
  const src = `https://www.openstreetmap.org/export/embed.html?bbox=${lon - d * 1.6},${lat - d},${lon + d * 1.6},${lat + d}&layer=mapnik&marker=${lat},${lon}`;
  return <iframe class="loc-map" title="Map of the saved location" src={src} loading="lazy" referrerpolicy="no-referrer" />;
}

/** The globe's location: used by weather, planes and the clock's sunrise. One box
 *  takes a town, "town state", a ZIP code, coordinates or a pasted map link, and
 *  suggests places as you type. */
export function LocationPicker({ settings, onSaved, toast }) {
  const has = hasLocation(settings);
  const [editing, setEditing] = useState(false);
  const [query, setQuery] = useState('');
  const [places, setPlaces] = useState(null);       // suggestions; null = nothing searched yet
  const [state, setState] = useState('idle');       // idle | searching | failed
  const [hi, setHi] = useState(0);                  // highlighted suggestion
  const seq = useRef(0);
  const open = editing || (settings && !has);
  const place = has && settings.place && settings.place.lat === settings.lat && settings.place.lon === settings.lon ? settings.place.name : null;

  useEffect(() => {
    const q = query.trim();
    if (q.length < 2) { setPlaces(null); setState('idle'); return undefined; }
    const mine = ++seq.current;
    setState('searching');
    const t = setTimeout(async () => {
      try {
        const found = await findPlaces(q, (navigator.language || 'en').slice(0, 2));
        if (mine === seq.current) { setPlaces(found); setHi(0); setState('idle'); }
      } catch { if (mine === seq.current) { setPlaces(null); setState('failed'); } }
    }, 300);
    return () => clearTimeout(t);
  }, [query]);

  const pick = async (p) => {
    const patch = p.coords ? { lat: p.lat, lon: p.lon } : { lat: p.lat, lon: p.lon, place: { name: p.name, lat: p.lat, lon: p.lon } };
    try {
      await api.saveSettings(patch);
      setLocation({ lat: p.lat, lon: p.lon });
      onSaved(patch);
      setEditing(false); setQuery(''); setPlaces(null);
      toast(true, p.coords ? 'Location saved' : `Location set to ${p.name}`);
    } catch (e) { toast(false, `Could not save: ${e.message}`); }
  };
  const key = (e) => {
    if (e.key === 'Escape') { setEditing(false); setQuery(''); return; }
    if (!places || !places.length) return;
    if (e.key === 'ArrowDown') { e.preventDefault(); setHi((hi + 1) % places.length); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); setHi((hi + places.length - 1) % places.length); }
    else if (e.key === 'Enter') { e.preventDefault(); pick(places[hi]); }
  };

  if (!settings) return <div class="loc"><span class="muted">Loading…</span></div>;
  return (
    <div class="loc">
      {has && !open && (
        <div class="loc-set">
          <MapPreview lat={settings.lat} lon={settings.lon} />
          <div class="loc-what">
            <b>{place || 'Saved coordinates'}</b>
            <span class="muted small mono">{settings.lat}, {settings.lon}</span>
            <button class="btn small" onClick={() => setEditing(true)}>Change</button>
          </div>
        </div>
      )}
      {open && (
        <div class="loc-find">
          <div class="loc-box">
            <svg class="loc-icon" width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><circle cx="7" cy="7" r="4.5" /><path d="M10.5 10.5 14 14" /></svg>
            <input type="text" value={query} autofocus={editing} onInput={(e) => setQuery(e.currentTarget.value)} onKeyDown={key}
              placeholder="City, ZIP code or coordinates" aria-label="Find the globe's location" autocomplete="off" spellcheck={false} />
            {state === 'searching' && <span class="loc-spin" aria-hidden="true" />}
            {has && <button class="btn small link" onClick={() => { setEditing(false); setQuery(''); }}>Cancel</button>}
          </div>
          {places && places.length > 0 && (
            <ul class="loc-list" role="listbox">
              {places.map((p, i) => (
                <li key={`${p.lat},${p.lon}`} role="option" aria-selected={i === hi}>
                  <button type="button" class={i === hi ? 'hi' : ''} onMouseEnter={() => setHi(i)} onClick={() => pick(p)}>
                    <span class="loc-name"><b>{p.town}</b><span class="muted">{p.region}</span></span>
                    <span class="muted small">{p.coords ? 'Use these' : fmtPop(p.population)}</span>
                  </button>
                </li>
              ))}
            </ul>
          )}
          {places && places.length === 0 && state === 'idle' && <p class="muted small loc-hint">Nothing found for “{query.trim()}”. Try just the town name, a ZIP code, or coordinates like <span class="mono">30.22, -92.02</span>.</p>}
          {state === 'failed' && <p class="muted small loc-hint">The place search needs internet in this browser. You can still type coordinates like <span class="mono">30.22, -92.02</span>.</p>}
          {!places && state === 'idle' && <p class="muted small loc-hint">Try <span class="mono">Lafayette LA</span>, <span class="mono">70501</span>, or paste a Google Maps link.</p>}
        </div>
      )}
    </div>
  );
}
