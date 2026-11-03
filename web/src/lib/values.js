// Live values in frame text, e.g. "SERVER {time}". Mirrors scene_subst() in
// scene_engine.c and clock_value() in clock_values.c, so the preview shows
// what the globe will.

const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
const p2 = (n) => String(n).padStart(2, '0');
const h12 = (h) => h % 12 || 12;

export const CITIES = {
  nyc: ['New York', 'America/New_York'], chi: ['Chicago', 'America/Chicago'], den: ['Denver', 'America/Denver'],
  la: ['Los Angeles', 'America/Los_Angeles'], hnl: ['Honolulu', 'Pacific/Honolulu'], lon: ['London', 'Europe/London'],
  par: ['Paris', 'Europe/Paris'], ber: ['Berlin', 'Europe/Berlin'], ath: ['Athens', 'Europe/Athens'], dxb: ['Dubai', 'Asia/Dubai'],
  del: ['Delhi', 'Asia/Kolkata'], sgp: ['Singapore', 'Asia/Singapore'], hkg: ['Hong Kong', 'Asia/Hong_Kong'], tyo: ['Tokyo', 'Asia/Tokyo'],
  syd: ['Sydney', 'Australia/Sydney'], akl: ['Auckland', 'Pacific/Auckland'], utc: ['UTC', 'UTC'],
};

export const VALUES = [
  { group: 'Time', name: 'clock', label: 'Clock (07:05 PM, blinking colon)' },
  { group: 'Time', name: 'clock:24', label: 'Clock, 24 h (19:05)' },
  { group: 'Time', name: 'clock:s', label: 'Clock with seconds (07:05:42)' },
  { group: 'Time', name: 'time', label: 'Time (7:05)' },
  { group: 'Time', name: 'time24', label: 'Time, 24 h (19:05)' },
  { group: 'Time', name: 'ampm', label: 'AM / PM' },
  { group: 'Time', name: 'sec', label: 'Seconds' },
  { group: 'Fun time', name: 'fuzzy', label: 'Fuzzy (QUARTER PAST TEN)' },
  { group: 'Fun time', name: 'words', label: 'In words (TEN TWENTY EIGHT)' },
  { group: 'Fun time', name: 'roman', label: 'Roman (X:XXVIII)' },
  { group: 'Fun time', name: 'binary', label: 'Binary (01010:011100)' },
  { group: 'Fun time', name: 'beat', label: 'Swatch internet time (432)' },
  { group: 'Fun time', name: 'decimal', label: 'Decimal time (4:28)' },
  { group: 'Fun time', name: 'unix', label: 'Unix time' },
  { group: 'Date', name: 'date', label: 'Date (Mon 02 Nov)' },
  { group: 'Date', name: 'day', label: 'Weekday (Monday)' },
  { group: 'Date', name: 'dd', label: 'Day of month (02)' },
  { group: 'Date', name: 'month', label: 'Month (November)' },
  { group: 'Date', name: 'mon', label: 'Month, short (Nov)' },
  { group: 'Date', name: 'year', label: 'Year' },
  { group: 'Date', name: 'doy', label: 'Day of the year (306)' },
  { group: 'Date', name: 'week', label: 'Week number' },
  { group: 'Progress', name: 'bar:day', label: 'Day bar (######----)' },
  { group: 'Progress', name: 'bar:hour', label: 'Hour bar' },
  { group: 'Progress', name: 'bar:min', label: 'Minute bar' },
  { group: 'Progress', name: 'bar:year', label: 'Year bar' },
  { group: 'Progress', name: 'pct:day', label: 'Day, percent (43)' },
  { group: 'Progress', name: 'pct:week', label: 'Week, percent' },
  { group: 'Progress', name: 'pct:year', label: 'Year, percent' },
  { group: 'Sky', name: 'moon', label: 'Moon phase (WAXING GIBBOUS)' },
  { group: 'Sky', name: 'moonpct', label: 'Moon lit, percent' },
  { group: 'Sky', name: 'sunrise', label: 'Sunrise (needs location)' },
  { group: 'Sky', name: 'sunset', label: 'Sunset (needs location)' },
  { group: 'World', name: 'tz:nyc', label: 'New York time' },
  { group: 'World', name: 'tz:lon', label: 'London time' },
  { group: 'World', name: 'tz:tyo', label: 'Tokyo time' },
  { group: 'Countdown', name: 'until:12-25', label: 'Days until 25 Dec' },
  { group: 'Countdown', name: 'until:17:00', label: 'Time until 17:00' },
  { group: 'Other', name: 'ip', label: "Globe's IP address" },
];

