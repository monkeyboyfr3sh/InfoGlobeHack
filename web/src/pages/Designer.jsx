import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { encodeText, compileFrame, hex, TEXT_MAX } from '../lib/protocol.js';
import { makeScene, makeSequence } from '../lib/sim.js';
import { fillValues } from '../lib/values.js';
import { TEMPLATES, newFrame, slug, cleanScene, toStored } from '../lib/scenes.js';
import { useHistory } from '../lib/history.js';
import * as api from '../lib/api.js';
import { Screen } from '../components/Screen.jsx';
import { FrameEditor, EFFECTS, animName } from '../components/FrameEditor.jsx';
import { Mini } from '../components/Mini.jsx';
import { animStep, isFiniteAnim } from '../lib/anim.js';
import { animStepStyle } from '../lib/sim.js';

const toCodes = (text) => encodeText(fillValues(text)).codes.slice(0, TEXT_MAX);
const styleOf = (f) => ({ entrance: f.entrance ?? -1, after: f.after, blink: f.blink, shift: f.shift });
const fxName = (id) => (id === null || id === undefined || id < 0 ? 'Appears' : EFFECTS[id].name);
const fill = (s) => fillValues(s);
/** What makeScene needs for a frame, its custom animation included. */
const simFrame = (f, anim = f.anim) => ({ codes: toCodes(f.text), style: styleOf(f), ...(anim ? { anim, text: fillValues(f.text), fill } : {}) });

function loadDraft() {
  try { const d = JSON.parse(localStorage.getItem('scene-draft')); if (d && d.scene) return d; } catch { /* ignore */ }
  return { scene: { id: '', name: 'My scene', frames: [newFrame({ text: 'SERVER OK' })] }, sel: 0 };
}

