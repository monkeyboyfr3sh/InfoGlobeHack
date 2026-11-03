# InfoGlobe web designer

Preact + Vite app served by the globe at `/designer` (the classic page at `/` stays as the Control/Explorer UI).
Pages: `#/dashboard` (now showing, playlist, quick message, scenes) and `#/designer[/<scene id>]` (scene editor).

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
| `src/lib/values.js` | Live values (`{time}`, `{date}`, ...); mirrors `scene_subst()` in `scene_engine.c` |
| `src/lib/scenes.js` | Scene model, templates, import/export clean-up |
| `src/lib/history.js` | Undo/redo hook |
| `src/pages/Designer.jsx` | Scene editor: library, templates, frame timeline, frame editor, save/export/import |
| `src/pages/Dashboard.jsx` | Now showing (polls `/api/v1/now`), playlist editor, quick message, scene list |
| `src/app.jsx` | Shell: tabs, hash routing, theme, notifications |
