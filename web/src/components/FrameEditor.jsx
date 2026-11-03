import { useMemo, useRef } from 'preact/hooks';
import effectsDoc from '../../../docs/effects.json';
import { encodeText, SUGGEST, TEXT_MAX } from '../lib/protocol.js';
import { textWidth, WINDOW, BAND } from '../lib/sim.js';
import { VALUES, worstCase, hasValues } from '../lib/values.js';
import { TransitionPicker } from './TransitionPicker.jsx';

export const EFFECTS = effectsDoc.effects;
const ACCENT_KEYS = 'à á â ä è é ê ë ì í î ï ò ó ô ö ù ú û ü Ç ñ É ✉'.split(' ');
export const AFTER_OPTIONS = [
  ['spin', 'Spin', 'Circles the globe right to left, readable from any side.'],
  ['hold', 'Hold still', 'Stays in the front window. Can blink the first few characters.'],
  ['flash', 'Flash', 'Stays in front and the whole text blinks, about once a second.'],
  ['instant', 'Instant', 'Appears at once, still and left-aligned.'],
];

export function WidthMeter({ codes, templated, still = false }) {
  const w = textWidth(codes);
  const n = codes.length;
  const what = templated ? 'At its longest' : '';
  let cls = 'ok', msg = `${what ? what + ': ' : ''}fits the front window (${n} characters)`;
  if (n > TEXT_MAX) { cls = 'bad'; msg = `${what ? what + ', ' : ''}${n} of ${TEXT_MAX} characters: too long for one frame`; }
  else if (w > WINDOW) { cls = 'warn'; msg = `${what ? what + ', ' : ''}${n} characters: wider than the front window, ${still ? 'so the ends wrap out of view' : 'so it reads while spinning'}`; }
  const pct = Math.min(100, (w / (BAND * 0.95)) * 100);
  const win = (WINDOW / (BAND * 0.95)) * 100;
  return (
    <div class="meter-wrap">
      <div class={`meter ${cls}`}><div style={{ width: `${pct}%` }} /><span class="meter-mark" style={{ left: `${win}%` }} title="Front window" /></div>
      <div class={`meter-text ${cls}`}>{msg}</div>
    </div>
  );
}

function Step({ n, title, children, aside }) {
  return (
    <section class="step">
      <header class="step-head"><span class="step-n">{n}</span><h2>{title}</h2>{aside && <span class="step-aside">{aside}</span>}</header>
      {children}
    </section>
  );
}

/**
 * Edits one frame: {text, entrance (number|null), after, blink, shift, seconds}.
 * `showSeconds` adds the timing step (scenes); `prevCodes` feeds the gallery.
 */
