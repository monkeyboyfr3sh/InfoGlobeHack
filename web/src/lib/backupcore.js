// Checking a backup file, with no globe or browser access (so it can be tested on its own).

const ID_RE = /^[a-z0-9-]{1,23}$/;

/** Throws with a reason unless this looks like a backup; returns {scenes, playlist, settings}. */
export function checkBackup(b) {
  if (!b || b.infoglobe !== 'backup') throw new Error('That is not an InfoGlobe backup file');
  const scenes = b.scenes && typeof b.scenes === 'object' ? b.scenes : {};
  for (const [id, s] of Object.entries(scenes)) {
    if (!ID_RE.test(id)) throw new Error(`“${id}” is not a scene id`);
    if (!s || !Array.isArray(s.frames) || !s.frames.length) throw new Error(`Scene “${id}” has no messages`);
  }
  const playlist = b.playlist && Array.isArray(b.playlist.items) ? b.playlist : null;
  const settings = b.settings && typeof b.settings === 'object' && !Array.isArray(b.settings) ? b.settings : null;
  return { scenes, playlist, settings };
}

