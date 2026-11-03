// Clock simulator: plays a scene with live values the way the globe's engine
// does (scene_engine.c): change transitions when a value changes (flip clock),
// colon blink, pendulum / drift motion, frame rotation and chimes.
// `speed` fast-forwards the clock (60 = a minute per second) so a minute
// change can be seen without waiting.

import { encodeText, TEXT_MAX } from './protocol.js';
import { makeScene, tiledLayout, stillLayout, spin, stillAt } from './sim.js';
import { fillValues } from './values.js';

const codesOf = (text) => encodeText(text).codes.slice(0, TEXT_MAX);
const frameMs = (f, n) => (f.seconds ? f.seconds * 1000 : n > 1 ? 5000 : 0);

/**
 * frames: [{text, entrance, after, blink, shift, seconds, onChange, motion, motionMs}]
 * opts: {speed = 1, start = Date.now(), chime: {frames, every} | null}
 * Call pictureAt() with increasing ms (it keeps state, like the globe).
 */
export function makeClockSim(frames, effects, opts = {}) {
  const speed = opts.speed || 1, start = opts.start || Date.now();
  const chime = opts.chime && opts.chime.frames && opts.chime.frames.length ? opts.chime : null;
  const distinct = Object.entries(effects).filter(([, e]) => e.sameAs === undefined).map(([id]) => +id);
  const dateAt = (ms) => new Date(start + ms * speed);
  let cur = null;          // {list, idx, f, sc, t0, frameT0, steady, codes, motionT0, chimeEnd}
  let lastChimeKey = null;

  // Show frame idx of list at ms; `entrance` overrides the frame's own (change transitions).
  function show(list, idx, ms, { entrance, frameT0, chimeEnd } = {}) {
    const f = list[idx];
    const d = dateAt(ms);
    const codes = codesOf(fillValues(f.text, d));
    const prev = cur ? { codes: cur.codes, style: { entrance: -1, after: 'spin' } } : null;
    const style = { entrance: entrance ?? (f.entrance ?? -1), after: f.after, blink: f.blink || 0, shift: f.shift ?? -1 };
    const sc = makeScene(prev, { codes, style }, effects);
    cur = { list, idx, f, sc, t0: ms, frameT0: frameT0 ?? ms, steady: fillValues(f.text, d, undefined, true),
      codes, motionT0: ms + sc.entranceMs, chimeEnd };
  }

  return {
    entranceMs: Infinity,
    info(ms) { return { date: dateAt(ms), chiming: !!(cur && cur.chimeEnd), frame: cur ? cur.idx : 0 }; },
    pictureAt(ms) {
      const d = dateAt(ms);
      if (!cur) show(frames, 0, ms);

      // chime: once each time the simulated clock reaches a multiple of `every` minutes
      if (chime && !cur.chimeEnd) {
        const key = Math.floor(d.getTime() / 60000);
        if (lastChimeKey !== null && key !== lastChimeKey && d.getMinutes() % chime.every === 0) {
          const len = chime.frames.reduce((a, f) => a + (f.seconds || 5) * 1000, 0);
          show(chime.frames, 0, ms, { chimeEnd: ms + len });
        }
        lastChimeKey = key;
      }
      if (cur.chimeEnd && ms >= cur.chimeEnd) show(frames, 0, ms);

      // next frame within the scene (or within the chime)
      const fm = frameMs(cur.f, cur.list.length);
      if (fm && ms - cur.frameT0 >= fm && cur.list.length > 1) {
        show(cur.list, (cur.idx + 1) % cur.list.length, ms, { chimeEnd: cur.chimeEnd });
      }

      // a live value changed (minute, date ...) once the entrance has played
      const f = cur.f;
      if (ms - cur.t0 >= cur.sc.entranceMs) {
        const steady = fillValues(f.text, d, undefined, true);
        if (steady !== cur.steady) {
          const oc = f.onChange;
          if (oc !== null && oc !== undefined && oc !== -1) {
            const fx = oc === 'random' ? distinct[Math.floor(Math.random() * distinct.length)] : oc;
            show(cur.list, cur.idx, ms, { entrance: fx, frameT0: cur.frameT0, chimeEnd: cur.chimeEnd });
          } else {
            cur.steady = steady;
          }
        }
      }

      const local = ms - cur.t0;
      if (local < cur.sc.entranceMs) return cur.sc.pictureAt(local);

      // steady state: current text (colon blink), spinning or still (with motion)
      const g = cur.f;
      const codes = codesOf(fillValues(g.text, d));
      cur.codes = codes;
      const after = local - cur.sc.entranceMs;
      if (g.after === 'spin' && !g.motion) return spin(tiledLayout(codes), after);
      const style = { after: g.after === 'spin' ? 'hold' : g.after, blink: g.blink || 0, shift: g.shift ?? -1 };
      if (g.motion === 'pendulum' || g.motion === 'drift') {
        const len = codes.length, span = len * 2 + 1;
        const k = Math.floor((ms - cur.motionT0) / (g.motionMs || (g.motion === 'drift' ? 160 : 250)));
        style.shift = g.motion === 'pendulum'
          ? Math.max(0, len + Math.round(len * Math.sin((2 * Math.PI * k) / (span * 2))))
          : (span - 1) - (((k % span) + span) % span);
      }
      return stillAt(stillLayout(codes, style), style, after);
    },
  };
}
