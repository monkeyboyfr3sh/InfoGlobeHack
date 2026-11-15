import { useEffect, useRef, useState } from 'preact/hooks';
import { GLOBE, subscribeNow, onTokenNeeded, setDisplay } from './lib/api.js';
import { Switch } from './components/Common.jsx';
import { VERSION } from './lib/releases.js';
import { Designer } from './pages/Designer.jsx';
import { Dashboard } from './pages/Dashboard.jsx';
import { Clock } from './pages/Clock.jsx';
import { Settings } from './pages/Settings.jsx';

function ThemeButton() {
  const isDark = () => {
    const t = document.documentElement.dataset.theme;
    return t ? t === 'dark' : matchMedia('(prefers-color-scheme: dark)').matches;
  };
  const [dark, setDark] = useState(isDark());
  const toggle = () => {
    const next = dark ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('theme', next); } catch { /* private mode */ }
    setDark(!dark);
  };
  const label = dark ? 'Switch to light mode' : 'Switch to dark mode';
  return (
    <button class="btn icon" onClick={toggle} aria-label={label} title={label}>
      {dark
        ? <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><circle cx="8" cy="8" r="3.2" /><g stroke="currentColor" stroke-width="1.4" stroke-linecap="round"><path d="M8 1v1.6M8 13.4V15M1 8h1.6M13.4 8H15M3 3l1.1 1.1M11.9 11.9 13 13M3 13l1.1-1.1M11.9 4.1 13 3" /></g></svg>
        : <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M13.5 10.4A6 6 0 0 1 5.6 2.5a6 6 0 1 0 7.9 7.9z" /></svg>}
    </button>
  );
}

// One menu, the same on every page: Home (what the globe shows), Clock (the clock
// studio) and Settings (everything else). #/scenes/<scene id> opens a scene's full
// editor, from Edit, More options on Home; #/settings/<card> opens Settings on that card.
const PAGES = [['home', 'Home'], ['clock', 'Clock'], ['settings', 'Settings']];
// links from before the menu was slimmed down: [page, card]
const OLD = { dashboard: ['home'], designer: ['home'], data: ['settings', 'weather'], weather: ['settings', 'weather'],
  planes: ['settings', 'planes'], advanced: ['settings', 'more'] };
