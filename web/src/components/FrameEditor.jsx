import { useMemo, useRef } from 'preact/hooks';
import effectsDoc from '../../../docs/effects.json';
import { encodeText, SUGGEST, TEXT_MAX } from '../lib/protocol.js';
import { textWidth, WINDOW, BAND } from '../lib/sim.js';
import { VALUES, worstCase, hasValues, fillValues } from '../lib/values.js';
import { ANIMS, normalizeAnim, makeAnimPlayer, animStep, airtimeMs } from '../lib/anim.js';
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

const ANIM_BY = Object.fromEntries(ANIMS.map((a) => [a.type, a]));
/** Display name of a frame's custom animation, or null. */
export const animName = (anim) => (anim && ANIM_BY[anim.type] ? ANIM_BY[anim.type].name : null);
// what a freshly picked type starts with (beyond its defaults)
const STARTERS = { sequence: { texts: ['( o o )', '( o o )', '( o o )', '( - - )'], ms: 300 } };

function AnimOption({ o, value, raw, onChange }) {
  if (o.kind === 'range') {
    const shown = o.key === 'amp' && !value ? 'auto' : `${value} ${o.unit || ''}`;
    return (
      <label class="anim-opt"><span>{o.label}</span>
        <input type="range" min={o.min} max={o.max} step={o.step || 1} value={value} onInput={(e) => onChange(e.currentTarget.valueAsNumber)} />
        <span class="muted small mono">{shown}</span>
      </label>
    );
  }
  if (o.kind === 'choice') {
    const cur = o.key === 'cursor' && !value ? 'none' : value;
    return (
      <div class="anim-opt"><span>{o.label}</span>
        <div class="seg" role="radiogroup" aria-label={o.label}>
          {o.choices.map(([v, label]) => (
            <button key={String(v)} role="radio" aria-checked={String(cur) === String(v)} aria-pressed={String(cur) === String(v)} onClick={() => onChange(v)}>{label}</button>
          ))}
        </div>
      </div>
    );
  }
  if (o.kind === 'bool') {
    return <label class="check anim-check"><input type="checkbox" checked={!!value} onChange={(e) => onChange(e.currentTarget.checked)} />{o.label}</label>;
  }
  const texts = Array.isArray(raw) ? raw : [];   // the texts as typed (the clipped ones would fight the cursor)
  return (
    <label class="anim-opt anim-texts"><span>{o.label}</span>
      <textarea rows={5} spellcheck={false} value={texts.join('\n')} onInput={(e) => onChange(e.currentTarget.value.split('\n').slice(0, 16))} />
      <span class="muted small">{texts.length} of 16 · texts of the same length stay in place · live values work</span>
    </label>
  );
}

