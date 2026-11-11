import { describe, it, expect } from 'vitest';
import { parseCoords, readings, fits, findPlaces } from './places.js';

const LAF = [
  { name: 'Lafayette', admin1: 'Indiana', admin2: 'Tippecanoe', country: 'United States', country_code: 'US', latitude: 40.4167, longitude: -86.87529, population: 71111 },
  { name: 'Lafayette', admin1: 'Louisiana', admin2: 'Lafayette', country: 'United States', country_code: 'US', latitude: 30.22409, longitude: -92.01984, population: 121374 },
];
const fake = (table) => async (url) => {
  const name = decodeURIComponent(url.match(/name=([^&]*)/)[1]);
  return { ok: true, json: async () => ({ results: table[name.toLowerCase()] || [] }) };
};

describe('place finder', () => {
  it('reads coordinates and map links', () => {
    expect(parseCoords('30.224, -92.02')).toEqual({ lat: 30.224, lon: -92.02 });
    expect(parseCoords('30.224 -92.02')).toEqual({ lat: 30.224, lon: -92.02 });
    expect(parseCoords('30.224N 92.02W')).toEqual({ lat: 30.224, lon: -92.02 });
    expect(parseCoords('https://www.google.com/maps/@30.2240897,-92.0198427,13z')).toEqual({ lat: 30.224, lon: -92.02 });
    expect(parseCoords('Lafayette')).toBeNull();
    expect(parseCoords('70501')).toBeNull();
    expect(parseCoords('95, 10')).toBeNull();
  });
  it('tries the whole text, then name + rest', () => {
    expect(readings('Lafayette Louisiana')).toEqual([{ name: 'Lafayette Louisiana', hint: '' }, { name: 'Lafayette', hint: 'Louisiana' }]);
    expect(readings('Baton Rouge, LA')[0]).toEqual({ name: 'Baton Rouge', hint: 'LA' });
    expect(readings('  ')).toEqual([]);
  });
  it('matches states, abbreviations and countries', () => {
    expect(fits(LAF[1], 'Louisiana')).toBe(true);
    expect(fits(LAF[1], 'LA')).toBe(true);
    expect(fits(LAF[1], 'la usa')).toBe(true);
    expect(fits(LAF[0], 'Louisiana')).toBe(false);
    expect(fits(LAF[0], '')).toBe(true);
  });
  it('finds "Lafayette Louisiana" (the service only knows names)', async () => {
    const r = await findPlaces('Lafayette Louisiana', 'en', fake({ lafayette: LAF }));
    expect(r).toHaveLength(1);
    expect(r[0]).toMatchObject({ name: 'Lafayette, Louisiana, United States', lat: 30.224, lon: -92.02 });
    expect((await findPlaces('lafayette, la', 'en', fake({ lafayette: LAF })))[0].lat).toBe(30.224);
    expect(await findPlaces('Lafayette', 'en', fake({ lafayette: LAF }))).toHaveLength(2);
    expect(await findPlaces('Nowhereville Zz', 'en', fake({}))).toEqual([]);
  });
  it('offers typed coordinates without asking the service', async () => {
    const r = await findPlaces('30.22, -92.02', 'en', () => { throw new Error('no network'); });
    expect(r[0]).toMatchObject({ lat: 30.22, lon: -92.02, coords: true });
  });
});