let LOCATION = null;   // {lat, lon} from the globe's settings
export function setLocation(loc) { LOCATION = loc && Number.isFinite(loc.lat) && Number.isFinite(loc.lon) ? loc : null; }

const ONES = ['ZERO', 'ONE', 'TWO', 'THREE', 'FOUR', 'FIVE', 'SIX', 'SEVEN', 'EIGHT', 'NINE', 'TEN', 'ELEVEN', 'TWELVE',
  'THIRTEEN', 'FOURTEEN', 'FIFTEEN', 'SIXTEEN', 'SEVENTEEN', 'EIGHTEEN', 'NINETEEN'];
const TENS = ['', '', 'TWENTY', 'THIRTY', 'FORTY', 'FIFTY'];
const words = (n) => (n < 20 ? ONES[n] : n % 10 === 0 ? TENS[n / 10 | 0] : `${TENS[n / 10 | 0]} ${ONES[n % 10]}`);
function roman(n) {
  if (n === 0) return 'N';
  const v = [[50, 'L'], [40, 'XL'], [10, 'X'], [9, 'IX'], [5, 'V'], [4, 'IV'], [1, 'I']];
  let s = '';
  for (const [k, r] of v) while (n >= k) { s += r; n -= k; }
  return s;
}
const isLeap = (y) => (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
function dayOfYear(d) { return Math.round((new Date(d.getFullYear(), d.getMonth(), d.getDate()) - new Date(d.getFullYear(), 0, 1)) / 864e5); }
function fraction(what, d) {
  const sec = d.getSeconds() + d.getMilliseconds() / 1000;
  const day = (d.getHours() * 3600 + d.getMinutes() * 60 + sec) / 86400;
  switch (what) {
    case 'min': return sec / 60;
    case 'hour': return (d.getMinutes() * 60 + sec) / 3600;
    case 'week': return (((d.getDay() + 6) % 7) + day) / 7;
    case 'month': return (d.getDate() - 1 + day) / new Date(d.getFullYear(), d.getMonth() + 1, 0).getDate();
    case 'year': return (dayOfYear(d) + day) / (isLeap(d.getFullYear()) ? 366 : 365);
    default: return day;
  }
}
const bar = (f) => '#'.repeat(Math.floor(f * 10 + 1e-4)).padEnd(10, '-');
function moonAge(d) {
  const jd = d.getTime() / 864e5 + 2440587.5;
  const a = (jd - 2451550.1) % 29.530588853;
  return a < 0 ? a + 29.530588853 : a;
}
const MOON = ['NEW MOON', 'WAXING CRESCENT', 'FIRST QUARTER', 'WAXING GIBBOUS', 'FULL MOON', 'WANING GIBBOUS', 'LAST QUARTER', 'WANING CRESCENT'];
function isoWeek(d) {
  const t = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()));
  const day = t.getUTCDay() || 7;
  t.setUTCDate(t.getUTCDate() + 4 - day);
  const y0 = new Date(Date.UTC(t.getUTCFullYear(), 0, 1));
  return p2(Math.ceil(((t - y0) / 864e5 + 1) / 7));
}
// NOAA almanac sunrise/sunset, local hours (same as clock_values.c)
function sunEvent(d, rise) {
  if (!LOCATION) return NaN;
  const D = Math.PI / 180, { lat, lon } = LOCATION;
  const N = dayOfYear(d) + 1, lngHour = lon / 15;
  const t = N + ((rise ? 6 : 18) - lngHour) / 24;
  const M = 0.9856 * t - 3.289;
  const L = (M + 1.916 * Math.sin(M * D) + 0.020 * Math.sin(2 * M * D) + 282.634 + 720) % 360;
  let RA = (Math.atan(0.91764 * Math.tan(L * D)) / D + 720) % 360;
  RA = (RA + Math.floor(L / 90) * 90 - Math.floor(RA / 90) * 90) / 15;
  const sinDec = 0.39782 * Math.sin(L * D), cosDec = Math.cos(Math.asin(sinDec));
  const cosH = (Math.cos(90.833 * D) - sinDec * Math.sin(lat * D)) / (cosDec * Math.cos(lat * D));
  if (cosH > 1 || cosH < -1) return NaN;
  const H = (rise ? 360 - Math.acos(cosH) / D : Math.acos(cosH) / D) / 15;
  const UT = (H + RA - 0.06571 * t - 6.622 - lngHour + 48) % 24;
  return (UT - d.getTimezoneOffset() / 60 + 48) % 24;
}
function hm12(h) {
  if (!Number.isFinite(h)) return '--:--';
  const m = Math.round(h * 60) % 1440;
  return `${h12(Math.floor(m / 60))}:${p2(m % 60)}`;
}
function cityTime(key, d) {
  const c = CITIES[key];
  if (!c) return null;
  const parts = new Intl.DateTimeFormat('en-US', { timeZone: c[1], hour: 'numeric', minute: '2-digit', hour12: false }).formatToParts(d);
  const h = +parts.find((p) => p.type === 'hour').value % 24, m = parts.find((p) => p.type === 'minute').value;
  return `${h12(h)}:${m}`;
}
function until(arg, d) {
  let m = arg.match(/^(\d{1,2}):(\d{2})$/);
  if (m) {
    const diff = ((+m[1] * 60 + +m[2]) - (d.getHours() * 60 + d.getMinutes()) + 1440) % 1440;
    return `${Math.floor(diff / 60)}:${p2(diff % 60)}`;
  }
  m = arg.match(/^(\d{1,2})-(\d{1,2})$/);
  if (m) {
    const today = new Date(d.getFullYear(), d.getMonth(), d.getDate(), 12);
    let t = new Date(d.getFullYear(), +m[1] - 1, +m[2], 12);
    if (t < today) t = new Date(d.getFullYear() + 1, +m[1] - 1, +m[2], 12);
    return String(Math.round((t - today) / 864e5));
  }
  return null;
}

