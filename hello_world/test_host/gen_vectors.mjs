// Writes anim_vectors.txt (next to this file) from web/src/lib/anim.js.
//   node hello_world/test_host/gen_vectors.mjs
import { writeFileSync } from 'node:fs';
import { buildVectors } from './vectors.mjs';

const out = new URL('./anim_vectors.txt', import.meta.url);
const text = buildVectors();
writeFileSync(out, text);
console.log(`wrote ${text.split('\n').length - 1} lines to anim_vectors.txt`);
