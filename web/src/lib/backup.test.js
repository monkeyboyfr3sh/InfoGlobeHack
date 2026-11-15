import { describe, it, expect } from 'vitest';
import { checkBackup } from './backupcore.js';

describe('backup files', () => {
  const good = { infoglobe: 'backup', scenes: { date: { name: 'Date', frames: [{ text: '{date}' }] } },
    playlist: { items: [{ scene: 'date', seconds: 10 }], timers: [] }, settings: { units: 'c' } };
  it('accepts a backup and hands back its parts', () => {
    const b = checkBackup(good);
    expect(Object.keys(b.scenes)).toEqual(['date']);
    expect(b.playlist.items).toHaveLength(1);
    expect(b.settings.units).toBe('c');
  });
  it('turns away anything else, saying why', () => {
    expect(() => checkBackup({ name: 'a scene file', frames: [] })).toThrow(/not an InfoGlobe backup/);
    expect(() => checkBackup({ ...good, scenes: { 'Bad Id': good.scenes.date } })).toThrow(/not a scene id/);
    expect(() => checkBackup({ ...good, scenes: { empty: { name: 'x', frames: [] } } })).toThrow(/no messages/);
  });
  it('leaves out what is missing or malformed instead of sending it', () => {
    const b = checkBackup({ infoglobe: 'backup', playlist: { nope: 1 }, settings: [1] });
    expect(b).toEqual({ scenes: {}, playlist: null, settings: null });
  });
});
