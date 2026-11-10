import { useEffect, useRef } from 'preact/hooks';
import { BAND, ROWS, WINDOW, FRONT } from '../lib/sim.js';

const LED = [70, 140, 255];

function pill(ctx, x, y, w, h) {
  const r = Math.min(h / 2, w / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.fill();
}

function fit(canvas, cssH) {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 600;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(cssH * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(cssH * dpr);
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w, h: cssH };
}

/** The front of the dome: window columns projected onto a cylinder. */
export function drawFront(canvas, pic, cssH = 120) {
  const { ctx, w, h } = fit(canvas, cssH);
  ctx.clearRect(0, 0, w, h);
  const start = FRONT - WINDOW / 2;
  const span = (WINDOW / BAND) * Math.PI * 2;          // angle the window covers
  const R = w / 2 / Math.sin(span / 2) * 0.97;
  const rowH = h / (ROWS + 1.2);
  const y0 = (h - rowH * ROWS) / 2;
  for (let c = 0; c < WINDOW; c++) {
    const th = ((c + 0.5) / WINDOW - 0.5) * span;
    const x = w / 2 + R * Math.sin(th);
    const cw = (R * span / WINDOW) * Math.cos(th);
    const shade = 0.35 + 0.65 * Math.cos(th);
    for (let y = 0; y < ROWS; y++) {
      const v = pic[y * BAND + c + start];
      // unlit dot, faint
      ctx.fillStyle = 'rgba(70,140,255,0.06)';
      pill(ctx, x - cw * 0.42, y0 + y * rowH + rowH * 0.32, cw * 0.84, rowH * 0.36);
      if (v > 0.02) {
        const a = v * shade;
        ctx.fillStyle = `rgba(${LED[0]},${LED[1]},${LED[2]},${0.25 * a})`;
        pill(ctx, x - cw * 0.62, y0 + y * rowH + rowH * 0.1, cw * 1.24, rowH * 0.8);
        ctx.fillStyle = `rgba(${150 + 80 * a},${190 + 50 * a},255,${a})`;
        pill(ctx, x - cw * 0.45, y0 + y * rowH + rowH * 0.3, cw * 0.9, rowH * 0.4);
      }
    }
  }
}

/** The whole band unrolled flat, with the front window marked. */
export function drawBand(canvas, pic, cssH = 34) {
  const { ctx, w, h } = fit(canvas, cssH);
  ctx.clearRect(0, 0, w, h);
  const cw = w / BAND, rh = h / ROWS;
  ctx.fillStyle = 'rgba(70,140,255,0.08)';
  ctx.fillRect((FRONT - WINDOW / 2) * cw, 0, WINDOW * cw, h);
  for (let y = 0; y < ROWS; y++) {
    for (let x = 0; x < BAND; x++) {
      const v = pic[y * BAND + x];
      if (v > 0.02) {
        ctx.fillStyle = `rgba(110,165,255,${v})`;
        ctx.fillRect(x * cw + cw * 0.1, y * rh + rh * 0.2, Math.max(1, cw * 0.8), rh * 0.6);
      }
    }
  }
}

/**
 * Animated preview. `scene` comes from makeScene(); `runKey` restarts playback.
 * Loops: entrance, then the steady state for `holdMs`, then again; a scene
 * with its own loopMs (Infinity for a looping custom animation) uses that.
 */
export function Screen({ scene, runKey, speed = 1, loop = true, holdMs = 4000, badge }) {
  const front = useRef(null);
  const band = useRef(null);
  useEffect(() => {
    if (!scene) return undefined;
    let raf = 0;
    const t0 = performance.now();
    const total = scene.loopMs ?? scene.entranceMs + holdMs;
    const tick = (now) => {
      let ms = (now - t0) * speed;
      if (loop && total > 0 && Number.isFinite(total)) ms %= total;
      const pic = scene.pictureAt(ms);
      if (front.current) drawFront(front.current, pic);
      if (band.current) drawBand(band.current, pic);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [scene, runKey, speed, loop, holdMs]);
  return (
    <div class="screen">
      {badge && <span class="badge" title="Shapes and timings are approximate until calibrated from video">{badge}</span>}
      <canvas ref={front} style="height:120px" aria-label="Front of the globe" />
      <div class="band-label"><span>Whole band, unrolled</span><span>front window shaded</span></div>
      <canvas ref={band} style="height:34px" aria-label="Whole LED band" />
    </div>
  );
}
