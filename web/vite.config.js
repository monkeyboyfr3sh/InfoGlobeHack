import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

// Every build carries a stamp (index.html <meta name="build">, import.meta.env.VITE_BUILD):
// a page left open asks the globe for its copy now and then and reloads when that one is newer.
process.env.VITE_BUILD = String(Date.now());

// One self-contained HTML file: the ESP32 serves it gzipped as /designer.
export default defineConfig({
  plugins: [preact(), viteSingleFile()],
  server: { fs: { allow: ['..'] } },        // effects.json lives in ../docs
  build: { outDir: 'dist', emptyOutDir: true, target: 'es2020' },
  test: { environment: 'node' },
});
