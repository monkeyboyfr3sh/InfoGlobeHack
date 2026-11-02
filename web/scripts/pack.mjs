// Gzip the single-file build into the firmware's SPIFFS data folder.
import { readFileSync, writeFileSync } from 'node:fs';
import { gzipSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';

const html = readFileSync(new URL('../dist/index.html', import.meta.url));
const gz = gzipSync(html, { level: 9 });
const out = new URL('../../hello_world/main/data/designer.html.gz', import.meta.url);
writeFileSync(out, gz);
console.log(`designer.html.gz: ${(html.length / 1024).toFixed(1)} KB -> ${(gz.length / 1024).toFixed(1)} KB gzipped`,
            '->', fileURLToPath(out));
