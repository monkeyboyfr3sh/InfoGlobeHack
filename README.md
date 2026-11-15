# InfoGlobeHack

The Olympia InfoGlobe is a small spinning dot-matrix globe that was sold to show caller-ID style messages, driven by
an infrared remote link. This project replaces whatever used to feed it with an ESP32 on the network: the ESP32 speaks
the globe's IR protocol, runs a clock and a playlist of scenes on its own, and serves a web app where you design what
the globe shows and watch it in a simulator first.

What makes it interesting:

- **A reverse-engineered IR protocol.** Bit timing, frame formats, the character set (including accented letters
  hidden in the control codes), blink and position bytes were worked out with a protocol explorer and a phone camera:
  [docs/protocol.md](docs/protocol.md).
- **All 38 transition effects mapped**, each described in a structured vocabulary ([docs/effects.json](docs/effects.json))
  that both the firmware and the simulator read.
- **A designer with a simulator** that draws the globe's real font (traced dot by dot from video) and animates every
  transition, so the preview shows what the globe will. The live values (`{clock}`, `{fuzzy}`, `{tz:tyo}`, ...) are
  tested to give the same text on the globe and in the preview.
- **It runs on its own**: clock, playlist, weather, aircraft overhead and values pushed from anything on your network,
  with no PC needed once it is set up.

## Hardware