/** Pick a custom animation and set its few options; hovering a type previews it. */
function AnimEditor({ frame, set, onHover, sample }) {
  const raw = frame.anim || null;
  const def = raw ? ANIM_BY[raw.type] : null;
  const full = useMemo(() => (raw ? normalizeAnim(raw) : null), [JSON.stringify(raw)]);
  const target = sample || (def && def.sample) || '';
  const info = useMemo(() => {
    if (!full) return null;
    const step = animStep(full, { target }, 0);
    const air = airtimeMs(Math.min(TEXT_MAX, [...step.text].length));
    const p = makeAnimPlayer(full, { target, fill: (s) => fillValues(s) });
    return { air, finite: p.finite, total: p.finite ? p.totalMs : 0 };
  }, [full, target]);
  const choose = (type) => set({ anim: type ? { type, ...(STARTERS[type] || {}) } : undefined });
  const setOpt = (key, v) => set({ anim: { ...raw, [key]: v } });
  const hover = (type) => onHover(type === undefined ? undefined : type ? { type, ...(STARTERS[type] || {}) } : null);
  return (
    <div>
      <div class="anim-types" role="radiogroup" aria-label="Animation">
        {[['', 'None'], ...ANIMS.map((a) => [a.type, a.name])].map(([type, name]) => (
          <button key={type} class="chip" role="radio" aria-checked={(raw ? raw.type : '') === type} aria-pressed={(raw ? raw.type : '') === type}
            onClick={() => { choose(type); onHover(undefined); }} onMouseEnter={() => hover(type)} onMouseLeave={() => hover(undefined)}
            onFocus={() => hover(type)} onBlur={() => hover(undefined)}>{name}</button>
        ))}
      </div>
      {!def && <p class="hint small" style="margin-top:8px">Optional motion made of still frames sent one after another. Hover to preview with your text.</p>}
      {def && (
        <div class="sub">
          <p class="hint" style="margin-top:0">{def.desc}</p>
          {def.type === 'scanner' && [...(sample || '')].length > 6 && <p class="hint small">Short text sweeps best: try <span class="mono">&lt;=&gt;</span> or <span class="mono">*</span>.</p>}
          <div class="anim-opts">
            {def.options.map((o) => <AnimOption key={o.key} o={o} value={full[o.key]} raw={raw[o.key]} onChange={(v) => setOpt(o.key, v)} />)}
          </div>
          {info && (
            <p class="hint small" style="margin-top:8px">
              {info.finite ? `Plays once (about ${(info.total / 1000).toFixed(1)} s), then the frame continues as set in Afterwards. ` : 'Loops for as long as the frame shows. '}
              {full.ms < info.air
                ? `Each step is a frame on the IR link; with this text one takes ${info.air} ms to send, so steps can't be quicker than that.`
                : `Each step is a frame on the IR link (${info.air} ms to send); the globe hasn't been filmed doing this yet, so treat the look as a preview.`}
            </p>
          )}
        </div>
      )}
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
 * Edits one frame: {text, entrance (number|null), after, blink, shift, seconds, anim}.
 * `showSeconds` adds the timing step (scenes); `prevCodes` feeds the gallery;
 * onHoverAnim(anim | null | undefined) previews an animation (undefined = stop).
 */
export function FrameEditor({ frame, onChange, prevCodes, onHover, onHoverAnim, showSeconds = true }) {
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
  const anim = frame.anim ? normalizeAnim(frame.anim) : null;
  const animTotal = useMemo(() => {
    if (!anim) return 0;
    const p = makeAnimPlayer(anim, { target: worstCase(frame.text), fill: (s) => fillValues(s) });
    return p.finite ? p.totalMs : Infinity;
  }, [JSON.stringify(frame.anim), frame.text]);

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
            {[...new Set(VALUES.map((v) => v.group))].map((g) => (
              <optgroup key={g} label={g}>{VALUES.filter((v) => v.group === g).map((v) => <option key={v.name} value={v.name}>{v.label}</option>)}</optgroup>
            ))}
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
        {anim ? (
          <div class="picker-current">
            <div>
              <div class="picker-name">{animName(frame.anim)} animation</div>
              <div class="muted small">{animTotal === Infinity ? 'It loops, so the frame has no transition.' : 'It plays instead of a transition.'}</div>
            </div>
            <span style="flex:1" />
            <button class="btn small" onClick={() => set({ anim: undefined })}>Use a transition</button>
          </div>
        ) : (
          <TransitionPicker effects={EFFECTS} codes={encodeText(worstCase(frame.text)).codes.slice(0, TEXT_MAX)} prevCodes={prevCodes}
            selected={entrance} onSelect={(id) => set({ entrance: id < 0 ? null : id })} onHover={onHover || (() => {})} />
        )}
      </Step>

      <Step n="3" title="Animation" aside="made of still frames">
        <AnimEditor frame={frame} set={set} onHover={onHoverAnim || (() => {})} sample={fillValues(frame.text)} />
      </Step>

      <Step n="4" title="Afterwards">
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
        {fx && !anim && frame.after !== 'spin' && <p class="hint small" style="margin-top:6px">The transition plays first ({(fx.durationMs / 1000).toFixed(1)} s), then the text stops in front.</p>}
        {animTotal === Infinity && <p class="hint small" style="margin-top:6px">With a looping animation the text stays still: Hold keeps its blinking characters, Flash blinks every step, Spin and Instant don't apply.</p>}
      </Step>

      {showSeconds && (
        <Step n="5" title="Timing">
          <div class="row">
            <label class="row">Show for
              <input type="number" min="0" max="3600" step="1" value={frame.seconds}
                onInput={(e) => set({ seconds: Math.max(0, e.currentTarget.valueAsNumber || 0) })} />
              seconds</label>
            {[5, 10, 30].map((s) => <button key={s} class="chip" aria-pressed={frame.seconds === s} onClick={() => set({ seconds: s })}>{s} s</button>)}
          </div>
          <p class="hint small" style="margin-top:6px">{frame.seconds === 0 ? '0 = stays until the playlist moves on.'
            : anim && animTotal !== Infinity && frame.seconds * 1000 < animTotal ? 'Shorter than the animation, so it will be cut off.'
            : anim ? 'Includes the animation.'
            : fx && frame.seconds * 1000 < fx.durationMs ? 'Shorter than the transition, so it will be cut off.' : 'Includes the transition.'}</p>
        </Step>
      )}
    </div>
  );
}
