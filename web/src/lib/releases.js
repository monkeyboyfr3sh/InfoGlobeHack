// Release notes (CHANGELOG.md at the top of the repository), built into the page,
// split into releases for Settings, About. Only the little Markdown the file uses:
// "## version (date)" starts a release, "### heading", "- bullet" (continued on
// indented lines), paragraphs, **bold** and `code`.

import text from '../../../CHANGELOG.md?raw';

/** [{version, date, blocks: [{kind: 'h'|'li'|'p', text}]}], newest first. */
export function parseReleases(md) {
  const out = [];
  let cur = null;
  for (const raw of md.split(/\r?\n/)) {
    const line = raw.trimEnd();
    const rel = /^## (.+?)(?: \(([^)]+)\))?$/.exec(line);
    if (rel) { cur = { version: rel[1], date: rel[2] || '', blocks: [] }; out.push(cur); continue; }
    if (!cur || !line.trim()) { if (cur) cur.blocks.push({ kind: 'gap' }); continue; }
    const last = cur.blocks[cur.blocks.length - 1];
    if (line.startsWith('### ')) cur.blocks.push({ kind: 'h', text: line.slice(4) });
    else if (/^- /.test(line)) cur.blocks.push({ kind: 'li', text: line.slice(2) });
    else if (/^\s+\S/.test(line) && last && (last.kind === 'li' || last.kind === 'p')) last.text += ` ${line.trim()}`;
    else if (last && last.kind === 'p') last.text += ` ${line.trim()}`;
    else cur.blocks.push({ kind: 'p', text: line.trim() });
  }
  for (const r of out) r.blocks = r.blocks.filter((b) => b.kind !== 'gap');
  return out;
}

export const RELEASES = parseReleases(text);
export const VERSION = import.meta.env.VITE_VERSION || (RELEASES[0] && RELEASES[0].version) || '';

/** **bold** and `code` as [{text, b?, code?}] pieces, for rendering without innerHTML. */
export function inline(s) {
  const out = [];
  const re = /\*\*([^*]+)\*\*|`([^`]+)`/g;
  let at = 0, m;
  while ((m = re.exec(s))) {
    if (m.index > at) out.push({ text: s.slice(at, m.index) });
    out.push(m[1] !== undefined ? { text: m[1], b: true } : { text: m[2], code: true });
    at = m.index + m[0].length;
  }
  if (at < s.length) out.push({ text: s.slice(at) });
  return out;
}
