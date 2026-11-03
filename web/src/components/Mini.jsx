import { useEffect, useRef } from 'preact/hooks';
import { encodeText, TEXT_MAX } from '../lib/protocol.js';
import { stillLayout, tiledLayout, BAND, ROWS, WINDOW, FRONT } from '../lib/sim.js';
import { fillValues } from '../lib/values.js';

/** Draw the front window of a band picture into a small canvas. */
export function drawWindow(canvas, plane, dim = false) {
  if (!canvas) return;
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 120, h = canvas.clientHeight || 28;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const start = FRONT - WINDOW / 2;
  const cw = w / WINDOW, rh = (h - 4) / ROWS;
  ctx.fillStyle = dim ? 'rgba(120,170,255,0.55)' : 'rgb(130,178,255)';
  for (let y = 0; y < ROWS; y++) {
    for (let c = 0; c < WINDOW; c++) {
      if (plane[y * BAND + start + c] > 0.4) ctx.fillRect(c * cw + cw * 0.08, 2 + y * rh + rh * 0.18, Math.max(1, cw * 0.84), rh * 0.64);
    }
  }
}

/** Static thumbnail of a frame as it looks once it has arrived. */
export function Mini({ frame, height = 28, dim = false }) {
  const ref = useRef(null);
  const text = frame ? fillValues(frame.text || '') : '';
  const after = frame ? frame.after : 'spin';
  useEffect(() => {
    const codes = encodeText(text).codes.slice(0, TEXT_MAX);
    const style = { after, blink: 0, shift: frame && typeof frame.shift === 'number' ? frame.shift : -1 };
    const L = after === 'spin' ? tiledLayout(codes) : stillLayout(codes, style);
    drawWindow(ref.current, L.plane, dim);
  }, [text, after, dim]);
  return <canvas ref={ref} class="mini" style={{ height: `${height}px` }} aria-hidden="true" />;
}
