// Live values in frame text, e.g. "SERVER {time}". Mirrors scene_subst() in
// hello_world/main/src/scene_engine.c, so the preview shows what the globe will.

const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
const p2 = (n) => String(n).padStart(2, '0');

export const VALUES = [
  { name: 'clock', label: 'Clock (07:05 PM, blinking colon)' },
  { name: 'time', label: 'Time (7:05)' },
  { name: 'time24', label: 'Time, 24 h (19:05)' },
  { name: 'ampm', label: 'AM / PM' },
  { name: 'sec', label: 'Seconds' },
  { name: 'date', label: 'Date (Mon 02 Nov)' },
  { name: 'day', label: 'Weekday (Monday)' },
  { name: 'dd', label: 'Day of month (02)' },
  { name: 'month', label: 'Month (November)' },
  { name: 'mon', label: 'Month, short (Nov)' },
  { name: 'year', label: 'Year' },
  { name: 'ip', label: "Globe's IP address" },
];

function value(name, d, ip) {
  const h12 = d.getHours() % 12 || 12;
  const ampm = d.getHours() < 12 ? 'AM' : 'PM';
  switch (name) {
    case 'clock': {
      const s = `${p2(h12)}:${p2(d.getMinutes())} ${ampm}`;
      return d.getMilliseconds() >= 500 ? s.slice(0, 2) + ' ' + s.slice(3) : s;
    }
    case 'time': return `${h12}:${p2(d.getMinutes())}`;
    case 'time24': return `${p2(d.getHours())}:${p2(d.getMinutes())}`;
    case 'ampm': return ampm;
    case 'sec': return p2(d.getSeconds());
    case 'date': return `${DAYS[d.getDay()].slice(0, 3)} ${p2(d.getDate())} ${MONTHS[d.getMonth()].slice(0, 3)}`;
    case 'day': return DAYS[d.getDay()];
    case 'dd': return p2(d.getDate());
    case 'month': return MONTHS[d.getMonth()];
    case 'mon': return MONTHS[d.getMonth()].slice(0, 3);
    case 'year': return String(d.getFullYear());
    case 'ip': return ip;
    default: return null;
  }
}

/** Fill in {values}; unknown names stay as typed (as on the globe). */
export function fillValues(text, d = new Date(), ip = (typeof location !== 'undefined' && location.hostname) || '192.168.0.71') {
  return text.replace(/\{([a-z0-9]{1,14})\}/g, (m, name) => {
    const v = value(name, d, ip);
    return v === null ? m : v;
  });
}

/** Longest text a template can produce (for the width meter). */
export function worstCase(text) {
  const wide = new Date(2026, 8, 30, 12, 59, 59, 0);   // Wednesday 30 September, 12:59 PM
  return fillValues(text, wide, '192.168.100.100');
}

export const hasValues = (text) => /\{[a-z0-9]{1,14}\}/.test(text);