// Colon blink: period = full on+off cycle in ms (0 = none); off half swaps ':' for ' ' or '.'.
function colonBlink(s, d, period, look) {
  if (!period) return s;
  const ms = (Math.floor(d.getTime() / 1000) % 3600) * 1000 + d.getMilliseconds();
  return ms % period < period / 2 ? s : s.replace(/:/g, look === 'dot' ? '.' : ' ');
}

function value(name, arg, d, ip, steady, colon) {
  const hr = d.getHours(), mi = d.getMinutes();
  const ampm = hr < 12 ? 'AM' : 'PM';
  switch (name) {
    case 'clock': {
      const a = arg || '', h24 = a.includes('2'), secs = a.includes('s'), noblink = a.includes('n');
      const showAmpm = !h24 && !a.includes('x');
      const hh = p2(h24 ? hr : h12(hr));
      let s = `${hh}:${p2(mi)}${secs ? `:${p2(d.getSeconds())}` : ''}${showAmpm ? ` ${ampm}` : ''}`;
      if (!steady) s = colon ? colonBlink(s, d, colon.period, colon.look) : colonBlink(s, d, noblink || secs ? 0 : 1000, 'hide');
      return s;
    }
    case 'time': return steady || !colon ? `${h12(hr)}:${p2(mi)}` : colonBlink(`${h12(hr)}:${p2(mi)}`, d, colon.period, colon.look);
    case 'time24': return steady || !colon ? `${p2(hr)}:${p2(mi)}` : colonBlink(`${p2(hr)}:${p2(mi)}`, d, colon.period, colon.look);
    case 'ampm': return ampm;
    case 'sec': return p2(d.getSeconds());
    case 'date': return `${DAYS[d.getDay()].slice(0, 3)} ${p2(d.getDate())} ${MONTHS[d.getMonth()].slice(0, 3)}`;
    case 'day': return DAYS[d.getDay()];
    case 'dd': return p2(d.getDate());
    case 'month': return MONTHS[d.getMonth()];
    case 'mon': return MONTHS[d.getMonth()].slice(0, 3);
    case 'year': return String(d.getFullYear());
    case 'ip': return ip;
    case 'words': {
      const h = words(h12(hr));
      if (mi === 0) return `${h} O'CLOCK`;
      return mi < 10 ? `${h} OH ${words(mi)}` : `${h} ${words(mi)}`;
    }
    case 'fuzzy': {
      let m5 = Math.floor((mi * 60 + d.getSeconds() + 150) / 300) * 5;
      let h = hr + (m5 > 30 ? 1 : 0);
      if (m5 === 60) { m5 = 0; h = hr + 1; }
      h %= 24;
      if (m5 === 0 && (h === 0 || h === 12)) return h ? 'NOON' : 'MIDNIGHT';
      const rel = ['', 'FIVE PAST', 'TEN PAST', 'QUARTER PAST', 'TWENTY PAST', 'TWENTY FIVE PAST', 'HALF PAST',
        'TWENTY FIVE TO', 'TWENTY TO', 'QUARTER TO', 'TEN TO', 'FIVE TO'];
      return m5 === 0 ? `${words(h12(h))} O'CLOCK` : `${rel[m5 / 5]} ${words(h12(h))}`;
    }
    case 'roman': return `${roman(h12(hr))}:${roman(mi)}`;
    case 'binary': return `${hr.toString(2).padStart(5, '0')}:${mi.toString(2).padStart(6, '0')}`;
    case 'beat': return String(Math.floor((((Math.floor(d.getTime() / 1000) + 3600) % 86400) * 10) / 864)).padStart(3, '0');
    case 'decimal': {
      const x = Math.floor((hr * 3600 + mi * 60 + d.getSeconds()) * 100000 / 86400);
      return `${Math.floor(x / 10000)}:${p2(Math.floor(x / 100) % 100)}`;
    }
    case 'unix': return String(Math.floor(d.getTime() / 1000));
    case 'doy': return String(dayOfYear(d) + 1);
    case 'week': return isoWeek(d);
    case 'bar': return bar(fraction(arg || 'day', d));
    case 'pct': return String(Math.floor(fraction(arg || 'day', d) * 100));
    case 'moon': return MOON[Math.floor(moonAge(d) / 29.530588853 * 8 + 0.5) % 8];
    case 'moonpct': return String(Math.round((1 - Math.cos(2 * Math.PI * moonAge(d) / 29.530588853)) / 2 * 100));
    case 'sunrise': return hm12(sunEvent(d, true));
    case 'sunset': return hm12(sunEvent(d, false));
    case 'tz': return arg ? cityTime(arg, d) : null;
    case 'until': return arg ? until(arg, d) : null;
    default: return null;
  }
}

const RE = /\{([a-z0-9]{1,14})(?::([^{}]{1,24}))?\}/g;

/** Fill in {values}; unknown names stay as typed (as on the globe). `steady` = no colon blink. */
export function fillValues(text, d = new Date(), ip = (typeof location !== 'undefined' && location.hostname) || '192.168.0.71', steady = false, colon = null) {
  return text.replace(RE, (m, name, arg) => {
    const v = value(name, arg, d, ip, steady, colon);
    return v === null || v === undefined ? m : v;
  });
}

/** Longest text a template can produce (for the width meter). */
export function worstCase(text) {
  const wide = new Date(2026, 8, 30, 23, 37, 59, 0);   // Wednesday 30 September, 11:37 PM ("TWENTY FIVE TO TWELVE")
  return fillValues(text, wide, '192.168.100.100', true);
}

export const hasValues = (text) => /\{[a-z0-9]{1,14}(?::[^{}]{1,24})?\}/.test(text);
