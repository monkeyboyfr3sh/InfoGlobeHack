// C/JS agreement check for the custom animations (globe_anim.c vs web/src/lib/anim.js).
//   node hello_world/test_host/run.mjs
// Regenerates anim_vectors.txt from anim.js, builds anim_host_test with a host C
// compiler (CC, else C:\msys64\ucrt64\bin\gcc.exe if present, else gcc) into
// hello_world/build/test_host, and replays the vectors through the C module.
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { delimiter, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildVectors } from './vectors.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const vectors = join(here, 'anim_vectors.txt');
writeFileSync(vectors, buildVectors());

const msys = 'C:\\msys64\\ucrt64\\bin\\gcc.exe';
const cc = process.env.CC || (existsSync(msys) ? msys : 'gcc');
const env = { ...process.env };
if (/[\\/]/.test(cc)) {   // the compiler's own DLLs must be on the path to run what it built
  const key = Object.keys(env).find((k) => k.toUpperCase() === 'PATH') || 'PATH';
  env[key] = `${dirname(cc)}${delimiter}${env[key] || ''}`;
}
const out = join(here, '..', 'build', 'test_host');
mkdirSync(out, { recursive: true });
const exe = join(out, process.platform === 'win32' ? 'anim_host_test.exe' : 'anim_host_test');
execFileSync(cc, ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-I', join(here, '..', 'main', 'inc'),
  join(here, '..', 'main', 'src', 'globe_anim.c'), join(here, 'anim_host_test.c'), '-o', exe], { stdio: 'inherit', env });
execFileSync(exe, [vectors], { stdio: 'inherit', env });
