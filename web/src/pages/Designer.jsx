import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { encodeText } from '../lib/protocol.js';
import { makeScene, makeSequence } from '../lib/sim.js';
import { newFrame, slug, cleanScene, toStored, toCodes, styleOf, simFrame } from '../lib/scenes.js';
import { useHistory } from '../lib/history.js';
import { BUILT_IN, HIDDEN, sceneSeconds, setInRotation, fitRotationSeconds, deleteScene } from '../lib/rotation.js';
import * as api from '../lib/api.js';
import { Screen } from '../components/Screen.jsx';
import { FrameEditor, EFFECTS, animName } from '../components/FrameEditor.jsx';
import { Mini } from '../components/Mini.jsx';
import { MessageRows } from '../components/MessageRows.jsx';
import { Switch } from '../components/Common.jsx';
import { WhenItShows } from '../components/WhenItShows.jsx';

// My scenes. A scene is a short show: messages that appear one after another
// and then start again. Home lists them; #/scenes/<id> (or /new) is the full editor of one.

const fxName = (id) => (id === null || id === undefined || id < 0 ? 'Just appears' : EFFECTS[id].name);
const fmtSecs = (s) => (s >= 60 ? `${Math.floor(s / 60)} min${s % 60 ? ` ${s % 60} s` : ''}` : `${s} s`);

const BLANK = { id: '', name: 'My scene', frames: [newFrame({ text: 'HELLO' })] };
// Scenes other pages look after; they can be opened here, with a note saying so.
const OWNER = { clock: ['Clock', '#/clock'], weather: ['Weather settings', '#/settings/weather'], plane: ['Planes settings', '#/settings/planes'] };

// The scene being worked on is kept in this browser, with what the globe had
// when it was last in step ("base"): with no changes of its own, a scene edited
// somewhere else since (Home) is fetched afresh instead of shown stale.
function loadDraft() {
  try { const d = JSON.parse(localStorage.getItem('scene-draft')); if (d && d.scene && Array.isArray(d.scene.frames) && d.scene.frames.length) return d; } catch { /* ignore */ }
  return { scene: BLANK, sel: 0, base: null };
}

/* ---------- the editor ---------- */

