// Finding the globe's location from whatever someone types: a town, "town state",
// "town, country", a ZIP / postal code, coordinates, or a pasted map link.
// Uses Open-Meteo's geocoding straight from the browser (it allows any origin).
// That service only matches the place NAME, so "Lafayette Louisiana" finds
// nothing by itself: we search for the name and use the rest to pick.

const US = {
  AL: 'Alabama', AK: 'Alaska', AZ: 'Arizona', AR: 'Arkansas', CA: 'California', CO: 'Colorado', CT: 'Connecticut', DE: 'Delaware',
  FL: 'Florida', GA: 'Georgia', HI: 'Hawaii', ID: 'Idaho', IL: 'Illinois', IN: 'Indiana', IA: 'Iowa', KS: 'Kansas', KY: 'Kentucky',
  LA: 'Louisiana', ME: 'Maine', MD: 'Maryland', MA: 'Massachusetts', MI: 'Michigan', MN: 'Minnesota', MS: 'Mississippi', MO: 'Missouri',
  MT: 'Montana', NE: 'Nebraska', NV: 'Nevada', NH: 'New Hampshire', NJ: 'New Jersey', NM: 'New Mexico', NY: 'New York',
  NC: 'North Carolina', ND: 'North Dakota', OH: 'Ohio', OK: 'Oklahoma', OR: 'Oregon', PA: 'Pennsylvania', RI: 'Rhode Island',
  SC: 'South Carolina', SD: 'South Dakota', TN: 'Tennessee', TX: 'Texas', UT: 'Utah', VT: 'Vermont', VA: 'Virginia', WA: 'Washington',
  WV: 'West Virginia', WI: 'Wisconsin', WY: 'Wyoming', DC: 'District of Columbia',
};
const COUNTRY = { usa: 'US', us: 'US', america: 'US', uk: 'GB', england: 'GB', britain: 'GB' };

const fold = (s) => String(s || '').normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase().trim();
const round3 = (n) => Math.round(n * 1000) / 1000;
const okCoords = (lat, lon) => Number.isFinite(lat) && Number.isFinite(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180;

/** Coordinates in the text, or null: "30.22, -92.02", "30.22 -92.02", "30.22N 92.02W", or a map link with @lat,lon / ?q=lat,lon. */
export function parseCoords(text) {
  const t = String(text || '').trim();
  let m = t.match(/[@=](-?\d{1,2}(?:\.\d+)?),\s*(-?\d{1,3}(?:\.\d+)?)/);                       // map links
  if (!m) m = t.match(/^(-?\d{1,2}(?:\.\d+)?)\s*°?\s*([NS])?[\s,;]+(-?\d{1,3}(?:\.\d+)?)\s*°?\s*([EW])?$/i);
  if (!m) return null;
  const four = m.length === 5;
  let lat = parseFloat(m[1]), lon = parseFloat(four ? m[3] : m[2]);
  if (four && m[2] && m[2].toUpperCase() === 'S') lat = -Math.abs(lat);
  if (four && m[4] && m[4].toUpperCase() === 'W') lon = -Math.abs(lon);
  return okCoords(lat, lon) ? { lat: round3(lat), lon: round3(lon) } : null;
}

/** Ways to read a query, best first: [{name, hint}]. "Baton Rouge LA" -> whole thing, then "Baton Rouge" + "LA", then "Baton" + "Rouge LA". */
export function readings(query) {
  const q = String(query || '').replace(/\s+/g, ' ').trim();
  if (!q) return [];
  const out = [];
  const comma = q.split(',').map((s) => s.trim()).filter(Boolean);
  if (comma.length > 1) out.push({ name: comma[0], hint: comma.slice(1).join(' ') });
  out.push({ name: q.replace(/,/g, ' ').replace(/\s+/g, ' '), hint: '' });
  const words = q.replace(/,/g, ' ').split(' ').filter(Boolean);
  for (let n = words.length - 1; n >= 1; n--) out.push({ name: words.slice(0, n).join(' '), hint: words.slice(n).join(' ') });
  const seen = new Set();
  return out.filter((r) => { const k = `${fold(r.name)}|${fold(r.hint)}`; if (seen.has(k)) return false; seen.add(k); return true; });
}

/** Does a geocoding result fit the rest of what was typed (a state, a county, a country, a state abbreviation)? */
export function fits(place, hint) {
  const h = fold(hint);
  if (!h) return true;
  const fields = [place.admin1, place.admin2, place.admin3, place.country, place.country_code].map(fold).filter(Boolean);
  const want = [h];
  const up = hint.trim().toUpperCase();
  if (US[up]) want.push(fold(US[up]));
  if (COUNTRY[h]) want.push(fold(COUNTRY[h]));
  // every word of the hint has to be accounted for: "LA USA", "Louisiana United States"
  const words = h.split(' ');
  const wordOk = (w) => fields.some((f) => f === w || f.split(' ').includes(w)) || (US[w.toUpperCase()] && fields.includes(fold(US[w.toUpperCase()])) && place.country_code === 'US')
    || (COUNTRY[w] && fold(place.country_code) === fold(COUNTRY[w]));
  return want.some((w) => fields.includes(w)) || words.every(wordOk);
}

const shape = (p) => ({
  name: [...new Set([p.name, p.admin1, p.country].filter(Boolean))].join(', '),
  town: p.name,
  region: [...new Set([p.admin2 && p.admin2 !== p.name ? p.admin2 : null, p.admin1, p.country].filter(Boolean))].join(', '),
  lat: round3(p.latitude),
  lon: round3(p.longitude),
  population: p.population || 0,
});

/** Up to `max` places for what was typed. Coordinates and map links come back as one "use these coordinates" entry. */
export async function findPlaces(query, lang = 'en', fetchFn = fetch, max = 6) {
  const c = parseCoords(query);
  if (c) return [{ name: `${c.lat}, ${c.lon}`, town: 'These coordinates', region: `${c.lat}, ${c.lon}`, lat: c.lat, lon: c.lon, population: 0, coords: true }];
  for (const r of readings(query).slice(0, 5)) {
    if (r.name.length < 2) continue;
    const url = `https://geocoding-api.open-meteo.com/v1/search?name=${encodeURIComponent(r.name)}&count=${r.hint ? 40 : max}&language=${encodeURIComponent(lang)}&format=json`;
    const res = await fetchFn(url);
    if (!res.ok) throw new Error(`Place search failed (HTTP ${res.status})`);
    const hits = ((await res.json()).results || []).filter((p) => fits(p, r.hint));
    if (hits.length) return hits.slice(0, max).map(shape);
  }
  return [];
}
