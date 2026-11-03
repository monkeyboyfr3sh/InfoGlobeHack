import { useMemo, useRef } from 'preact/hooks';
import effectsDoc from '../../../docs/effects.json';
import { encodeText, SUGGEST, TEXT_MAX } from '../lib/protocol.js';
import { textWidth, WINDOW, BAND } from '../lib/sim.js';
import { VALUES, worstCase, hasValues } from '../lib/values.js';
import { Gallery } from './Gallery.jsx';

export const EFFECTS = effectsDoc.effects;
const ACCENT_KEYS = 'à á â ä è é ê ë ì í î ï ò ó ô ö ù ú û ü Ç ñ É ✉'.split(' ');
export const AFTER_OPTIONS = [
  ['spin', 'Spin', 'Text circles the globe (readable from any side)'],
  ['hold', 'Hold still', 'Stays in front; can blink the start of the text'],
  ['flash', 'Flash', 'Whole text flashes on and off'],
  ['instant', 'Instant', 'Still and left-aligned, no transition'],
];

export function WidthMeter({ codes, templated, still = false }) {
  const w = textWidth(codes);
  const n = codes.length;
  const what = templated ? 'At its longest' : '';
  let cls = 'ok', msg = `${what ? what + ': ' : ''}fits the front window (${n} characters)`;
  if (n > TEXT_MAX) { cls = 'bad'; msg = `${what ? what + ', ' : ''}${n} of ${TEXT_MAX} characters: too long for one frame`; }
  else if (w > WINDOW) { cls = 'warn'; msg = `${what ? what + ', ' : ''}${n} characters: wider than the front window, ${still ? 'so the ends wrap out of view' : 'so it reads while spinning'}`; }
  const pct = Math.min(100, (w / (BAND * 0.95)) * 100);
  return (
    <div>
      <div class={`meter ${cls}`}><div style={{ width: `${pct}%` }} /></div>
      <div class={`meter-text ${cls}`}>{msg}</div>
    </div>
  );
}

/**
 * Edits one frame: {text, entrance (number|null), after, blink, shift, seconds}.
 * `showSeconds` adds the "show for" field (scenes); `prevCodes` feeds the gallery.
 */
export function FrameEditor({ frame, onChange, prevCodes, onHover, showSeconds = true, compact = false }) {
  const inputRef = useRef(null);
  const set = (patch) => onChange({ ...frame, ...patch });
  const longest = useMemo(() => encodeText(worstCase(frame.text)), [frame.text]);
  const { bad } = useMemo(() => encodeText(frame.text.replace(/\{[a-z0-9]{1,14}\}/g, '')), [frame.text]);
  const entrance = frame.entrance ?? -1;
  const nChars = longest.codes.length;

  const insert = (s) => {
    const el = inputRef.current;
    const a = el ? el.selectionStart : frame.text.length, b = el ? el.selectionEnd : frame.text.length;
    set({ text: frame.text.slice(0, a) + s + frame.text.slice(b) });
    requestAnimationFrame(() => { if (el) { el.focus(); el.setSelectionRange(a + s.length, a + s.length); } });
  };
  const fixAll = () => set({ text: [...frame.text].map((ch) => (SUGGEST[ch] !== undefined && encodeText(ch).bad.length ? SUGGEST[ch] : ch)).join('') });

  return (
    <div>
      <div class="section">
        <h2>Text</h2>
        <input ref={inputRef} class="big" type="text" value={frame.text} maxLength={95} spellcheck={false}
          onInput={(e) => set({ text: e.currentTarget.value })} aria-label="Text to show" />
        <WidthMeter codes={longest.codes} templated={hasValues(frame.text)} still={frame.after !== 'spin'} />
        {bad.length > 0 && (
          <div class="problem">
            The globe can't draw {[...new Set(bad.map((b) => `“${b.ch}”`))].join(' ')}; they show as blanks.
            {bad.some((b) => SUGGEST[b.ch] !== undefined) && <> <button class="btn small" onClick={fixAll}>Replace with suggestions</button></>}
          </div>
        )}
        <div class="row" style="margin-top:10px">
          <select class="select" value="" onChange={(e) => { if (e.currentTarget.value) insert(`{${e.currentTarget.value}}`); e.currentTarget.value = ''; }}
            aria-label="Insert a live value">
            <option value="">+ Insert live value…</option>
            {VALUES.map((v) => <option key={v.name} value={v.name}>{v.label}</option>)}
          </select>
        </div>
        <div class="chips" style="margin-top:8px" aria-label="Insert a special character">
          {ACCENT_KEYS.map((ch) => <button key={ch} class="chip char" onClick={() => insert(ch)} title={`Insert ${ch}`}>{ch}</button>)}
        </div>
      </div>

      <div class="section">
        <h2>Entrance</h2>
        {!compact && <p class="hint">Hover a tile to preview it with your text; click to choose.</p>}
        <Gallery effects={EFFECTS} codes={encodeText(worstCase(frame.text)).codes.slice(0, TEXT_MAX)} prevCodes={prevCodes}
          selected={entrance} onSelect={(id) => set({ entrance: id < 0 ? null : id })} onHover={onHover || (() => {})} compact={compact} />
      </div>

      <div class="section">
        <h2>Afterwards</h2>
        <div class="chips">
          {AFTER_OPTIONS.map(([v, label, tip]) => (
            <button key={v} class="chip" aria-pressed={frame.after === v} title={tip} onClick={() => set({ after: v })}>{label}</button>
          ))}
        </div>
        <p class="hint" style="margin-top:6px">{AFTER_OPTIONS.find((o) => o[0] === frame.after)[2]}.</p>
        {frame.after === 'hold' && (
          <div style="margin-top:8px">
            <div class="row">
              <label class="row" style="flex:1">Blink the first
                <input type="number" min="0" max={nChars} value={frame.blink}
                  onInput={(e) => set({ blink: Math.max(0, Math.min(nChars, e.currentTarget.valueAsNumber || 0)) })} />
                characters
              </label>
              {nChars > 0 && <button class="btn small" onClick={() => set({ blink: nChars })}>All</button>}
              <button class="btn small" onClick={() => set({ blink: 0 })}>None</button>
            </div>
            <div class="row" style="margin-top:10px">
              <label class="check"><input type="checkbox" checked={frame.shift < 0} onChange={(e) => set({ shift: e.currentTarget.checked ? -1 : nChars })} />Centred</label>
              {frame.shift >= 0 && <>
                <span class="muted">right</span>
                <input type="range" min="0" max={Math.max(16, nChars * 2)} value={frame.shift} onInput={(e) => set({ shift: e.currentTarget.valueAsNumber })} />
                <span class="muted">left</span>
              </>}
            </div>
            {entrance >= 0 && <p class="hint" style="margin-top:6px">The entrance plays first, then the globe switches to still text.</p>}
          </div>
        )}
        {showSeconds && (
          <div class="row" style="margin-top:12px">
            <label class="row">Show for
              <input type="number" min="0" max="3600" step="1" value={frame.seconds}
                onInput={(e) => set({ seconds: Math.max(0, e.currentTarget.valueAsNumber || 0) })} />
              seconds
            </label>
            <span class="muted">{frame.seconds === 0 ? '0 = until the playlist moves on' : ''}</span>
          </div>
        )}
      </div>
    </div>
  );
}