export function FrameEditor({ frame, onChange, prevCodes, onHover, showSeconds = true }) {
  const inputRef = useRef(null);
  const set = (patch) => onChange({ ...frame, ...patch });
  const longest = useMemo(() => encodeText(worstCase(frame.text)), [frame.text]);
  const { bad } = useMemo(() => encodeText(frame.text.replace(/\{[a-z0-9]{1,14}\}/g, '')), [frame.text]);
  const entrance = frame.entrance ?? -1;
  const nChars = longest.codes.length;
  const fx = entrance >= 0 ? EFFECTS[String(entrance)] : null;

  const insert = (s) => {
    const el = inputRef.current;
    const a = el ? el.selectionStart : frame.text.length, b = el ? el.selectionEnd : frame.text.length;
    set({ text: frame.text.slice(0, a) + s + frame.text.slice(b) });
    requestAnimationFrame(() => { if (el) { el.focus(); el.setSelectionRange(a + s.length, a + s.length); } });
  };
  const fixAll = () => set({ text: [...frame.text].map((ch) => (SUGGEST[ch] !== undefined && encodeText(ch).bad.length ? SUGGEST[ch] : ch)).join('') });

  return (
    <div class="editor">
      <Step n="1" title="Text">
        <input ref={inputRef} class="big" type="text" value={frame.text} maxLength={95} spellcheck={false}
          onInput={(e) => set({ text: e.currentTarget.value })} aria-label="Text to show" placeholder="Type what the globe should say" />
        <WidthMeter codes={longest.codes} templated={hasValues(frame.text)} still={frame.after !== 'spin'} />
        {bad.length > 0 && (
          <div class="problem">
            The globe can't draw {[...new Set(bad.map((b) => `“${b.ch}”`))].join(' ')}; they show as blanks.
            {bad.some((b) => SUGGEST[b.ch] !== undefined) && <> <button class="btn small" onClick={fixAll}>Use suggestions</button></>}
          </div>
        )}
        <div class="text-tools">
          <select class="select" value="" onChange={(e) => { if (e.currentTarget.value) insert(`{${e.currentTarget.value}}`); e.currentTarget.value = ''; }}
            aria-label="Insert a live value">
            <option value="">Insert live value…</option>
            {VALUES.map((v) => <option key={v.name} value={v.name}>{v.label}</option>)}
          </select>
          <details class="accents">
            <summary class="btn small">Accents &amp; ✉</summary>
            <div class="chips">
              {ACCENT_KEYS.map((ch) => <button key={ch} class="chip char" onClick={() => insert(ch)} title={`Insert ${ch}`}>{ch}</button>)}
            </div>
          </details>
        </div>
      </Step>

      <Step n="2" title="Transition">
        <TransitionPicker effects={EFFECTS} codes={encodeText(worstCase(frame.text)).codes.slice(0, TEXT_MAX)} prevCodes={prevCodes}
          selected={entrance} onSelect={(id) => set({ entrance: id < 0 ? null : id })} onHover={onHover || (() => {})} />
      </Step>

      <Step n="3" title="Afterwards">
        <div class="seg wide" role="radiogroup" aria-label="Afterwards">
          {AFTER_OPTIONS.map(([v, label]) => (
            <button key={v} role="radio" aria-checked={frame.after === v} aria-pressed={frame.after === v} onClick={() => set({ after: v })}>{label}</button>
          ))}
        </div>
        <p class="hint" style="margin-top:8px">{AFTER_OPTIONS.find((o) => o[0] === frame.after)[2]}</p>
        {frame.after === 'hold' && (
          <div class="sub">
            <div class="row">
              <label class="row">Blink the first
                <input type="number" min="0" max={nChars} value={frame.blink}
                  onInput={(e) => set({ blink: Math.max(0, Math.min(nChars, e.currentTarget.valueAsNumber || 0)) })} />
                characters</label>
              <button class="btn small" onClick={() => set({ blink: 0 })} disabled={!frame.blink}>None</button>
            </div>
            <div class="row" style="margin-top:10px">
              <label class="check"><input type="checkbox" checked={frame.shift < 0} onChange={(e) => set({ shift: e.currentTarget.checked ? -1 : nChars })} />Centred</label>
              {frame.shift >= 0 && <>
                <span class="muted small">right</span>
                <input type="range" min="0" max={Math.max(16, nChars * 2)} value={frame.shift} onInput={(e) => set({ shift: e.currentTarget.valueAsNumber })} aria-label="Position" />
                <span class="muted small">left</span>
              </>}
            </div>
          </div>
        )}
        {fx && frame.after !== 'spin' && <p class="hint small" style="margin-top:6px">The transition plays first ({(fx.durationMs / 1000).toFixed(1)} s), then the text stops in front.</p>}
      </Step>

      {showSeconds && (
        <Step n="4" title="Timing">
          <div class="row">
            <label class="row">Show for
              <input type="number" min="0" max="3600" step="1" value={frame.seconds}
                onInput={(e) => set({ seconds: Math.max(0, e.currentTarget.valueAsNumber || 0) })} />
              seconds</label>
            {[5, 10, 30].map((s) => <button key={s} class="chip" aria-pressed={frame.seconds === s} onClick={() => set({ seconds: s })}>{s} s</button>)}
          </div>
          <p class="hint small" style="margin-top:6px">{frame.seconds === 0 ? '0 = stays until the playlist moves on.' : fx && frame.seconds * 1000 < fx.durationMs ? 'Shorter than the transition, so it will be cut off.' : 'Includes the transition.'}</p>
        </Step>
      )}
    </div>
  );
}
