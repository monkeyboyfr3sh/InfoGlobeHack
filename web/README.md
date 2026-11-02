# InfoGlobe web designer

Preact + Vite app served by the globe at `/designer` (the classic page at `/` stays as the Control/Explorer UI).

- `npm install` once; after that `idf.py build` rebuilds the bundle automatically when `src/` changes
  (output: `hello_world/main/data/designer.html.gz`, committed so the firmware builds without Node).
- `npm run dev` then open `http://localhost:5173/?globe=http://192.168.0.71` to work against the real globe
  (the globe's `/api/v1` allows cross-origin requests).
- `npm test` runs the compiler and simulator tests.

| Path | What |
|---|---|
| `src/lib/protocol.js` | Text encoding (accents → control codes) and frame compiler; mirrors `globe_frame.c` |
| `src/lib/sim.js` | Display simulator: band layout, all 38 transitions from `docs/effects.json`, spin/hold/blink/flash |
| `src/lib/font.js` | 5×7 font (approximation of the globe's glyphs) |
| `src/components/` | Preview screen (front window + unrolled band) and the transition gallery |
| `src/app.jsx` | Designer page |