export function Designer({ sceneId, toast }) {
  const draft = useMemo(loadDraft, []);
  const hist = useHistory(draft.scene);
  const scene = hist.value;
  const [sel, setSel] = useState(Math.min(draft.sel || 0, scene.frames.length - 1));
  const [savedJson, setSavedJson] = useState(draft.base || null);
  const [library, setLibrary] = useState([]);       // the globe's scenes: a new name mustn't overwrite one by accident
  const [items, setItems] = useState([]);
  const [timers, setTimers] = useState([]);
  const [hover, setHover] = useState(null);
  const [hoverAnim, setHoverAnim] = useState(undefined);   // undefined: not hovering; null: "None"
  const [whole, setWhole] = useState(false);
  const [runKey, setRunKey] = useState(0);
  const [busy, setBusy] = useState(false);
  const fileRef = useRef(null);
  const frame = scene.frames[Math.min(sel, scene.frames.length - 1)];
  const dirty = savedJson !== JSON.stringify(toStored(scene));

  const refreshLibrary = async () => {
    try {
      const list = (await api.getScenes()).filter((s) => !HIDDEN.includes(s.id));
      setLibrary(list);
      const p = await api.getPlaylist();
      setItems(p.items || []);
      setTimers(p.timers || []);
    } catch { setLibrary([]); }
  };
  useEffect(() => { refreshLibrary(); }, []);
  useEffect(() => { try { localStorage.setItem('scene-draft', JSON.stringify({ scene, sel, base: savedJson })); } catch { /* ignore */ } }, [scene, sel, savedJson]);
  // A scene restored from last time: is it still what the globe has?
  useEffect(() => {
    if (!draft.scene.id) return;
    const mine = JSON.stringify(toStored(draft.scene));
    api.getScene(draft.scene.id).then((got) => {
      const s = cleanScene(got), theirs = JSON.stringify(toStored(s));
      if (mine === draft.base && theirs !== mine) { hist.set(s, { replace: true }); setSel(0); }    // changed elsewhere, nothing unsaved here
      setSavedJson(theirs);
    }, (e) => {
      if (e.status !== 404) return;                    // offline: keep what is here
      hist.set(BLANK, { replace: true }); setSel(0); setSavedJson(null);     // deleted elsewhere
      if (sceneId === draft.scene.id) location.hash = '#/home';
    });
  }, []);

  const open = async (id) => {
    try {
      const s = cleanScene(await api.getScene(id));
      hist.set(s, { replace: true });
      setSel(0);
      setSavedJson(JSON.stringify(toStored(s)));
    } catch (e) { toast(false, `Could not open: ${e.message}`); location.hash = '#/home'; }
  };
  useEffect(() => {
    if (!sceneId || sceneId === 'new' || sceneId === scene.id) return;
    if (dirty && scene.id && !confirm(`Discard unsaved changes to “${scene.name}”?`)) { location.hash = `#/scenes/${scene.id}`; return; }
    open(sceneId);
  }, [sceneId]);

  // ---- scene edits ----
  const setFrame = (f) => hist.set((s) => ({ ...s, frames: s.frames.map((x, i) => (i === sel ? f : x)) }));
  const dupFrame = () => { hist.set((s) => ({ ...s, frames: [...s.frames.slice(0, sel + 1), { ...frame }, ...s.frames.slice(sel + 1)] })); setSel(sel + 1); };

  // keyboard: undo/redo, duplicate
  useEffect(() => {
    const onKey = (e) => {
      if (!(e.ctrlKey || e.metaKey)) return;
      const k = e.key.toLowerCase();
      if (k === 'z' && !e.shiftKey) { e.preventDefault(); hist.undo(); }
      else if ((k === 'z' && e.shiftKey) || k === 'y') { e.preventDefault(); hist.redo(); }
      else if (k === 'd') { e.preventDefault(); dupFrame(); }
    };
    addEventListener('keydown', onKey);
    return () => removeEventListener('keydown', onKey);
  });

  // ---- preview ----
  const prevFrame = scene.frames.length > 1 ? scene.frames[(sel + scene.frames.length - 1) % scene.frames.length] : null;
  const prev = prevFrame ? { codes: toCodes(prevFrame.text), style: styleOf(prevFrame) } : { codes: encodeText('12:00').codes, style: { entrance: -1, after: 'spin' } };
  const hovering = hover !== null || hoverAnim !== undefined;
  const preview = useMemo(() => {
    if (whole && !hovering) {
      return makeSequence(scene.frames.map((f) => ({ ...simFrame(f), ms: (f.seconds || 6) * 1000 })), EFFECTS);
    }
    if (hover !== null) return makeScene(prev, { ...simFrame(frame, null), style: { ...styleOf(frame), entrance: hover } }, EFFECTS);
    return makeScene(prev, simFrame(frame, hoverAnim !== undefined ? hoverAnim : frame.anim), EFFECTS);
  }, [scene, sel, hover, hoverAnim, whole]);

  const shownFx = whole && !hovering ? scene.frames.filter((f) => !f.anim).map((f) => f.entrance)
    : [hover !== null ? hover : hoverAnim !== undefined || frame.anim ? null : frame.entrance];
  const estimate = shownFx.some((id) => id !== null && id !== undefined && id >= 0 && !EFFECTS[String(id)].calibrated);
  const animShown = whole && !hovering ? scene.frames.some((f) => f.anim) : hover === null && !!(hoverAnim !== undefined ? hoverAnim : frame.anim);

  // ---- globe actions ----
  const save = async (alsoShow = false) => {
    const id = scene.id || slug(scene.name);
    if (!scene.id && (id === 'new' || HIDDEN.includes(id) || BUILT_IN[id])) { toast(false, 'That name is taken by the app. Give the scene another name.'); return null; }
    if (!scene.id && library.some((s) => s.id === id) && !confirm(`You already have a scene called “${scene.name}”. Replace it?`)) return null;
    setBusy(true);
    try {
      await api.saveScene(id, toStored(scene));
      hist.set((s) => ({ ...s, id }), { replace: true });
      setSavedJson(JSON.stringify(toStored(scene)));
      location.hash = `#/scenes/${id}`;
      const showing = items.some((it) => it.scene === id);
      if (alsoShow && !showing) await setInRotation(id, true, sceneSeconds(scene));
      else if (showing && !OWNER[id]) await fitRotationSeconds(id, sceneSeconds(scene));     // its turn is one pass of its messages
      await refreshLibrary();
      toast(true, showing ? `Saved “${scene.name}”. The globe is showing the new version`
        : alsoShow ? `Saved “${scene.name}”. It now takes a turn on the globe; set the order on Home`
          : `Saved “${scene.name}”. It isn’t showing yet: switch on “On the globe” at the top`);
      return id;
    } catch (e) { toast(false, `Could not save: ${e.message}`); return null; }
    finally { setBusy(false); }
  };
  const showOnGlobe = async (next) => {
    try { setItems(await setInRotation(scene.id, next, sceneSeconds(scene))); toast(true, next ? `“${scene.name}” now takes a turn on the globe; set the order on Home` : `“${scene.name}” taken out of the rotation`); }
    catch (e) { toast(false, e.message); }
  };
  const tryIt = async () => {
    try { await api.playFrames(toStored(scene).frames, 0); toast(true, 'Playing it once on the globe, then the globe goes back to what it was doing'); }
    catch (e) { toast(false, `Could not play: ${e.message}`); }
  };
  const remove = async () => {
    if (!scene.id || !confirm(`Delete “${scene.name}” from the globe?`)) return;
    try {
      await deleteScene(scene.id);
      toast(true, 'Deleted');
      hist.set(BLANK, { replace: true }); setSel(0); setSavedJson(null);
      await refreshLibrary();
      location.hash = '#/home';
    } catch (e) { toast(false, e.message); }
  };
  const exportFile = () => {
    const blob = new Blob([JSON.stringify({ ...toStored(scene), id: scene.id || slug(scene.name) }, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `${scene.id || slug(scene.name)}.globe.json`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  };
  const importFile = async (file) => {
    try {
      const s = cleanScene(JSON.parse(await file.text()));
      hist.set({ ...s, id: '' }, { replace: true });
      setSel(0);
      setSavedJson(null);
      location.hash = '#/scenes/new';
      toast(true, `Opened “${s.name}” from the file. Save it to keep it on the globe`);
    } catch (e) { toast(false, `Could not read that file: ${e.message}`); }
  };

  const owner = OWNER[scene.id];
  const n = scene.frames.length;
  return (
    <div>
      <div class="scenebar">
        <a class="btn" href="#/home" title="Back to the list on Home">← Home</a>
        <div class="scenebar-title">
          <input type="text" class="name" value={scene.name} onInput={(e) => hist.set((x) => ({ ...x, name: e.currentTarget.value }))} aria-label="Scene name" placeholder="Name this scene" />
          <span class={`save-state${dirty ? ' dirty' : ''}`}>{!scene.id ? 'Not saved yet' : dirty ? 'Changes not saved' : 'Saved on the globe'}</span>
        </div>
        {scene.id && (
          <label class="scene-on" title="Give this scene a turn on the globe, with the clock and your other things">
            On the globe <Switch on={items.some((it) => it.scene === scene.id)} onChange={showOnGlobe} label="Give this scene a turn on the globe" />
          </label>
        )}
        <div class="scenebar-group">
          <button class="btn icon" onClick={hist.undo} disabled={!hist.canUndo} title="Undo (Ctrl+Z)" aria-label="Undo">↶</button>
          <button class="btn icon" onClick={hist.redo} disabled={!hist.canRedo} title="Redo (Ctrl+Shift+Z)" aria-label="Redo">↷</button>
        </div>
        <details class="menu">
          <summary class="btn">More ▾</summary>
          <div class="menu-list">
            <button onClick={exportFile}>Save a copy to a file</button>
            <button onClick={() => fileRef.current.click()}>Open from a file…</button>
            {scene.id && !owner && <button class="danger" onClick={remove}>Delete this scene</button>}
          </div>
        </details>
        <input ref={fileRef} type="file" accept=".json,application/json" hidden onChange={(e) => { const f = e.currentTarget.files[0]; if (f) importFile(f); e.currentTarget.value = ''; }} />
        <button class="btn" onClick={tryIt} title="Play it once on the globe without saving">Try on the globe ▶</button>
        {scene.id
          ? <button class="btn primary" onClick={() => save()} disabled={busy || !dirty}>Save</button>
          : <>
            <button class="btn" onClick={() => save()} disabled={busy} title="Keep it on the globe without showing it yet">Save only</button>
            <button class="btn primary" onClick={() => save(true)} disabled={busy} title="Save it and give it a turn on the globe">Save and show on the globe</button>
          </>}
      </div>
      {owner && (
        <div class="problem">
          This scene belongs to the <a href={owner[1]}>{owner[0]} page</a>, which rewrites it when you change things there. Small edits here are fine; for a look of your own, start a new scene.
        </div>
      )}

      <div class="layout">
        <div class="layout-left">
        <div class="sticky">
          <Screen scene={preview} runKey={runKey} holdMs={whole && !hovering ? 0 : 4000}
            badge={estimate || animShown ? '≈ close to what the globe does' : null} />
          <div class="row controls">
            <div class="seg" role="group" aria-label="Preview">
              <button aria-pressed={!whole} onClick={() => setWhole(false)}>This message</button>
              <button aria-pressed={whole} onClick={() => setWhole(true)}>The whole scene</button>
            </div>
            <button class="btn" onClick={() => setRunKey((k) => k + 1)}>↻ Play again</button>
            <span class="muted small">{hover !== null ? `Trying ${fxName(hover)}` : hoverAnim ? `Trying ${animName(hoverAnim)}` : ''}</span>
          </div>
        </div>

          <div class="strip-head">
            <h2>Messages</h2>
            <span class="muted small">{n === 1 ? 'one message' : `${n} messages, one after another`} · {fmtSecs(sceneSeconds(scene))}, then it starts again</span>
          </div>
          <MessageRows frames={scene.frames} sel={sel} onSelect={setSel} onChange={(frames, i) => { hist.set((s) => ({ ...s, frames })); setSel(i); }} />
        </div>

        <div>
          <div class="inspector-head">
            <h2>Message {sel + 1}{n > 1 ? ` of ${n}` : ''}</h2>
            <span class="muted small">every option, with previews</span>
            <span style="flex:1" />
            <button class="btn small" onClick={dupFrame} title="Ctrl+D" disabled={n >= 24}>Make a copy</button>
          </div>
          <FrameEditor frame={frame} onChange={setFrame} prevCodes={prev.codes} onHover={setHover} onHoverAnim={setHoverAnim} />
        </div>
      </div>
      <WhenItShows scene={scene} seconds={sceneSeconds(scene)} items={items} timers={timers} onItems={setItems} onTimers={setTimers} toast={toast} />
    </div>
  );
}