export function Designer({ sceneId, toast }) {
  const draft = useMemo(loadDraft, []);
  const hist = useHistory(draft.scene);
  const scene = hist.value;
  const [sel, setSel] = useState(Math.min(draft.sel || 0, scene.frames.length - 1));
  const [savedJson, setSavedJson] = useState(null);
  const [library, setLibrary] = useState([]);
  const [hover, setHover] = useState(null);
  const [hoverAnim, setHoverAnim] = useState(undefined);   // undefined: not hovering; null: "None"
  const [whole, setWhole] = useState(false);
  const [runKey, setRunKey] = useState(0);
  const [live, setLive] = useState(false);
  const [busy, setBusy] = useState(false);
  const fileRef = useRef(null);
  const frame = scene.frames[Math.min(sel, scene.frames.length - 1)];
  const dirty = savedJson !== JSON.stringify(toStored(scene));

  const refreshLibrary = () => api.getScenes().then(setLibrary, () => setLibrary([]));
  useEffect(() => { refreshLibrary(); }, []);
  useEffect(() => { try { localStorage.setItem('scene-draft', JSON.stringify({ scene, sel })); } catch { /* ignore */ } }, [scene, sel]);

  const open = async (id) => {
    if (dirty && !confirm('Discard unsaved changes to this scene?')) return;
    try {
      const s = cleanScene(await api.getScene(id));
      hist.set(s, { replace: true });
      setSel(0);
      setSavedJson(JSON.stringify(toStored(s)));
      location.hash = `#/designer/${id}`;
    } catch (e) { toast(false, `Could not open: ${e.message}`); }
  };
  useEffect(() => { if (sceneId && sceneId !== scene.id) open(sceneId); }, [sceneId]);

  const fromTemplate = (key) => {
    const t = TEMPLATES.find((x) => x.key === key);
    if (!t || (dirty && !confirm('Discard unsaved changes to this scene?'))) return;
    hist.set({ id: '', name: t.key === 'blank' ? 'My scene' : t.name, frames: t.frames.map((f) => ({ ...f })) }, { replace: true });
    setSel(0);
    setSavedJson(null);
    location.hash = '#/designer';
  };

  // ---- scene edits ----
  const setFrame = (f) => hist.set((s) => ({ ...s, frames: s.frames.map((x, i) => (i === sel ? f : x)) }));
  const addFrame = () => { hist.set((s) => ({ ...s, frames: [...s.frames.slice(0, sel + 1), newFrame({ text: 'NEXT' }), ...s.frames.slice(sel + 1)] })); setSel(sel + 1); };
  const dupFrame = () => { hist.set((s) => ({ ...s, frames: [...s.frames.slice(0, sel + 1), { ...frame }, ...s.frames.slice(sel + 1)] })); setSel(sel + 1); };
  const delFrame = () => { if (scene.frames.length < 2) return; hist.set((s) => ({ ...s, frames: s.frames.filter((_, i) => i !== sel) })); setSel(Math.max(0, sel - 1)); };
  const moveFrame = (d) => {
    const j = sel + d;
    if (j < 0 || j >= scene.frames.length) return;
    hist.set((s) => { const fr = [...s.frames]; [fr[sel], fr[j]] = [fr[j], fr[sel]]; return { ...s, frames: fr }; });
    setSel(j);
  };

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
  const sceneSecs = scene.frames.reduce((a, f) => a + (f.seconds || 0), 0);

  // An animated frame is a stream of still frames: show the first one, the engine sends the rest.
  let compiled = null, compileError = null, firstStep = null;
  try {
    if (frame.anim) {
      const step = animStep(frame.anim, { target: fillValues(frame.text), shift: frame.after === 'hold' ? frame.shift : -1 }, 0);
      const text = step.tmpl ? fillValues(step.text) : step.text;
      compiled = compileFrame(toCodes(text), { entrance: -1, ...animStepStyle(step, styleOf(frame), isFiniteAnim(frame.anim)) });
      firstStep = step;
    } else compiled = compileFrame(toCodes(frame.text), styleOf(frame));
  } catch (e) { compileError = e.message; }

  // ---- globe actions ----
  const frameBody = (f) => {
    const b = { text: fillValues(f.text), entrance: f.entrance ?? null, after: f.after };
    if (f.after === 'hold') { b.blink = f.blink; b.shift = f.shift; }
    return b;
  };
  const sendThis = async (quiet) => {
    try {
      if (frame.anim) {
        // animations run in the globe's scene engine, so this plays as a short scene
        const secs = Math.max(15, frame.seconds || 30);
        await api.playFrames([{ ...frame, seconds: 0 }], secs);
        if (!quiet) toast(true, `Playing this animated frame for ${secs} s, then back to the playlist`);
      } else {
        await api.sendFrame(frameBody(frame));
        if (!quiet) toast(true, 'Frame sent; the playlist is paused until you resume it');
      }
    } catch (e) { toast(false, `Could not send: ${e.message}`); }
  };
  useEffect(() => {
    if (!live) return undefined;
    const t = setTimeout(() => sendThis(true), 400);
    return () => clearTimeout(t);
  }, [live, frame]);

  const save = async () => {
    const id = scene.id || slug(scene.name);
    if (!scene.id && library.some((s) => s.id === id) && !confirm(`A scene called “${id}” exists. Replace it?`)) return null;
    setBusy(true);
    try {
      await api.saveScene(id, toStored(scene));
      hist.set((s) => ({ ...s, id }), { replace: true });
      setSavedJson(JSON.stringify(toStored(scene)));
      location.hash = `#/designer/${id}`;
      refreshLibrary();
      toast(true, `Saved “${scene.name}” on the globe`);
      return id;
    } catch (e) { toast(false, `Could not save: ${e.message}`); return null; }
    finally { setBusy(false); }
  };
  const playOnGlobe = async () => {
    try { await api.playFrames(toStored(scene).frames, 0); toast(true, 'Playing once on the globe, then back to the playlist'); }
    catch (e) { toast(false, `Could not play: ${e.message}`); }
  };
  const remove = async () => {
    if (!scene.id || !confirm(`Delete “${scene.name}” from the globe?`)) return;
    try { await api.deleteScene(scene.id); toast(true, 'Deleted'); refreshLibrary(); fromTemplate('blank'); }
    catch (e) { toast(false, e.message); }
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
      toast(true, `Imported “${s.name}”; save it to keep it on the globe`);
    } catch (e) { toast(false, `Could not import: ${e.message}`); }
  };

  return (
    <div>
      <div class="scenebar">
        <div class="scenebar-group">
          <select class="select" value={scene.id || ''} onChange={(e) => e.currentTarget.value && open(e.currentTarget.value)} aria-label="Open a scene">
            <option value="">{scene.id ? '' : 'Unsaved scene'}</option>
            {library.filter((x) => x.id !== 'quick').map((x) => <option key={x.id} value={x.id}>{x.name}{x.builtin ? ' (built in)' : ''}</option>)}
          </select>
          <select class="select" value="" onChange={(e) => { fromTemplate(e.currentTarget.value); e.currentTarget.value = ''; }} aria-label="New scene">
            <option value="">+ New…</option>
            {TEMPLATES.map((t) => <option key={t.key} value={t.key}>{t.name}</option>)}
          </select>
        </div>
        <div class="scenebar-title">
          <input type="text" class="name" value={scene.name} onInput={(e) => hist.set((x) => ({ ...x, name: e.currentTarget.value }))} aria-label="Scene name" />
          <span class={`save-state${dirty ? ' dirty' : ''}`}>{dirty ? 'Unsaved changes' : 'Saved'}{!scene.id && ` · saves as “${slug(scene.name)}”`}</span>
        </div>
        <div class="scenebar-group">
          <button class="btn icon" onClick={hist.undo} disabled={!hist.canUndo} title="Undo (Ctrl+Z)" aria-label="Undo">↶</button>
          <button class="btn icon" onClick={hist.redo} disabled={!hist.canRedo} title="Redo (Ctrl+Shift+Z)" aria-label="Redo">↷</button>
        </div>
        <details class="menu">
          <summary class="btn">More ▾</summary>
          <div class="menu-list">
            <button onClick={exportFile}>Export to file</button>
            <button onClick={() => fileRef.current.click()}>Import from file…</button>
            {scene.id && !scene.builtin && <button class="danger" onClick={remove}>Delete scene</button>}
          </div>
        </details>
        <input ref={fileRef} type="file" accept=".json,application/json" hidden onChange={(e) => { const f = e.currentTarget.files[0]; if (f) importFile(f); e.currentTarget.value = ''; }} />
        <button class="btn primary" onClick={save} disabled={busy || (!dirty && !!scene.id)}>Save</button>
      </div>

      <div class="layout">
        <div class="sticky">
          <Screen scene={preview} runKey={runKey} holdMs={whole && !hovering ? 0 : 4000}
            badge={estimate ? '≈ estimated transition' : animShown ? '≈ animation, not yet seen on the globe' : null} />
          <div class="row controls">
            <button class="btn" onClick={() => setRunKey((k) => k + 1)}>↻ Replay</button>
            <div class="seg" role="group" aria-label="Preview">
              <button aria-pressed={!whole} onClick={() => setWhole(false)}>This frame</button>
              <button aria-pressed={whole} onClick={() => setWhole(true)}>Whole scene</button>
            </div>
            <span class="muted small">{hover !== null ? `Previewing ${fxName(hover)}` : hoverAnim ? `Previewing ${animName(hoverAnim)}` : hoverAnim === null ? 'Previewing no animation' : ''}</span>
          </div>

          <div class="strip-head">
            <h2>Frames</h2>
            <span class="muted small">{scene.frames.length} frame{scene.frames.length === 1 ? '' : 's'} · {sceneSecs ? `${sceneSecs} s, then it loops` : 'stays until the playlist moves on'}</span>
          </div>
          <div class="strip" role="listbox" aria-label="Frames">
            {scene.frames.map((f, i) => (
              <button key={i} role="option" aria-selected={i === sel} class="frame-card" aria-pressed={i === sel} onClick={() => setSel(i)}
                style={{ flexBasis: `${Math.round(Math.min(260, Math.max(150, 110 + (f.seconds || 6) * 8)))}px` }}>
                <Mini frame={f} height={26} dim={i !== sel} />
                <span class="frame-text mono">{i + 1}. {f.text || '(empty)'}</span>
                <span class="frame-meta">{f.anim ? animName(f.anim) || 'Animation' : fxName(f.entrance)} · {f.after}{f.seconds ? ` · ${f.seconds} s` : ''}</span>
              </button>
            ))}
            <button class="frame-card add" onClick={addFrame} title="Add a frame after the selected one">+ Add frame</button>
          </div>
          <div class="row strip-tools">
            <button class="btn small" onClick={() => moveFrame(-1)} disabled={sel === 0} title="Move earlier">← Earlier</button>
            <button class="btn small" onClick={() => moveFrame(1)} disabled={sel >= scene.frames.length - 1} title="Move later">Later →</button>
            <button class="btn small" onClick={dupFrame} title="Ctrl+D">Duplicate</button>
            <button class="btn small" onClick={delFrame} disabled={scene.frames.length < 2}>Delete</button>
          </div>

          <details style="margin-top:16px">
            <summary>Show commands (advanced)</summary>
            {compiled && (
              <div style="margin-top:8px">
                {firstStep && <p class="hint small">First step of the animation; the globe's engine sends one still frame like it every {firstStep.ms} ms or more.</p>}
                <div class="bytes">{hex(compiled.first)}</div>
                {compiled.then && <div class="bytes" style="margin-top:6px">then {hex(compiled.then)}</div>}
              </div>
            )}
          </details>
        </div>

        <div>
          <div class="inspector-head"><h2>Frame {sel + 1}</h2><span class="muted small">of {scene.frames.length} · edits apply to the selected frame</span></div>
          <FrameEditor frame={frame} onChange={setFrame} prevCodes={prev.codes} onHover={setHover} onHoverAnim={setHoverAnim} />
          <div class="actions">
            <div class="row">
              <button class="btn primary" onClick={playOnGlobe}>Play scene on globe ▶</button>
              <button class="btn" onClick={() => sendThis(false)} disabled={!!compileError}>Send this frame</button>
              <label class="check"><input type="checkbox" checked={live} onChange={(e) => setLive(e.currentTarget.checked)} />Live</label>
              <span style="flex:1" />
              <button class="btn" onClick={() => api.resume().then(() => toast(true, 'Playlist resumed'), (e) => toast(false, e.message))}>Resume playlist</button>
            </div>
            {compileError && <div class="toast bad">{compileError}</div>}
          </div>
        </div>
      </div>
    </div>
  );
}