| Part | What the repo assumes |
|---|---|
| Board | Espressif **ESP32-Ethernet-Kit A V1.2** (ESP32-WROVER-E module, 4 MB flash, PSRAM enabled) |
| IR LED | A 38 kHz infrared LED aimed at the globe, driven from **GPIO4** (the kit's GPIO Header 2) |
| Button (optional) | Active-low push button between **GPIO32** (Header 1) and GND; toggles the clock, a 10 s hold clears the access token |
| Network | **Ethernet** (the kit's IP101 PHY) preferred, **Wi-Fi** always on as the fallback |
| Display | An Olympia InfoGlobe within line of sight of the LED |

The pins are set in menuconfig (`InfoGlobe Configuration`, see [`hello_world/main/Kconfig.projbuild`](hello_world/main/Kconfig.projbuild));
its help text lists the pins to avoid (Ethernet uses GPIO0, 5, 18, 19, 21-23 and 25-27; PSRAM uses 16 and 17).
The repo doesn't record how the LED is wired. An ESP32 pin can only drive an LED weakly, so check your build for a series
resistor or a transistor driver before copying it, and aim the LED at the globe's IR receiver (also not documented
here; find it by trying). The firmware generates the carrier itself (38.54 kHz, RMT peripheral), so the LED needs no
modulator.

## Build and flash

Firmware ([ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **6.1**, folder `hello_world/`):

```sh
cd hello_world
idf.py menuconfig          # InfoGlobe Configuration -> Network: Wi-Fi SSID and password; also time zone, pins
idf.py build
idf.py -p COM5 flash monitor
```

`sdkconfig` is committed with the owner's settings (including a Wi-Fi network): set your own in menuconfig before
building. `idf.py flash` writes the app and the web pages; scenes, playlist, settings and notes live on a separate
`notes` partition that flashing never touches. After the first flash, updates can go over the network:
`POST /ota` with `build/hello_world.bin` (see [docs/api.md](docs/api.md)), or the classic page's Firmware tab.

Web app ([Node.js](https://nodejs.org/) 22.12 or later, which the test runner needs; folder `web/`):

```sh
cd web
npm ci                     # once; after this, idf.py build rebuilds the bundle when web/src changes
npm test                   # unit tests, plus the C-vs-preview check of the live values (needs gcc; skipped without)
npm run build              # writes hello_world/main/data/designer.html.gz, which is committed
npm run dev                # then open http://localhost:5173/?globe=http://<globe ip> to develop against a real globe
```

The built bundle is committed, so the firmware builds without Node.

## Versions and release notes

The version is in `VERSION` (for example `0.9.0-beta`); the firmware build and the web app both read it, and the globe
reports it at `GET /api/v1/version`. What changed in each version is in `CHANGELOG.md`, which the web app shows under
Settings, About. Making a release:

1. Put the new version at the top of `CHANGELOG.md` (`## 0.10.0-beta (2026-12-01)`) with what changed, in plain words.
2. Write the same version in `VERSION` (`npm test` in `web/` checks they agree).
3. Build and flash, commit, then tag it: `git tag -a v0.10.0-beta -m "0.10.0-beta"` and `git push origin v0.10.0-beta`.

## Using it

When the globe joins the network it scrolls its IP address across the display. Then open:

- `http://<ip>/designer`: the web app.
- `http://<ip>/`: the classic Control & Explorer page (quick text, IR link stats and gap tuning, the protocol
  explorer with its notes, firmware upload).

A tour of the web app:

- **Dashboard**: what the globe is showing right now (a live mirror over WebSocket), the playlist, a quick message,
  your scenes.
- **Designer**: scenes as a timeline of frames. Each frame has text with live values, an entrance picked from a gallery
  of the 38 transitions (shown animated), what happens afterwards (spin, hold, flash, blink the first letters), and how
  long it stays. A width meter shows what fits in the globe's front window. Save to the globe, play now, export, import.
- **Clock**: a clock studio with twenty styles (classic, split-flap with a transition each minute, word clock, roman,
  binary, internet time, moon phase, sunrise and sunset, world clock, countdown, ...), colon blink, 12/24 h, extras such
  as the date or another city, and chimes on the hour; it compiles into ordinary scenes.
- **Data**: values pushed from your network, and the weather and aircraft cards.
- **Settings**: the access token that protects everything that changes the globe.

Getting your own data onto the globe is one HTTP request: `POST /api/v1/values {"cpu":42}` and a frame `CPU {var:cpu}`.
[docs/pushed-values.md](docs/pushed-values.md) has recipes for curl, PowerShell, Python, PC stats, 3D-print progress,
Home Assistant, GitHub Actions, Uptime Kuma and Claude Code.

## Repository layout

| Path | What |
|---|---|
| `hello_world/` | The ESP32 firmware (ESP-IDF project; the folder name is historical) |
| `hello_world/main/src/` | IR link (`ir_led_task.c`), frame compiler (`globe_frame.c`), scene engine and playlist (`scene_engine.c`), live values (`live_values.c`, `clock_values.c`), pushed values (`value_store.c`), REST API (`api_v1.c`), web server and older routes (`http_server_task.c`), network (`network.c`), feeds (`feeds.c`, `feed_weather.c`, `feed_aircraft.c`) |
| `hello_world/main/data/` | Files for the `spiffs` partition: the classic page and the built web app |
| `hello_world/host_test/` | Firmware code compiled and tested on a PC |
| `web/` | The web app (Preact + Vite): designer, simulator, dashboard. See [web/README.md](web/README.md) |
| `docs/` | Protocol findings, effect catalogue, API and feature docs (below) |
| `tools/` | `gen_effects.py` (effect table for the firmware from `effects.json`), font tracing and motion calibration from video, `ir_gap_sweep.py` and `effect_twins.py` for the open camera tests |
| `captures/` | Camera footage of the globe (git-ignored) |
| `python/` | An older PyQt desktop client for the raw TCP ports the firmware no longer has; kept for reference |

## Documentation

| Doc | What |
|---|---|
| [docs/protocol.md](docs/protocol.md) | The IR protocol: link layer, character set, frame formats, all 38 effects, open questions and how to film them |
| [docs/effects.json](docs/effects.json) | Machine-readable effect catalogue (names, motion, durations) |
| [docs/live-values.md](docs/live-values.md) | Every `{value}` a frame can show, and the frame options for clocks |
| [docs/pushed-values.md](docs/pushed-values.md) | Recipes for pushing your own data to the globe |
| [docs/api.md](docs/api.md) | The whole HTTP API, the WebSocket mirror and the access token |
| [docs/weather.md](docs/weather.md) | The weather feed (Open-Meteo) and its values |
| [docs/aircraft.md](docs/aircraft.md) | The aircraft-overhead feed and its values |
| [docs/animations.md](docs/animations.md) | Custom animations |
| [docs/plan-designer-and-monitor.md](docs/plan-designer-and-monitor.md) | The original plan for the designer and monitoring (2026-11) |
| [web/README.md](web/README.md) | The web app's structure and development |
| [tools/font-trace/README.md](tools/font-trace/README.md) | How the font was traced from camera stills |
