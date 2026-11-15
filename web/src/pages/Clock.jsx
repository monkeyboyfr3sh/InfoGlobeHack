import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import effectsDoc from '../../../docs/effects.json';
import { STYLES, CHIMES, DEFAULT_CLOCK, BLINK_RATES, compileClock, upgradeClock } from '../lib/clockstyles.js';
import { makeClockSim } from '../lib/clocksim.js';
import { fillValues, setLocation } from '../lib/values.js';
import { encodeText, TEXT_MAX } from '../lib/protocol.js';
import { stillLayout, tiledLayout } from '../lib/sim.js';
import * as api from '../lib/api.js';
import { Screen } from '../components/Screen.jsx';
import { drawWindow } from '../components/Mini.jsx';
import { TransitionPicker } from '../components/TransitionPicker.jsx';

const EFFECTS = effectsDoc.effects;
const SPEEDS = [[1, 'Real time'], [30, '30×'], [120, '2 min / s']];

/** Thumbnail of a style showing the current time. */
function StyleThumb({ config, styleKey, now }) {
  const ref = useRef(null);
  const { frames } = compileClock({ ...config, style: styleKey, change: undefined, motion: undefined });
  const f = frames[0];
  const text = fillValues(f.text, now, undefined, true);
  useEffect(() => {
    const codes = encodeText(text).codes.slice(0, TEXT_MAX);
    const L = f.after === 'spin' ? tiledLayout(codes) : stillLayout(codes, { after: 'hold', shift: -1 });
    drawWindow(ref.current, L.plane);
  }, [text, f.after]);
  return <canvas ref={ref} class="mini" style="height:30px" aria-hidden="true" />;
}

function Section({ title, hint, children, aside }) {
  return (
    <section class="step">
      <div class="step-head"><h2>{title}</h2>{aside && <span class="step-aside">{aside}</span>}</div>
      {hint && <p class="hint small" style="margin-top:-4px">{hint}</p>}
      {children}
    </section>
  );
}