function parseHash() {
  const [, raw, id] = (location.hash || '').replace(/^#/, '').split('/');
  if (OLD[raw]) return { page: OLD[raw][0], id: OLD[raw][1] || null };
  if (raw === 'scenes' && id) return { page: 'scenes', id };          // the full editor of one scene
  return { page: PAGES.some(([p]) => p === raw) ? raw : 'home', id: id || null };
}

// After the globe is updated, a page that was already open is an old copy that
// doesn't know the new features. When the live link comes back (the globe
// restarted) or the tab is looked at again, ask the globe for the page and
// reload if it carries a newer build stamp. Mid-typing, offer instead of reloading.
const BUILD = import.meta.env.VITE_BUILD || '';
function useFreshPage(link) {
  const [newer, setNewer] = useState(false);
  const state = useRef({ busy: false, last: Date.now() });
  const check = async () => {
    const s = state.current;
    if (!BUILD || GLOBE || s.busy || document.hidden || Date.now() - s.last < 15000) return;
    s.busy = true; s.last = Date.now();
    try {
      const html = await (await fetch(location.pathname, { cache: 'no-store' })).text();
      const m = /<meta name="build" content="(\d+)"/.exec(html);
      if (!m || m[1] === BUILD) return;
      let tried = '';
      try { tried = sessionStorage.getItem('reloaded-for') || ''; sessionStorage.setItem('reloaded-for', m[1]); } catch { /* private mode */ }
      const typing = /^(INPUT|TEXTAREA|SELECT)$/.test((document.activeElement || {}).tagName || '');
      if (typing || tried === m[1]) setNewer(true); else location.reload();
    } catch { /* the globe is restarting: the next change of the link asks again */ } finally { s.busy = false; }
  };
  useEffect(() => { check(); }, [link.online, link.via]);
  useEffect(() => {
    const on = () => check();
    document.addEventListener('visibilitychange', on);
    addEventListener('focus', on);
    return () => { document.removeEventListener('visibilitychange', on); removeEventListener('focus', on); };
  }, []);
  return newer;
}

// What the globe is doing, for the whole app: pushed over a WebSocket when
// the globe allows one, polled otherwise (lib/live.js).
function useNow() {
  const [now, setNow] = useState(null);
  const [link, setLink] = useState({ online: true, via: null });
  useEffect(() => subscribeNow(setNow, setLink), []);
  return { now, online: link.online, via: link.via };
}

const MODE = { playlist: 'Rotation', scene: 'Playing once', manual: 'Manual', off: 'Display off' };

// The display switch, for the whole app. The globe says where it stands in
// "now" (mode "off"); a change shows at once and is held until the globe agrees.
function useDisplay(now, say) {
  const [want, setWant] = useState(null);
  const real = !now || now.mode !== 'off';
  useEffect(() => {
    if (want === null) return undefined;
    if (now && real === want) { setWant(null); return undefined; }
    const t = setTimeout(() => setWant(null), 5000);
    return () => clearTimeout(t);
  }, [now, want]);
  const set = async (on) => {
    setWant(on);
    try {
      await setDisplay(on);
      say(true, on ? 'Display on' : 'Display off. The globe stays dark until you switch it back on');
    } catch (e) { setWant(null); say(false, e.message); throw e; }
  };
  return { on: want !== null ? want : real, set };
}

function StatusChip({ now, online, via }) {
  if (!online) return <span class="status bad" title="The globe isn't answering"><span class="dot" />Offline</span>;
  if (!now) return <span class="status"><span class="dot" />Connecting…</span>;
  const what = now.mode === 'manual' ? (now.text ? `“${now.text}”` : 'manual text') : now.name || now.scene;
  const how = via === 'ws' ? 'Connected to the globe (live)' : 'Connected to the globe (checking every 1.5 s)';
  return <span class={`status${now.mode === 'off' ? '' : ' ok'}`} title={now.mode === 'off' ? 'The display is switched off' : how}><span class="dot" />{MODE[now.mode] || now.mode}{what ? <> · <b>{what}</b></> : null}</span>;
}

// Asked for by lib/api.js when a change gets a 401; resolves with the token or null.
function TokenDialog({ reason, onDone }) {
  const [value, setValue] = useState('');
  const [show, setShow] = useState(false);
  const input = useRef(null);
  useEffect(() => { if (input.current) input.current.focus(); }, []);
  const submit = (e) => { e.preventDefault(); if (value.trim()) onDone(value.trim()); };
  return (
    <div class="modal-back" onClick={(e) => { if (e.target === e.currentTarget) onDone(null); }}>
      <form class="modal card" role="dialog" aria-modal="true" aria-labelledby="token-title" onSubmit={submit}
        onKeyDown={(e) => { if (e.key === 'Escape') onDone(null); }}>
        <h2 id="token-title">Access token</h2>
        <p class="hint">
          {reason === 'wrong'
            ? 'The token saved in this browser was not accepted. Enter the current one.'
            : 'This globe asks for its access token before it changes anything. Once it works, this browser remembers it.'}
        </p>
        <input ref={input} type={show ? 'text' : 'password'} value={value} onInput={(e) => setValue(e.currentTarget.value)}
          autocomplete="current-password" aria-label="Access token" placeholder="Access token" />
        <label class="check" style="margin-top:8px"><input type="checkbox" checked={show} onChange={(e) => setShow(e.currentTarget.checked)} /> Show</label>
        <div class="card-foot">
          <a class="btn link" href="#/settings" onClick={() => onDone(null)}>Lost it?</a>
          <span style="flex:1" />
          <button type="button" class="btn" onClick={() => onDone(null)}>Cancel</button>
          <button type="submit" class="btn primary" disabled={!value.trim()}>Continue</button>
        </div>
      </form>
    </div>
  );
}

export function App() {
  const [route, setRoute] = useState(parseHash());
  const [toast, setToast] = useState(null);
  const [asking, setAsking] = useState(null);       // {reason, resolve} while the token dialog is open
  const { now, online, via } = useNow();
  const say = (ok, msg) => setToast({ ok, msg, at: Date.now() });
  const display = useDisplay(now, say);
  const newer = useFreshPage({ online, via });
  useEffect(() => onTokenNeeded((reason) => new Promise((resolve) => setAsking({ reason, resolve }))), []);
  useEffect(() => {
    const on = () => setRoute(parseHash());
    addEventListener('hashchange', on);
    return () => removeEventListener('hashchange', on);
  }, []);
  useEffect(() => {
    if (!toast) return undefined;
    const t = setTimeout(() => setToast(null), toast.ok ? 3500 : 8000);
    return () => clearTimeout(t);
  }, [toast]);

  return (
    <div class="app">
      <header class="appbar">
        <a class="brand" href="#/home" aria-label="InfoGlobe home">
          <svg width="26" height="26" viewBox="0 0 26 26" aria-hidden="true">
            <circle cx="13" cy="13" r="11.5" fill="none" stroke="currentColor" stroke-width="1.5" opacity=".45" />
            {[5, 9, 13, 17, 21].map((x) => <circle key={x} cx={x} cy="13" r="1.5" fill="currentColor" />)}
            {[9, 13, 17].map((x) => <circle key={`t${x}`} cx={x} cy="8.5" r="1.1" fill="currentColor" opacity=".55" />)}
            {[9, 13, 17].map((x) => <circle key={`b${x}`} cx={x} cy="17.5" r="1.1" fill="currentColor" opacity=".55" />)}
          </svg>
          <span>InfoGlobe</span>
        </a>
        <a class="version" href="#/settings/about" title="Version and release notes">v{VERSION}</a>
        <nav class="tabs">
          {PAGES.map(([p, label]) => <a key={p} href={`#/${p}`} aria-current={route.page === p ? 'page' : undefined}>{label}</a>)}
        </nav>
        <div class="head-right">
          {GLOBE && <span class="muted small">{GLOBE}</span>}
          <label class="power" title="Switch the globe's display off (dark) or on. Everything is kept while it is off. Settings has a night schedule.">
            <span>Display</span>
            <Switch on={display.on} onChange={(on) => display.set(on).catch(() => false)} label="Display on" disabled={!now || !online} />
          </label>
          <StatusChip now={now} online={online} via={via} />
          <ThemeButton />
        </div>
      </header>
      <main class="wrap">
      {route.page === 'scenes' ? <Designer sceneId={route.id} toast={say} />
        : route.page === 'clock' ? <Clock toast={say} />
        : route.page === 'settings' ? <Settings toast={say} display={display} section={route.id} />
        : <Dashboard toast={say} now={now} online={online} display={display} />}
      </main>
      {newer && <div class="snack update" role="status">The globe has a newer version of this page. <button class="btn small primary" onClick={() => location.reload()}>Reload</button></div>}
      {toast && <div class={`snack ${toast.ok ? 'ok' : 'bad'}`} role="status" onClick={() => setToast(null)}>{toast.msg}</div>}
      {asking && <TokenDialog reason={asking.reason} onDone={(t) => { asking.resolve(t); setAsking(null); }} />}
    </div>
  );
}
