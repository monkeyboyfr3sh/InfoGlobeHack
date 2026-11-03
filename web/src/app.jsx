import { useEffect, useState } from 'preact/hooks';
import { GLOBE, getNow } from './lib/api.js';
import { Designer } from './pages/Designer.jsx';
import { Dashboard } from './pages/Dashboard.jsx';

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

// #/dashboard, #/designer, #/designer/<scene id>
function parseHash() {
  const [, page, id] = (location.hash || '').replace(/^#/, '').split('/');
  return { page: page === 'designer' ? 'designer' : 'dashboard', id: id || null };
}

// What the globe is doing, polled once for the whole app.
function useNow() {
  const [now, setNow] = useState(null);
  const [online, setOnline] = useState(true);
  useEffect(() => {
    let stop = false, t = 0;
    const poll = async () => {
      try { const n = await getNow(); if (!stop) { setNow(n); setOnline(true); } }
      catch { if (!stop) setOnline(false); }
      if (!stop) t = setTimeout(poll, document.hidden ? 5000 : 1500);
    };
    poll();
    return () => { stop = true; clearTimeout(t); };
  }, []);
  return { now, online };
}

const MODE = { playlist: 'Playlist', scene: 'Playing once', manual: 'Manual' };

function StatusChip({ now, online }) {
  if (!online) return <span class="status bad" title="The globe isn't answering"><span class="dot" />Offline</span>;
  if (!now) return <span class="status"><span class="dot" />Connecting…</span>;
  const what = now.mode === 'manual' ? (now.text ? `“${now.text}”` : 'manual text') : now.name || now.scene;
  return <span class="status ok" title="Connected to the globe"><span class="dot" />{MODE[now.mode] || now.mode}{what ? <> · <b>{what}</b></> : null}</span>;
}

export function App() {
  const [route, setRoute] = useState(parseHash());
  const [toast, setToast] = useState(null);
  const { now, online } = useNow();
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
  const say = (ok, msg) => setToast({ ok, msg, at: Date.now() });

  return (
    <div class="wrap">
      <header>
        <h1>InfoGlobe</h1>
        <div class="head-right">
          {GLOBE && <span class="muted small">{GLOBE}</span>}
          <StatusChip now={now} online={online} />
          <ThemeButton />
        </div>
      </header>
      <nav class="tabs">
        <a href="#/dashboard" aria-current={route.page === 'dashboard' ? 'page' : undefined}>Dashboard</a>
        <a href="#/designer" aria-current={route.page === 'designer' ? 'page' : undefined}>Designer</a>
        <a href={GLOBE + '/'}>Control &amp; Explorer</a>
      </nav>
      {route.page === 'designer' ? <Designer sceneId={route.id} toast={say} /> : <Dashboard toast={say} now={now} online={online} />}
      {toast && <div class={`snack ${toast.ok ? 'ok' : 'bad'}`} role="status" onClick={() => setToast(null)}>{toast.msg}</div>}
    </div>
  );
}
