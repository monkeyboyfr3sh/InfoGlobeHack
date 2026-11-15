// Backup and restore: the scenes, the rotation (with its set times and chime)
// and the settings, in one file. The access token is not in it (the globe never
// hands it out), nor the "quick" scene (whatever quick message ran last).

import * as api from './api.js';
import { VERSION } from './releases.js';
import { checkBackup } from './backupcore.js';

export { checkBackup };

const SKIP = ['quick'];

/** Everything the globe keeps, as one object. */
export async function makeBackup() {
  const list = (await api.getScenes()).filter((s) => !SKIP.includes(s.id) && !s.builtin);
  const scenes = {};
  for (const s of list) {
    const sc = await api.getScene(s.id);
    delete sc.id;
    scenes[s.id] = sc;
  }
  return { infoglobe: 'backup', version: VERSION, made: new Date().toISOString(), scenes, playlist: await api.getPlaylist(), settings: await api.getSettings() };
}

/** Put a backup on the globe: its scenes (others are left alone), then the rotation, then the settings. */
export async function restoreBackup(b, progress = () => {}) {
  const { scenes, playlist, settings } = checkBackup(b);
  const ids = Object.keys(scenes);
  for (let i = 0; i < ids.length; i++) {
    progress(`Scene ${i + 1} of ${ids.length}`);
    await api.saveScene(ids[i], scenes[ids[i]]);
  }
  if (playlist) { progress('The rotation'); await api.savePlaylistRaw(playlist); }
  if (settings) { progress('Settings'); await api.saveSettings(settings); }
  return { scenes: ids.length, playlist: !!playlist, settings: !!settings };
}

/** Hand the browser a file to save. */
export function download(name, obj) {
  const blob = new Blob([JSON.stringify(obj, null, 2)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
