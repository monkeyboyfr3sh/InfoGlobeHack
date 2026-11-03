import { useState } from 'preact/hooks';
import { Gallery, GROUP_ORDER } from './Gallery.jsx';

/**
 * The chosen entrance as one line, with a gallery that opens on demand.
 * selected: effect id or -1 (none). Hovering tiles previews them via onHover.
 */
export function TransitionPicker({ effects, codes, prevCodes, selected, onSelect, onHover, startOpen = false }) {
  const [open, setOpen] = useState(startOpen);
  const [filter, setFilter] = useState('All');
  const fx = selected >= 0 ? effects[String(selected)] : null;
  const pick = (id) => { onSelect(id); onHover(null); };
  return (
    <div class="picker">
      <div class="picker-current">
        <div>
          <div class="picker-name">{fx ? fx.name : 'None'}</div>
          <div class="muted small">
            {fx ? <>{fx.group} · FX {selected} · {(fx.durationMs / 1000).toFixed(1)} s
              {fx.calibrated ? <span class="tag ok" title="Length measured from camera footage">measured</span>
                : <span class="tag" title="Not filmed yet; length and look are estimates">estimate</span>}</>
              : 'Text appears without a transition'}
          </div>
        </div>
        <span style="flex:1" />
        {selected >= 0 && <button class="btn small" onClick={() => pick(-1)}>No transition</button>}
        <button class="btn small" aria-expanded={open} onClick={() => setOpen(!open)}>{open ? 'Done' : 'Change…'}</button>
      </div>
      {open && (
        <div class="picker-body">
          <div class="chips filter" role="tablist" aria-label="Transition groups">
            {['All', ...GROUP_ORDER].map((g) => (
              <button key={g} class="chip" role="tab" aria-selected={filter === g} aria-pressed={filter === g} onClick={() => setFilter(g)}>{g}</button>
            ))}
          </div>
          <p class="hint small">Hover to preview with your text, click to choose. Tiles marked ≈ haven't been filmed yet.</p>
          <Gallery effects={effects} codes={codes} prevCodes={prevCodes} selected={selected} filter={filter}
            onSelect={(id) => { pick(id); }} onHover={onHover} />
        </div>
      )}
    </div>
  );
}
