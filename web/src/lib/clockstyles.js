// Clock studio: a clock is a config that compiles into ordinary scenes
// ("clock" and "clock-chime") which the globe's engine plays.

export const STYLES = [
  { key: 'classic', name: 'Classic', desc: 'The original: still in front, colon blinks.', uses: 'clock' },
  { key: 'flip', name: 'Split-flap', desc: 'Digits drop and change like a flip clock.', uses: 'time', change: 26 },
  { key: 'surprise', name: 'Surprise me', desc: 'A different transition every minute.', uses: 'time', change: 'random' },
  { key: 'pendulum', name: 'Pendulum', desc: 'The time swings gently left and right.', uses: 'time', motion: 'pendulum' },
  { key: 'ticker', name: 'Ticker', desc: 'Time and date circling the globe.', text: '{time} {ampm}  {date}', after: 'spin' },
  { key: 'fuzzy', name: 'Word clock', desc: '“QUARTER PAST TEN”, letters shuffle as it changes.', text: '{fuzzy}', after: 'spin', change: 22 },
  { key: 'spoken', name: 'Spoken', desc: '“TEN TWENTY EIGHT”.', text: '{words}', after: 'spin', change: 0 },
  { key: 'roman', name: 'Roman', desc: 'X:XXVIII, crossfades each minute.', text: '{roman}', after: 'hold', change: 3 },
  { key: 'binary', name: 'Binary', desc: 'Hours and minutes in ones and zeros.', text: '{binary}', after: 'hold', change: 36 },
  { key: 'decimal', name: 'Revolutionary', desc: 'French decimal time: 10 hours of 100 minutes.', text: 'DEC {decimal}', after: 'hold', change: 35 },
  { key: 'beat', name: 'Internet time', desc: 'Swatch .beats: 1000 per day, same everywhere.', text: 'BEAT {beat}', after: 'hold', change: 12 },
];

export const EXTRAS = [
  { key: 'date', name: 'Date', desc: 'Mon 02 Nov', frame: { text: '{day} {dd} {mon}', entrance: 16, after: 'spin' } },
  { key: 'daybar', name: 'Day progress', desc: 'DAY ######----', frame: { text: 'DAY {bar:day}', entrance: 12, after: 'spin' } },
  { key: 'moon', name: 'Moon phase', desc: 'WAXING GIBBOUS', frame: { text: '{moon}', entrance: 3, after: 'spin' } },
  { key: 'sun', name: 'Sunrise and sunset', desc: 'needs your location', frame: { text: 'RISE {sunrise} SET {sunset}', entrance: 17, after: 'spin' } },
  { key: 'world', name: 'World clock', desc: 'other cities', frame: null },
  { key: 'countdown', name: 'Countdown', desc: 'days or time until…', frame: null },
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
  extras: [], clockSeconds: 20, extraSeconds: 6,
  world: ['nyc', 'lon', 'tyo'], countdown: { label: 'XMAS', target: '12-25' },
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

  const extras = [];
  for (const key of c.extras) {
    const x = EXTRAS.find((e) => e.key === key);
    if (!x) continue;
    let f = x.frame;
    if (key === 'world') {
      const parts = (c.world || []).slice(0, 3).map((k) => `${k.toUpperCase()} {tz:${k}}`);
      if (!parts.length) continue;
      f = { text: parts.join('  '), entrance: 0, after: 'spin' };
    }
    if (key === 'countdown') {
      const { label, target } = c.countdown || {};
      if (!target) continue;
      f = /^\d{1,2}-\d{1,2}$/.test(target)
        ? { text: `${label || ''} {until:${target}} DAYS`.trim(), entrance: 26, after: 'spin' }
        : { text: `${label || ''} IN {until:${target}}`.trim(), entrance: 26, after: 'spin' };
    }
    extras.push(blankFrame({ ...f, seconds: c.extraSeconds || 6 }));
  }
  if (extras.length) {
    main.seconds = c.clockSeconds || 20;
    // come back to the clock with its own look
    if (main.entrance === null) main.entrance = 17;
  }
  const chime = c.chime && c.chime.every ? (CHIMES.find((x) => x.key === c.chime.look) || CHIMES[0]).frames.map((f) => blankFrame(f)) : null;
  return { frames: [main, ...extras], chimeFrames: chime };
}
