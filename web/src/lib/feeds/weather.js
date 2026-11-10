// Weather feed (feed_weather.c): the values it publishes, for the designer.
// VALUES go in the "Insert live value" menu; SAMPLES fill the preview when the
// globe hasn't published a real value yet. docs/weather.md has the details.

export const VALUES = [
  { group: 'Weather', name: 'weather', label: 'Temperature and sky (61 CLOUDY)' },
  { group: 'Weather', name: 'temp', label: 'Temperature (61)' },
  { group: 'Weather', name: 'sky', label: 'Sky (CLOUDY, RAIN, SNOW…)' },
  { group: 'Weather', name: 'feels', label: 'Feels like (58)' },
  { group: 'Weather', name: 'hi', label: "Today's high (64)" },
  { group: 'Weather', name: 'lo', label: "Today's low (48)" },
  { group: 'Weather', name: 'rain', label: 'Chance of rain today (20)' },
  { group: 'Weather', name: 'humid', label: 'Humidity (72)' },
  { group: 'Weather', name: 'wind', label: 'Wind speed (8)' },
  { group: 'Weather', name: 'winddir', label: 'Wind direction (SW)' },
  { group: 'Weather', name: 'tsky', label: "Tomorrow's sky (RAIN)" },
  { group: 'Weather', name: 'thi', label: "Tomorrow's high (66)" },
  { group: 'Weather', name: 'tlo', label: "Tomorrow's low (50)" },
];

export const SAMPLES = {
  weather: '61 CLOUDY', temp: '61', sky: 'CLOUDY', feels: '58', hi: '64', lo: '48', rain: '20',
  humid: '72', wind: '8', winddir: 'SW', tsky: 'RAIN', thi: '66', tlo: '50',
};

// The longest each value gets (the sky words are in feed_weather.c), for length checks.
export const WORST = {
  weather: '-40 HEAVY SNOW', temp: '-40', sky: 'HEAVY SNOW', feels: '-40', hi: '120', lo: '-40', rain: '100',
  humid: '100', wind: '100', winddir: 'NW', tsky: 'HEAVY SNOW', thi: '120', tlo: '-40',
};

// wx_status (no TTL): why there is or isn't weather.
export const STATUS = {
  OK: { label: 'On', cls: 'ok' },
  OFF: { label: 'Off', cls: '', why: 'The weather is turned off, so the globe isn’t fetching it.' },
  'NO LOCATION': { label: 'Needs location', cls: 'warn', why: 'Choose the globe’s location below and it starts fetching straight away.' },
  OFFLINE: { label: 'Offline', cls: 'warn', why: 'The globe can’t reach Open-Meteo. Check that its network has internet access; it keeps trying.' },
  'BAD LOCATION': { label: 'Bad location', cls: 'warn', why: 'Open-Meteo didn’t accept the location. Choose the place again below.' },
  ERROR: { label: 'Error', cls: 'warn', why: 'Open-Meteo sent an answer the globe couldn’t read. It tries again in a few minutes.' },
};

const frame = (f) => ({ entrance: null, after: 'hold', blink: 0, shift: -1, seconds: 6, ...f });

/** The ready-made "weather" scene: short lines held in front, longer ones spin.
 *  {var:x} rather than {x}: with no weather (off, offline, expired) it shows
 *  "--", where a bare name would stay in the text as typed. */
export function weatherScene() {
  return {
    name: 'Weather',
    frames: [
      frame({ text: '{var:weather}', entrance: 16, seconds: 8, onChange: 3 }),      // rises in; fades when it changes
      frame({ text: 'HI {var:hi} LO {var:lo}', entrance: 26, seconds: 6 }),         // letters drop in
      frame({ text: 'FEELS {var:feels}  RAIN {var:rain} PCT', entrance: 12, after: 'spin', seconds: 8 }),
      frame({ text: 'TOMORROW {var:tsky} {var:thi}/{var:tlo}', entrance: 0, after: 'spin', seconds: 8 }),
    ],
  };
}

/** Place search (Open-Meteo geocoding, straight from the browser: it allows any origin). */
export async function searchPlaces(name, lang = 'en') {
  const url = `https://geocoding-api.open-meteo.com/v1/search?name=${encodeURIComponent(name)}&count=6&language=${encodeURIComponent(lang)}&format=json`;
  const r = await fetch(url);
  if (!r.ok) throw new Error(`Place search failed (HTTP ${r.status})`);
  const data = await r.json();
  return (data.results || []).map((p) => ({
    name: [...new Set([p.name, p.admin1, p.country].filter(Boolean))].join(', '),
    lat: Math.round(p.latitude * 1000) / 1000,
    lon: Math.round(p.longitude * 1000) / 1000,
  }));
}

/** "just now", "4 min ago", "2 h ago" from a unix time. */
export function ago(unix, now = Date.now()) {
  const s = Math.max(0, Math.round(now / 1000 - Number(unix)));
  if (!Number.isFinite(s)) return '';
  if (s < 90) return 'just now';
  if (s < 5400) return `${Math.round(s / 60)} min ago`;
  return `${Math.round(s / 3600)} h ago`;
}
