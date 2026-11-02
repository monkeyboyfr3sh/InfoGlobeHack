import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

// One self-contained HTML file: the ESP32 serves it gzipped as /designer.
export default defineConfig({
  plugins: [preact(), viteSingleFile()],
  server: { fs: { allow: ['..'] } },        // effects.json lives in ../docs
  build: { outDir: 'dist', emptyOutDir: true, target: 'es2020' },
  test: { environment: 'node' },
});
