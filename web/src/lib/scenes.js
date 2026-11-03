// Scene model shared by the designer and dashboard. A scene is stored on the
// globe as {name, frames:[{text, entrance, after, blink, shift, seconds}]}.

export const newFrame = (over = {}) => ({ text: 'HELLO', entrance: 2, after: 'spin', blink: 0, shift: -1, seconds: 6, ...over });

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
];

/** A scene id from its name: lowercase letters, digits and dashes (max 23). */
export function slug(name) {
  const s = name.toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '')
    .replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 23);
  return s || 'scene';
}

/** Normalise a scene from the globe or an imported file. */
export function cleanScene(s) {
  if (!s || !Array.isArray(s.frames) || !s.frames.length) throw new Error('Not a scene file (needs "frames")');
  return {
    id: s.id || '',
    name: String(s.name || s.id || 'Untitled'),
    builtin: !!s.builtin,
    frames: s.frames.slice(0, 24).map((f) => newFrame({
      text: String(f.text ?? ''),
      entrance: typeof f.entrance === 'number' ? f.entrance : null,
      after: ['spin', 'hold', 'flash', 'instant'].includes(f.after) ? f.after : 'spin',
      blink: f.blink | 0, shift: typeof f.shift === 'number' ? f.shift : -1,
      seconds: typeof f.seconds === 'number' ? f.seconds : 6,
    })),
  };
}

export const toStored = (scene) => ({ name: scene.name, frames: scene.frames });
