import { readFileSync, writeFileSync } from 'node:fs';
const traced = JSON.parse(readFileSync(new URL('./glyphs.json', import.meta.url), 'utf8'));
const g = {};
for (const [k, rows] of Object.entries(traced)) g[k] = rows.join('|');
const put = (ch, s) => { const r = s.split('|'); while (r.length < 8) r.push('.'.repeat(r[0].length)); g[ch.codePointAt(0)] = r.join('|'); };
const low = (body, top = '.....|.....') => top + '|' + body;   // rows 0-1 + body from row 2
// p-w group: traced with a corrected cell start (descender of p sat in the margin)
put('p', low('#.##.|##..#|#...#|####.|#....|#....'));
put('q', low('.###.|#..#.|#..#.|.###.|...#.|...##'));
put('r', low('#.##.|##..#|#....|#....|#....'));
put('s', low('.####|#....|.###.|....#|####.'));
put('t', '..#..|..#..|.###.|..#..|..#..|..#.#|...#.');
put('u', low('#...#|#...#|#...#|#..##|.##.#'));
put('v', low('#...#|#...#|#...#|.#.#.|..#..'));
put('w', low('#.#.#|#.#.#|#.#.#|#.#.#|.#.#.'));
// near the frame edges the dome squeezed a column; shapes confirmed by eye / earlier traces
put('"', '.#.#.|.#.#.|.#.#.|.....|.....|.....|.....');
put('(', '...#.|..#..|.#...|.#...|.#...|..#..|...#.');
put('=', '.....|.....|#####|.....|#####|.....|.....');
put('?', '.###.|#...#|#...#|...#.|..#..|.....|..#..');
put('G', '.####|#....|#....|#..##|#...#|#...#|.####');
put('W', '#...#|#...#|#...#|#.#.#|#.#.#|#.#.#|.#.#.');
put('f', '..##.|.#..#|.#...|####.|.#...|.#...|.#...');
put('g', low('.####|#...#|#...#|.####|....#|.###.'));
put('y', low('#...#|#...#|#...#|.####|....#|.###.'));
put('.', '.....|.....|.....|.....|.....|.....|..#..');
put(' ', '.....|.....|.....|.....|.....|.....|.....');
// accented letters: marks and bodies from the control-character footage
const MARK = { grave: '.#...|..#..', acute: '...#.|..#..', circ: '..#..|.#.#.', uml: '.#.#.|.....', tilde: '.##.#|#..#.' };
const BODY = { a: '.###.|#..#.|#..#.|#..#.|.##.#', e: '.###.|#...#|#####|#....|.####', i: '.....|..#..|..#..|..#..|..#..',
               o: '.###.|#...#|#...#|#...#|.###.', u: '#..#.|#..#.|#..#.|#..#.|.##.#', n: '#.##.|##..#|#...#|#...#|#...#' };
const ACC = [[1,'grave','a'],[2,'acute','a'],[3,'circ','a'],[4,'uml','a'],[5,'grave','e'],[6,'acute','e'],[7,'circ','e'],[8,'uml','e'],
  [9,'grave','i'],[10,'acute','i'],[11,'circ','i'],[12,'uml','i'],[13,'grave','o'],[14,'acute','o'],[15,'circ','o'],[16,'uml','o'],
  [17,'grave','u'],[18,'acute','u'],[19,'circ','u'],[20,'uml','u'],[22,'tilde','n']];
for (const [c, m, b] of ACC) g[c] = MARK[m] + '|' + BODY[b] + '|.....';
g[0x15] = '.###.|#...#|#....|#...#|.###.|..#..|.##..|.....';   // Ç (approximate)
g[0x19] = '.#...|..#..|.###.|..#..|..#..|..#..|.###.|.....';   // i-like (unclear)
g[0x1a] = '...#.|#####|#....|####.|#....|#....|#####|.....';   // É (approximate)
const keys = Object.keys(g).map(Number).sort((a, b) => a - b);
for (const k of keys) { const r = g[k].split('|'); if (r.length !== 8 || r.some((x) => x.length !== r[0].length)) throw new Error('bad glyph ' + k + ' ' + g[k]); }
const lines = keys.map((k) => {
  const label = k >= 0x21 && k < 0x7f ? String.fromCharCode(k) : k === 0x20 ? 'space' : k === 0x80 ? 'envelope' : 'code 0x' + k.toString(16);
  return `  0x${k.toString(16).padStart(2, '0')}: '${g[k]}',${' '.repeat(Math.max(1, 52 - g[k].length))}// ${label}`;
});
writeFileSync(new URL('./glyph_table.txt', import.meta.url), lines.join('\n') + '\n');
console.log(keys.length, 'glyphs');
