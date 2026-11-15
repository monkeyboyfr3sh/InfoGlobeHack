// Clock studio: a clock is a config that compiles into ordinary scenes
// ("clock" and "clock-chime") which the globe's engine plays.

export const STYLES = [
  { key: 'classic', name: 'Classic', desc: 'The original: still in front, colon blinks.', uses: 'clock' },
  { key: 'flip', name: 'Drop', desc: 'Old digits drop away and the new ones fall into place.', uses: 'time', change: 26 },
  { key: 'surprise', name: 'Surprise me', desc: 'A different transition every minute.', uses: 'time', change: 'random' },
  { key: 'pendulum', name: 'Pendulum', desc: 'The time swings gently left and right.', uses: 'time', motion: 'pendulum' },
  { key: 'ticker', name: 'Ticker', desc: 'Time and date circling the globe.', text: '{time} {ampm}  {date}', after: 'spin' },
  { key: 'fuzzy', name: 'Word clock', desc: '“QUARTER PAST TEN”, letters shuffle as it changes.', text: '{fuzzy}', after: 'spin', change: 22 },
  { key: 'spoken', name: 'Spoken', desc: '“TEN TWENTY EIGHT”.', text: '{words}', after: 'spin', change: 0 },
  { key: 'roman', name: 'Roman', desc: 'X:XXVIII, crossfades each minute.', text: '{roman}', after: 'hold', change: 3 },
  { key: 'binary', name: 'Binary', desc: 'Hours and minutes in ones and zeros.', text: '{binary}', after: 'hold', change: 36 },
  { key: 'decimal', name: 'Revolutionary', desc: 'French decimal time: 10 hours of 100 minutes.', text: 'DEC {decimal}', after: 'hold', change: 35 },
  { key: 'beat', name: 'Internet time', desc: 'Swatch .beats: 1000 per day, same everywhere.', text: 'BEAT {beat}', after: 'hold', change: 12 },
  { key: 'decode', name: 'Decode', desc: 'Each new minute scrambles and locks in, Matrix style.', uses: 'time', change: 'decode' },
  { key: 'flap', name: 'Departure board', desc: 'Each digit flips on through the numbers to the new minute.', uses: 'time', change: 'flap' },
  { key: 'odometer', name: 'Odometer', desc: 'Digits roll up to the new minute like a car’s mileage counter.', uses: 'time',
    change: { type: 'count', mode: 'digits', steps: 8, ms: 90 } },
  { key: 'dissolve', name: 'Dissolve', desc: 'The new minute dissolves in, dot by dot.', uses: 'time', change: 36 },
];

// What used to take turns inside the clock ("extras"): rows of their own on Home
// now, offered switched off until they are wanted (Dashboard.jsx). id = the scene's id.
const row = (text, entrance, seconds = 10) => ({ text, entrance, after: 'spin', blink: 0, shift: -1, seconds });
export const READY_ROWS = [
  { id: 'date', name: 'Date', frames: [row('{day} {dd} {mon}', 16)] },
  { id: 'world', name: 'World clock', frames: [row('NYC {tz:nyc}  LON {tz:lon}  TYO {tz:tyo}', 0, 12)] },
  { id: 'sun', name: 'Sunrise and sunset', frames: [row('RISE {sunrise} SET {sunset}', 17)] },
  { id: 'moon', name: 'Moon phase', frames: [row('{moon}', 3)] },
  { id: 'daybar', name: 'Day progress', frames: [row('DAY {bar:day}', 12)] },
];

export const CHIMES = [
  { key: 'words', name: 'Say it', frames: [{ text: "{fuzzy}", entrance: 24, after: 'flash', seconds: 6 }] },
  { key: 'flash', name: 'Flash the time', frames: [{ text: '{time} {ampm}', entrance: 37, after: 'flash', seconds: 5 }] },
  { key: 'party', name: 'Party', frames: [
    { text: '* * * * * * * *', entrance: 9, after: 'flash', seconds: 3 },
    { text: "IT'S {fuzzy}", entrance: 22, after: 'spin', seconds: 7 }] },
];

export const DEFAULT_CLOCK = {
  style: 'classic', h24: false, seconds: false, ampm: true, colon: { period: 1000, look: 'hide' },
  change: undefined, motion: undefined, motionMs: undefined,

  chime: { every: 0, look: 'words' },
};

const blankFrame = (f) => ({ entrance: null, after: 'hold', blink: 0, shift: -1, seconds: 0, ...f });

/** The clock's main frame text for a style. */
function mainText(c, st) {
  if (st.text) return st.text;
  const ampm = !c.h24 && c.ampm !== false;
  if (st.uses === 'clock') {
    const a = `${c.h24 ? '24' : ''}${c.seconds ? 's' : ''}${!c.h24 && !ampm ? 'x' : ''}`;
    return a ? `{clock:${a}}` : '{clock}';
  }
  // flip / surprise / pendulum use the plain time
  const base = c.h24 ? '{time24}' : '{time}';
  return `${base}${c.seconds ? ':{sec}' : ''}${ampm ? ' {ampm}' : ''}`;
}

export const BLINK_RATES = [[0, 'Off'], [500, 'Fast'], [1000, 'Normal'], [2000, 'Slow']];

/** Older saved configs had blink: true/false. */
export function upgradeClock(c) {
  const out = { ...c };
  if (!out.colon) out.colon = { period: c.blink === false ? 0 : 1000, look: 'hide' };
  if (out.ampm === undefined) out.ampm = true;
  if (out.style === 'drift') { out.style = 'pendulum'; out.motion = undefined; }
  if (out.motion === 'drift') out.motion = undefined;
  delete out.blink;
  // what took turns inside the clock (date, world clock, countdown...) is a row of its own on Home now
  for (const k of ['extras', 'countdown', 'world', 'clockSeconds', 'extraSeconds']) delete out[k];
  return out;
}

/** Compile a clock config to {frames, chimeFrames}. */
export function compileClock(c) {
  const st = STYLES.find((s) => s.key === c.style) || STYLES[0];
  const change = c.change !== undefined ? c.change : st.change;
  const motion = c.motion !== undefined ? c.motion : st.motion;
  const after = motion ? 'hold' : st.after || 'hold';
  const main = blankFrame({ text: mainText(c, st), after });
  if (st.uses) main.blinkColon = { period: (c.colon && c.colon.period) || 0, look: (c.colon && c.colon.look) || 'hide' };
  if (change !== undefined && change !== null) main.onChange = change;
  if (motion) { main.motion = motion; main.motionMs = c.motionMs || (motion === 'drift' ? 160 : 250); }

  const chime = c.chime && c.chime.every ? (CHIMES.find((x) => x.key === c.chime.look) || CHIMES[0]).frames.map((f) => blankFrame(f)) : null;
  return { frames: [main], chimeFrames: chime };
}
