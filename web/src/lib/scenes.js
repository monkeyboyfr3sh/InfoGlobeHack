// Scene model shared by the designer and dashboard. A scene is stored on the
// globe as {name, frames:[{text, entrance, after, blink, shift, seconds}]};
// frames may also carry anim, onChange, motion, motionMs and blinkColon, and a
// scene made in the Clock studio keeps its settings in "clock".

import { cleanAnim } from './anim.js';

export const newFrame = (over = {}) => ({ text: 'HELLO', entrance: 2, after: 'spin', blink: 0, shift: -1, seconds: 6, ...over });
const animFrame = (text, anim, over = {}) => newFrame({ text, entrance: null, after: 'hold', anim, ...over });

export const TEMPLATES = [
  { key: 'blank', name: 'Blank scene', frames: [newFrame()] },
  { key: 'clock', name: 'Clock', frames: [newFrame({ text: '{clock}', entrance: null, after: 'hold', seconds: 0 })] },
  { key: 'clockdate', name: 'Clock and date', frames: [
    newFrame({ text: '{time} {ampm}', entrance: 16, after: 'hold', seconds: 10 }),
    newFrame({ text: '{date}', entrance: 17, after: 'hold', seconds: 5 })] },
  { key: 'ticker', name: 'Ticker', frames: [newFrame({ text: 'A longer message that circles the globe', entrance: null, after: 'spin', seconds: 20 })] },
  { key: 'alert', name: 'Alert', frames: [
    newFrame({ text: '✉ ALERT', entrance: 37, after: 'flash', seconds: 8 }),
    newFrame({ text: 'CHECK SERVER', entrance: 22, after: 'spin', seconds: 8 })] },
  { key: 'alternate', name: 'Two lines, alternating', frames: [
    newFrame({ text: 'LINE ONE', entrance: 16, after: 'hold', seconds: 4 }),
    newFrame({ text: 'LINE TWO', entrance: 17, after: 'hold', seconds: 4 })] },
  // custom animations (docs/animations.md)
  { key: 'decode', name: 'Animation: decode', frames: [
    animFrame('HELLO WORLD', { type: 'decode' }, { seconds: 6 }),
    animFrame('{time} {ampm}', { type: 'decode', order: 'random' }, { seconds: 8, onChange: 'decode' })] },
  { key: 'typewriter', name: 'Animation: typewriter', frames: [
    animFrame('TYPE IT OUT', { type: 'typewriter' }, { seconds: 6 }),
    animFrame('then spin...', { type: 'typewriter', cursor: '-' }, { after: 'spin', seconds: 8 })] },
  { key: 'counter', name: 'Animation: counters', frames: [
    animFrame('HITS 1234', { type: 'count', steps: 16 }, { seconds: 6 }),
    animFrame('CPU {var:cpu}', { type: 'count' }, { seconds: 10, onChange: 'count' })] },
  { key: 'thinking', name: 'Animation: thinking', frames: [
    animFrame('<=>', { type: 'scanner' }, { seconds: 8 }),
    animFrame('O', { type: 'scanner', trail: 'o.' }, { seconds: 8 }),
    animFrame('HELLO', { type: 'bounce' }, { seconds: 10 })] },
  { key: 'arcade', name: 'Animation: sprites', frames: [
    animFrame('', { type: 'sprites', sprite: 'pacman' }, { seconds: 8 }),
    animFrame('', { type: 'sprites', sprite: 'chase' }, { seconds: 6 }),
    animFrame('', { type: 'sprites', sprite: 'pulse' }, { seconds: 6 }),
    animFrame('', { type: 'sprites', sprite: 'fish' }, { seconds: 8 })] },
  { key: 'automaton', name: 'Animation: automaton', frames: [
    animFrame('', { type: 'automaton', rule: 30 }, { seconds: 40 }),
    animFrame('', { type: 'automaton', rule: 90 }, { seconds: 20 })] },
  { key: 'sequence', name: 'Animation: your own frames', frames: [
    animFrame('( o o )', { type: 'sequence', texts: ['( o o )', '( o o )', '( o o )', '( - - )'], ms: 300 }, { seconds: 8 }),
    animFrame('LOADING', { type: 'sequence', texts: ['LOADING   ', 'LOADING.  ', 'LOADING.. ', 'LOADING...'], ms: 400 }, { seconds: 8 })] },
];

/** A scene id from its name: lowercase letters, digits and dashes (max 23). */
export function slug(name) {
  const s = name.toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '')
    .replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 23);
  return s || 'scene';
}

/** Frame fields beyond the basics, kept when valid (they used to be dropped on open/save). */
function extras(f) {
  const x = {};
  const anim = cleanAnim(f.anim);
  if (anim) x.anim = anim;
  const oc = f.onChange;
  if (typeof oc === 'number' || typeof oc === 'string') x.onChange = oc;
  else if (oc && typeof oc === 'object' && cleanAnim(oc)) x.onChange = cleanAnim(oc);
  if (f.motion === 'pendulum' || f.motion === 'drift') x.motion = f.motion;
  if (typeof f.motionMs === 'number') x.motionMs = f.motionMs;
  if (f.blinkColon && typeof f.blinkColon === 'object') {
    x.blinkColon = { period: typeof f.blinkColon.period === 'number' ? f.blinkColon.period : 0, look: f.blinkColon.look === 'dot' ? 'dot' : 'hide' };
  }
  return x;
}

/** Normalise a scene from the globe or an imported file. */
export function cleanScene(s) {
  if (!s || !Array.isArray(s.frames) || !s.frames.length) throw new Error('Not a scene file (needs "frames")');
  const out = {
    id: s.id || '',
    name: String(s.name || s.id || 'Untitled'),
    builtin: !!s.builtin,
    frames: s.frames.slice(0, 24).map((f) => newFrame({
      text: String(f.text ?? ''),
      entrance: typeof f.entrance === 'number' ? f.entrance : null,
      after: ['spin', 'hold', 'flash', 'instant'].includes(f.after) ? f.after : 'spin',
      blink: f.blink | 0, shift: typeof f.shift === 'number' ? f.shift : -1,
      seconds: typeof f.seconds === 'number' ? f.seconds : 6,
      ...extras(f),
    })),
  };
  if (s.clock && typeof s.clock === 'object') out.clock = s.clock;   // the Clock studio's settings
  return out;
}

export const toStored = (scene) => ({ name: scene.name, ...(scene.clock ? { clock: scene.clock } : {}), frames: scene.frames });
