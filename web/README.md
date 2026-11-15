# InfoGlobe web designer

Preact + Vite app served by the globe at `/` (and `/designer`); the classic Control/Explorer page is at `/classic`.
Three pages: `#/home` (now showing, timer, quick message, and the list of what the globe shows), `#/clock` (clock
studio) and `#/settings` (display and night schedule, location, weather, planes; animation pace, scripts, firmware and
the access token folded under "More"). `#/settings/<card>` opens on a card; `#/scenes/<id>` is one scene's full editor,
reached from Edit, More options on Home. Links to the old pages (`#/weather`, `#/planes`, `#/advanced`, ...) still land
in the right place.

- `npm ci` once (Node.js 22.12 or later); after that `idf.py build` rebuilds the bundle automatically when `src/` changes
  (output: `hello_world/main/data/designer.html.gz`, committed so the firmware builds without Node).
- `npm run dev` then open `http://localhost:5173/?globe=http://192.168.0.71` to work against the real globe
  (the globe's `/api/v1` allows cross-origin requests; if it has an access token, the app asks for it on the first change).
- `npm test` runs the compiler, simulator, live-value and client tests. It also compiles the globe's live-value C code
  with gcc and checks it gives the same text as `values.js` (skipped, with a message, when no C compiler is found or
  `SKIP_C_CHECK=1` is set).

| Path | What |
|---|---|
| `src/app.jsx` | Shell: tabs, hash routing, theme, notifications, the access-token prompt |
| `src/pages/Dashboard.jsx` | Home: now showing (live from `live.js`), quick message, and the one list of what the globe shows, where messages are added, edited, reordered and deleted in place |
| `src/pages/Designer.jsx` | One scene's full editor: every option of a message with previews, save/export/import, when it shows |
| `src/components/MessageRows.jsx` | A scene's messages, one block each (words, seconds, entrance, what it does, order); used by Home and the editor |
| `src/pages/Clock.jsx` | Clock studio: styles, colon blink, extras and chimes, compiled into scenes (`clockstyles.js`) |
| `src/pages/Settings.jsx` | Display and night schedule, location and units, the weather and plane feeds, and under "More": animation pace, scripts, firmware, access token |
| `src/components/` | Preview screen (front window + unrolled band), transition gallery and picker, frame editor, mini previews |
| `src/lib/api.js` | The globe's REST API (one function per route); base URL from `?globe=` or `VITE_GLOBE` |
| `src/lib/request.js` | HTTP calls with the optional access token (`Authorization: Bearer`), retry after a 401 |
| `src/lib/live.js` | Live link: WebSocket `/api/v1/ws` for "now showing" and pushed values, polling as the fallback |
| `src/lib/protocol.js` | Text encoding (accents → control codes) and frame compiler; mirrors `globe_frame.c` |
| `src/lib/sim.js` | Display simulator: band layout, all 38 transitions from `docs/effects.json`, spin/hold/blink/flash |
| `src/lib/anim.js` | Custom animations; mirrors `globe_anim.c` (see `docs/animations.md`) |
| `src/lib/clocksim.js` | Plays a clock scene the way the engine does: change transitions, colon blink, motion, chimes |
| `src/lib/font.js` | The globe's 5×7 font, traced from camera footage (`tools/font-trace/`) |
| `src/lib/values.js` | Live values (`{time}`, `{fuzzy}`, `{var:cpu}`, ...); mirrors `live_values.c` and `clock_values.c` |
| `src/lib/values.vectors.json` | Shared test vectors for the live values; `values.vectors.test.js` runs them against `values.js` and the C code |
| `src/lib/feeds/` | Per-feed value names and preview samples for the designer (`weather.js`, `aircraft.js`) |
| `src/lib/scenes.js` | Scene model, templates, import/export clean-up |
| `src/lib/history.js` | Undo/redo hook |
| `scripts/pack.mjs` | Gzips the built page into `hello_world/main/data/designer.html.gz` |