export function Clock({ toast }) {
  const [c, setC] = useState(DEFAULT_CLOCK);
  const [saved, setSaved] = useState(null);
  const [speed, setSpeed] = useState(30);
  const [simStart, setSimStart] = useState(Date.now());
  const [runKey, setRunKey] = useState(0);
  const [now, setNow] = useState(new Date());
  const [busy, setBusy] = useState(false);
  const set = (patch) => setC((o) => ({ ...o, ...patch }));
  const st = STYLES.find((s) => s.key === c.style) || STYLES[0];
  const change = c.change !== undefined ? c.change : st.change;
  const motion = c.motion !== undefined ? c.motion : st.motion;
  const usesFormat = !!st.uses;

  // load the current clock and location from the globe
  useEffect(() => {
    api.getScene('clock').then((s) => { if (s && s.clock) { const cfg = upgradeClock({ ...DEFAULT_CLOCK, ...s.clock }); setC(cfg); setSaved(JSON.stringify(cfg)); } }, () => {});
    api.getSettings().then((s) => {
      if (Number.isFinite(s.lat)) setLocation({ lat: s.lat, lon: s.lon });
    }, () => {});
    const t = setInterval(() => setNow(new Date()), 10000);
    return () => clearInterval(t);
  }, []);

  const compiled = useMemo(() => compileClock(c), [c]);
  const sim = useMemo(() => makeClockSim(compiled.frames, EFFECTS, {
    speed, start: simStart, chime: compiled.chimeFrames ? { frames: compiled.chimeFrames, every: c.chime.every } : null,
  }), [compiled, speed, simStart, runKey]);

  // simulated clock readout
  const [simNow, setSimNow] = useState('');
  useEffect(() => {
    const t0 = performance.now();
    const id = setInterval(() => {
      const d = new Date(simStart + (performance.now() - t0) * speed);
      setSimNow(d.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit', second: '2-digit' }));
    }, 250);
    return () => clearInterval(id);
  }, [sim]);

  const jumpToHour = () => {
    const d = new Date(); d.setMinutes(59, 50, 0);
    setSimStart(d.getTime()); setSpeed(1); setRunKey((k) => k + 1);
  };
  const minuteSoon = () => {
    const d = new Date(); d.setSeconds(55, 0);
    setSimStart(d.getTime()); setSpeed(1); setRunKey((k) => k + 1);
  };

  const useClock = async () => {
    setBusy(true);
    try {
      await api.saveScene('clock', { name: 'Clock', clock: c, frames: compiled.frames });
      const p = await api.getPlaylist();
      let items = p.items && p.items.length ? p.items : [{ scene: 'clock', seconds: 0 }];
      if (!items.some((it) => it.scene === 'clock')) items = [{ scene: 'clock', seconds: 30 }, ...items];
      const body = { items };
      if (compiled.chimeFrames) {
        await api.saveScene('clock-chime', { name: 'Clock chime', frames: compiled.chimeFrames });
        body.chime = { scene: 'clock-chime', every: c.chime.every };
      }
      await api.savePlaylistRaw(body);
      await api.resume();
      setSaved(JSON.stringify(c));
      toast(true, 'Your clock is on the globe');
    } catch (e) { toast(false, `Could not save: ${e.message}`); }
    finally { setBusy(false); }
  };
  const tryIt = async () => {
    try { await api.playFrames(compiled.frames, 60); toast(true, 'Trying it on the globe for a minute'); }
    catch (e) { toast(false, e.message); }
  };
  const dirty = saved !== JSON.stringify(c);
  const tooWide = compiled.frames.some((f) => encodeText(fillValues(f.text, now, undefined, true)).codes.length > TEXT_MAX);

  return (
    <div class="layout">
      <div class="sticky">
        <Screen scene={sim} runKey={runKey} holdMs={0} />
        <div class="row controls">
          <div class="seg" role="radiogroup" aria-label="Preview speed">
            {SPEEDS.map(([v, l]) => <button key={v} role="radio" aria-checked={speed === v} aria-pressed={speed === v} onClick={() => { setSpeed(v); setSimStart(Date.now()); }}>{l}</button>)}
          </div>
          <button class="btn" onClick={minuteSoon} title="Start the preview 5 seconds before a minute changes">Next minute</button>
          {c.chime.every > 0 && <button class="btn" onClick={jumpToHour} title="Start the preview 10 seconds before the hour">Hear the chime</button>}
        </div>
        <p class="muted small" style="margin-top:6px">Preview clock: <span class="mono">{simNow}</span>{speed > 1 ? ` (fast-forward ${speed}×)` : ''}</p>

        <div class="card" style="margin-top:16px">
          <div class="card-head"><h2>What runs on the globe</h2><span class="muted small">{compiled.frames.length} frame{compiled.frames.length === 1 ? '' : 's'}</span></div>
          <ol class="recipe">
            {compiled.frames.map((f, i) => (
              <li key={i}><span class="mono">{f.text}</span>
                <span class="muted small"> · {f.after}{f.onChange !== undefined ? ` · on change: ${f.onChange === 'random' ? 'surprise' : EFFECTS[f.onChange] ? EFFECTS[f.onChange].name : f.onChange.type || f.onChange}` : ''}{f.motion ? ` · ${f.motion}` : ''}{f.seconds ? ` · ${f.seconds} s` : ''}</span></li>
            ))}
            {compiled.chimeFrames && <li><span class="muted">Chime every {c.chime.every} min:</span> <span class="mono">{compiled.chimeFrames.map((f) => f.text).join(' → ')}</span></li>}
          </ol>
          {tooWide && <div class="problem">It is longer than 35 characters.</div>}
          <div class="card-foot">
            <button class="btn" onClick={tryIt}>Try for a minute</button>
            <span style="flex:1" />
            <button class="btn primary" onClick={useClock} disabled={busy}>{dirty ? 'Use as my clock' : 'In use ✓'}</button>
          </div>
        </div>
      </div>

      <div class="editor">
        <Section title="Style" hint="Every style runs on the globe itself.">
          <div class="style-grid">
            {STYLES.map((s) => (
              <button key={s.key} class="style-card" aria-pressed={c.style === s.key}
                onClick={() => set({ style: s.key, change: undefined, motion: undefined })}>
                <StyleThumb config={c} styleKey={s.key} now={now} />
                <b>{s.name}</b>
                <span class="muted small">{s.desc}</span>
              </button>
            ))}
          </div>
        </Section>

        {usesFormat && (
          <Section title="Format">
            <div class="row">
              <div class="seg" role="radiogroup" aria-label="Hours">
                <button aria-pressed={!c.h24} onClick={() => set({ h24: false })}>12 h</button>
                <button aria-pressed={c.h24} onClick={() => set({ h24: true })}>24 h</button>
              </div>
              {!c.h24 && <label class="check"><input type="checkbox" checked={c.ampm !== false} onChange={(e) => set({ ampm: e.currentTarget.checked })} />AM / PM</label>}
              <label class="check"><input type="checkbox" checked={c.seconds} onChange={(e) => set({ seconds: e.currentTarget.checked })} />Seconds</label>
            </div>
            <div class="form-grid">
              <span class="label">Blinking colon</span>
              <div class="seg" role="radiogroup" aria-label="Blinking colon">
                {BLINK_RATES.map(([v, l]) => (
                  <button key={v} role="radio" aria-checked={c.colon.period === v} aria-pressed={c.colon.period === v}
                    title={v ? `${v / 1000} s per blink` : 'Colon always on'} onClick={() => set({ colon: { ...c.colon, period: v } })}>{l}</button>
                ))}
              </div>
              {c.colon.period > 0 && <>
                <span class="label">Blinks to</span>
                <div class="seg" role="radiogroup" aria-label="Blink look">
                  <button aria-pressed={c.colon.look !== 'dot'} onClick={() => set({ colon: { ...c.colon, look: 'hide' } })}>Nothing (12 30)</button>
                  <button aria-pressed={c.colon.look === 'dot'} onClick={() => set({ colon: { ...c.colon, look: 'dot' } })}>A dot (12.30)</button>
                </div>
              </>}
            </div>
          </Section>
        )}

        <Section title="When the time changes" hint="Play a transition into the new time, like a flip clock.">
          <div class="seg" role="radiogroup" aria-label="Change">
            <button aria-pressed={change === undefined || change === null} onClick={() => set({ change: null })}>Just update</button>
            <button aria-pressed={typeof change === 'number'} onClick={() => set({ change: typeof change === 'number' ? change : 26 })}>Transition</button>
            <button aria-pressed={change === 'random'} onClick={() => set({ change: 'random' })}>Surprise me</button>
          </div>
          {typeof change === 'number' && (
            <div style="margin-top:12px">
              <TransitionPicker effects={EFFECTS} codes={encodeText(fillValues(compiled.frames[0].text, now, undefined, true)).codes}
                prevCodes={encodeText('12:00').codes} selected={change} onSelect={(id) => set({ change: id < 0 ? null : id })} onHover={() => {}} />
            </div>
          )}
        </Section>

        <Section title="Motion" hint="Pendulum swings still text gently left and right.">
          <div class="seg" role="radiogroup" aria-label="Motion">
            <button aria-pressed={!motion} onClick={() => set({ motion: null })}>{st.after === 'spin' && !st.motion ? 'Spin' : 'Still'}</button>
            <button aria-pressed={motion === 'pendulum'} onClick={() => set({ motion: 'pendulum' })}>Pendulum</button>
          </div>
          {motion && (
            <div class="row" style="margin-top:10px">
              <span class="muted small">slow</span>
              <input type="range" min="100" max="500" step="20" value={600 - (c.motionMs || 250)}
                onInput={(e) => set({ motionMs: 600 - e.currentTarget.valueAsNumber })} aria-label="Motion speed" />
              <span class="muted small">fast</span>
            </div>
          )}
        </Section>

        <Section title="Chime">
          <div class="seg" role="radiogroup" aria-label="Chime">
            {[[0, 'Off'], [60, 'Hourly'], [30, 'Every 30 min'], [15, 'Every 15 min']].map(([v, l]) => (
              <button key={v} aria-pressed={c.chime.every === v} onClick={() => set({ chime: { ...c.chime, every: v } })}>{l}</button>
            ))}
          </div>
          {c.chime.every > 0 && (
            <div class="seg" style="margin-top:10px" role="radiogroup" aria-label="Chime look">
              {CHIMES.map((x) => <button key={x.key} aria-pressed={c.chime.look === x.key} onClick={() => set({ chime: { ...c.chime, look: x.key } })}>{x.name}</button>)}
            </div>
          )}
        </Section>
      </div>
    </div>
  );
}
