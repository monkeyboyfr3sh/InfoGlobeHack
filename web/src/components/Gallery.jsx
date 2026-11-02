import { useEffect, useMemo, useRef } from 'preact/hooks';
import { makeScene, BAND, ROWS, WINDOW, FRONT } from '../lib/sim.js';

const GROUP_ORDER = ['Slides', 'Wipers', 'Vertical', 'Build-ups', 'Fades', 'Letter swaps', 'Drop & pluck', 'Dissolves'];

function drawMini(canvas, pic) {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 110, h = canvas.clientHeight || 26;
  if (canvas.width !== Math.round(w * dpr)) { canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr); }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const cols = WINDOW, start = FRONT - WINDOW / 2;
  const cw = w / cols, rh = (h - 4) / ROWS;
  for (let y = 0; y < ROWS; y++) {
    for (let c = 0; c < cols; c++) {
      const v = pic[y * BAND + start + c];
      if (v > 0.02) {
        ctx.fillStyle = `rgba(120,170,255,${v})`;
        ctx.fillRect(c * cw + cw * 0.08, 2 + y * rh + rh * 0.15, Math.max(1, cw * 0.84), rh * 0.7);
      }
    }
  }
}

/**
 * Transition picker: one animated tile per distinct effect, grouped.
 * Tiles loop the effect with the user's own text.
 */
export function Gallery({ effects, codes, prevCodes, selected, onSelect, onHover }) {
  const canvases = useRef({});
  const list = useMemo(() => Object.entries(effects)
    .map(([id, e]) => ({ id: Number(id), ...e }))
    .filter((e) => e.sameAs === undefined), [effects]);
  const twins = useMemo(() => {
    const t = {};
    for (const [id, e] of Object.entries(effects)) if (e.sameAs !== undefined) (t[e.sameAs] ||= []).push(Number(id));
    return t;
  }, [effects]);
  const scenes = useMemo(() => {
    const s = {};
    const prev = { codes: prevCodes, style: { entrance: -1, after: 'spin' } };
    for (const e of list) s[e.id] = makeScene(prev, { codes, style: { entrance: e.id, after: 'spin' } }, effects);
    return s;
  }, [list, codes, prevCodes, effects]);

  useEffect(() => {
    let raf = 0, last = 0;
    const t0 = performance.now();
    const tick = (now) => {
      raf = requestAnimationFrame(tick);
      if (now - last < 50) return;            // ~20 fps is plenty for thumbnails
      last = now;
      for (const e of list) {
        const cv = canvases.current[e.id];
        const sc = scenes[e.id];
        if (!cv || !sc) continue;
        const ms = (now - t0) % (sc.entranceMs + 1200);
        drawMini(cv, sc.pictureAt(Math.min(ms, sc.entranceMs)));
      }
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [list, scenes]);

  const groups = GROUP_ORDER.map((g) => [g, list.filter((e) => e.group === g)]).filter(([, l]) => l.length);
  return (
    <div>
      <div class="chips">
        <button class="chip" aria-pressed={selected === -1} onClick={() => onSelect(-1)}
          onMouseEnter={() => onHover(-1)} onMouseLeave={() => onHover(null)}>None (appear at once)</button>
      </div>
      {groups.map(([g, items]) => (
        <div class="gallery-group" key={g}>
          <h3>{g}</h3>
          <div class="tiles">
            {items.map((e) => (
              <button key={e.id} class="tile" aria-pressed={selected === e.id || (twins[e.id] || []).includes(selected)}
                title={twins[e.id] ? `Looks the same as FX ${twins[e.id].join(', ')}` : ''}
                onClick={() => onSelect(e.id)} onMouseEnter={() => onHover(e.id)} onMouseLeave={() => onHover(null)}
                onFocus={() => onHover(e.id)} onBlur={() => onHover(null)}>
                <canvas ref={(el) => { canvases.current[e.id] = el; }} />
                <span class="name">{e.name} <span class="num">FX {e.id}</span></span>
              </button>
            ))}
          </div>
        </div>
      ))}
    </div>
  );
}
