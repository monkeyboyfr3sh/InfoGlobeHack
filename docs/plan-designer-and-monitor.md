# Plan: Globe Designer and Monitor

**Goal:** stop thinking in commands. You design what the globe should *look like* in an editor with a live animated preview, save it, and the globe plays it: on demand, on a schedule, or when something you monitor needs your attention. Later, the base buttons let you acknowledge alerts and flip through pages.

Status: proposal for discussion (2026-11-01). Nothing here is built yet.

---

## 1. What we know (the constraints that shape everything)

| Fact | Consequence for the design |
|---|---|
| The globe shows **text only** in a 7-row dot-matrix font. We can't draw pixels. | "Graphics" means text, symbols and character tricks. Graphs are character-based (§8). |
| **38 transition effects** (0–37), all mapped and named (`docs/effects.json`), and they run *on the globe*. | Smooth motion costs nothing on the link, so the editor should lean on the globe's own effects. |
| Link speed: ~1 kbit/s, **~10 frames/s** for short text; one frame is at most ~40 bytes (~35 characters). | Fine for scenes and alerts, but no video. Animations are sequences of frames plus effects. |
| The camera sees about **8 characters at a time** on the front; the text then spins. | A "frame" is designed around a ~8-character window. Longer text scrolls around the globe. |
| The ESP32 now has Ethernet, Wi-Fi, accurate time with DST, a web server, flash storage and a hardware-timed IR link. | The scene engine and alert logic can run **on the globe itself**, independent of any PC. |
| The base has physical buttons (LEFT, DOWN, SELECT, UP…). | They are not wired to the ESP32 yet; wiring them is a small hardware task (§7). |

**Still unknown, and measured in Phase 0:** the exact character set and font, how many characters fit around the globe, what the `0x05` blink and shift bytes do, the minimum safe gap between frames, and whether any unknown command allows custom glyphs.

---

## 2. Core concepts (the vocabulary the editor uses)

A small set of nouns the user works with. Commands never appear in the normal UI.

- **Frame:** one thing on the display. It has:
  - **text**, which may contain live values such as `{time}` or `{cpu}`,
  - an **entrance** (one of the 38 named transitions, or *none*),
  - a **style** afterwards: spin / hold still / blink, and which characters blink,
  - a **duration**: how long to show it before the next frame.
- **Scene:** an ordered list of frames that plays once or loops. "Clock" is a scene; so is "Server down!".
- **Value (source):** a live number or string the globe knows about: time, date, weather, a server's status, CPU %, a build result. Values come from **sources** (§6).
- **Rule:** *when* a condition is true, *play* a scene at a **priority**, *until* it is resolved or acknowledged.
- **Playlist (ambient rotation):** what the globe shows when nothing needs attention, e.g. clock 20 s → weather 8 s → server summary 8 s, repeat.
- **Priority:** `ambient < info < warning < critical`. Higher priority interrupts lower; when it ends, the globe goes back to where it was.

All of these are stored as JSON on the globe (a renamed and enlarged `userdata` partition), so they survive reboots and reflashes, and can be exported and imported.

---

## 3. Architecture

```
            Browser (Designer, Dashboard, Rules)                       Anything on your network
                     │   REST + WebSocket                                │  webhook / MQTT / polled HTTP
                     ▼                                                   ▼
 ┌──────────────────────────────── ESP32 firmware ─────────────────────────────────────┐
 │  Web/API server ── Scene store (JSON) ── Sources (webhook, HTTP poll, MQTT, clock)  │
 │                         │                         │                                 │
 │                         ▼                         ▼                                 │
 │                 Scene engine  ◄──── Rules & priorities ◄── Buttons (GPIO)            │
 │      (playlist, interrupts, timing, variable substitution)                          │
 │                         │                                                           │
 │                         ▼                                                           │
 │            Frame compiler  (frame → protocol bytes, uses effects table)             │
 │                         │                                                           │
 │                         ▼                                                           │
 │             IR link (RMT, pipelined; already built)  ──────►  Globe                  │
 └─────────────────────────────────────────────────────────────────────────────────────┘
```

Key decisions:

1. **The globe runs the show.** Scenes, playlists, rules and sources run on the ESP32, so nothing breaks when your PC is off. The browser is only for editing and watching.
2. **One effect catalogue, two consumers.** `docs/effects.json` becomes the single source of truth. A build step generates a C table for the firmware (names → effect numbers, typical durations), and the **simulator** in the browser animates each effect from the same parametric description we wrote while mapping (exit motion, entry motion, order, speed). That's why mapping them in a structured vocabulary pays off now.
3. **The frame compiler is the only code that knows about bytes.** Everything above it speaks *frames*. Today's raw command endpoints stay, but move into a "Lab" area for experiments.
4. **Live link to the browser.** A WebSocket pushes "now showing" so the Dashboard mirrors the real globe, and the Designer's *Live* mode can push edits to the globe as you type.
5. **The web UI gets a proper build.** The current single hand-written HTML file won't scale. Plan: a small framework (Preact) built with Vite into a gzipped bundle served from flash (target < 250 KB). The repo gets a `web/` folder, and the firmware build copies the bundle in.

---

## 4. The Designer (main editor)

### 4.1 Layout

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ InfoGlobe   Designer  Dashboard  Alerts  Sources  Settings  Lab                ☾      │
├───────────────┬───────────────────────────────────────────────┬──────────────────────┤
│ LIBRARY       │                 PREVIEW                        │ FRAME 2 of 3          │
│ ▸ Clock       │      ╭───────────────────────────────╮         │ Text                  │
│ ▸ Morning     │      │   ·  ·  S E R V E R   O K  ·  │  ◀ 3D   │ [ SERVER {status}  ]  │
│ ▸ Server down │      ╰───────────────────────────────╯   band  │  8 / 8 visible ▮▮▮▮▮▮ │
│ ▸ Build done  │      ▶ Play   ⟲ Loop   ⏱ 1.0×   ● Live      │  + Insert value ▾      │
│               │                                               │                       │
│ + New scene   │  TIMELINE                                     │ Entrance              │
│               │  ┌────────┐ ┌────────┐ ┌────────┐  ┌───┐      │ ┌────┐┌────┐┌────┐    │
│ TEMPLATES     │  │ 12:59  │→│SERVER  │→│ CPU 42%│  │ + │      │ │Wipe││Push││Drop│ ▾  │
│ ▸ Clock       │  │ Slide→ │ │ Shade  │ │ Rise   │  └───┘      │ └────┘└────┘└────┘    │
│ ▸ Ticker      │  │ 20 s   │ │ 6 s    │ │ 6 s    │             │ Afterwards            │
│ ▸ Alert       │  └────────┘ └────────┘ └────────┘             │ (•) Spin ( ) Hold     │
│ ▸ Countdown   │                                               │ [x] Blink  "OK"       │
│               │                                               │ Show for  [ 6 ] s     │
├───────────────┴───────────────────────────────────────────────┴──────────────────────┤
│  Saved ✓        Send to globe ▶         Show commands (advanced) ▸                    │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 The preview simulator

The most important piece: you should see what the globe will do **before** sending anything.

- **Rendering:** a dot-matrix band drawn on a canvas, 7 rows high, using the globe's real font (captured in Phase 0) and LED-blue dots with a soft glow. Two views:
  - **Front window:** the ~8 characters a person actually sees, curved slightly like the dome.
  - **Unrolled band:** the whole circumference flat, so long text and wrap-around are obvious.
- **Animation:** each of the 38 effects is animated from its catalogue entry. The vocabulary already covers them: slide, wiper, vertical slide with flip, rows, letters/drop/pluck with their orders (even→/odd←, scrambled 2,4,7,6,5,8,3,1…), column and pixel dissolves, fades. Durations get calibrated from the video captures. After the entrance, the steady-state spin and blink are simulated as well.
- **Honesty about fidelity:** the simulator shows a small "≈ preview" badge until an effect's timing has been calibrated. **Live** mode sends every change to the real globe (debounced to about 300 ms) for the final check.
- **Playback controls:** play/pause, loop, speed 0.5×/1×/2×, step frame-by-frame, scrub the timeline.

### 4.3 Picking a transition (the part that replaces numbers)

- A **gallery of 38 animated tiles**, each looping a tiny preview with its name (Pincer, Window shade, Bounce up…), grouped:

  | Group | Effects |
  |---|---|
  | Slides | 0, 1, 2, 4, 5, 6, 7 |
  | Wipers | 12, 13, 14, 20, 21, 37 |
  | Vertical | 16, 17, 18, 19, 24, 25 |
  | Build-ups | 3, 8, 9, 10, 11 |
  | Letter swaps | 15, 22, 23 |
  | Drop / Pluck | 26–33 |
  | Dissolves | 34, 35, 36 |

- **Hover** a tile and the main preview plays it with *your* text. **Click** to apply it.
- A **"feel"** filter: *Calm* (fades, slides), *Playful* (bounce, drop/pluck), *Attention* (shade, dissolve plus blink). It's a quick way to choose without knowing names.
- Look-alikes (e.g. FX 4 = FX 0) are shown once, with a note, so the gallery stays at about 30 distinct choices.

### 4.4 Editing text

- A **visible-width meter** under the text box: green while the text fits the front window, amber when it wraps around the back, red past the ~35-character frame limit.
- **Unsupported characters are underlined**, using the character map from Phase 0, with suggestions (e.g. `°` → `o` if there's no degree glyph).
- **Insert value ▾** adds live fields such as `{time:h:mm}`, `{date:ddd d}`, `{temp}`, `{cpu}`, `{server.status}`. The preview fills them with sample or current values, and the meter uses their worst-case width.
- **Text effects that don't need protocol knowledge:** *center*, *pad*, *pulse a word* (maps to the blink byte once Phase 0 decodes it), *ticker* (long text scrolls via command `04`).

### 4.5 Frames, scenes and the timeline

- **Filmstrip timeline** of frames. Drag to reorder, duplicate (Ctrl+D), delete, and set durations by dragging a frame's right edge.
- **Scene settings:** loop or play once, a total-time readout, and "return to playlist after".
- **Templates** to start from: *Clock* (several formats), *Ticker*, *Alert* (severity styles), *Countdown*, *Big number + label*, *Two-line alternate* (A ↔ B).
- **Undo/redo** (Ctrl+Z / Ctrl+Shift+Z), autosave of drafts, and "unsaved changes" protection.

### 4.6 Saving and using a design

- **Save** stores the scene on the globe. From there:
  - **Play now** (one tap),
  - **Add to playlist**, with a duration,
  - **Use for an alert** (picked in a rule, §6),
  - **Export as a file** to share or back up.
- **Show commands (advanced)** reveals the compiled bytes per frame for debugging. It's collapsed by default and never needed.

---

## 5. The Dashboard (home screen)

What you open most days. It answers *"what's the globe doing, and does anything need me?"*

```
┌───────────────────────────────────────────────────────────────┐
│ NOW SHOWING   ╭──────────────────────╮   Clock · ambient       │
│               │  · 1 2 : 5 9   P M · │   next: Weather in 12 s │
│               ╰──────────────────────╯   ⏸ Pause  ⏭ Skip       │
├───────────────────────────────────────────────────────────────┤
│ ALERTS                                                          │
│  ● CRITICAL  NAS offline           2 min   [Acknowledge][Snooze]│
│  ● warning   Disk 91% on server    1 h     [Acknowledge][Snooze]│
├───────────────────────────────────────────────────────────────┤
│ PLAYLIST   Clock 20s → Weather 8s → Servers 8s      [Edit]      │
│ QUICK      [Do not disturb 1h] [Show message…] [Test alert]     │
└───────────────────────────────────────────────────────────────┘
```

- **Now showing** is a live mirror (WebSocket) using the same simulator.
- **Alerts** are listed by severity with how long they've been active, plus **Acknowledge** (stop showing, keep the record) and **Snooze** (15 min / 1 h / until tomorrow).
- **Quick actions:** do-not-disturb, a one-off message, and a test alert.
- An **activity log** of what was shown and why, which helps when a rule is too noisy.

---

## 6. Monitoring and alerts

### 6.1 Sources (where values come from)

Ordered by usefulness against effort. Webhooks first, because anything can call them.

| Source | How it works | Good for |
|---|---|---|
| **Built-in** | time, date, uptime, IP, link status | the clock scene, health |
| **Webhook** | `POST /api/v1/event` with a token: `{ "source":"nas", "status":"down", "value":…, "message":"…" }` | Uptime Kuma, Home Assistant automations, scripts, CI (GitHub Actions), cron jobs |
| **HTTP poll** | the globe fetches a URL every N seconds and extracts a field (simple JSON path, e.g. `$.cpu`) | REST APIs, a weather API, Prometheus queries |
| **Ping / TCP check** | is host:port reachable? | "is the NAS/router/printer up?" with no other software |
| **MQTT** | subscribe to topics | Home Assistant, Zigbee2MQTT, IoT sensors |
| **Later** | Home Assistant entity API, Prometheus Alertmanager webhook format, GitHub check runs | richer integrations |

Each source has a **test button** showing the last value received, so setup is confidence-building rather than guesswork.

### 6.2 Rules (when to show what)

A sentence-style builder, no code:

> **When** `NAS (ping)` **is** `down` **for** `60 s` → **play** `Server down!` **as** `critical` → **repeat every** `5 min` **until** `resolved or acknowledged`.

Options:
- **Conditions:** equals / not / above / below / changed / missing for N seconds, plus a **hold-off** so flapping doesn't spam.
- **Quiet hours:** e.g. warnings are silent from 23:00 to 07:00, but criticals still show.
- **On resolve:** optionally play a short "NAS back ✓" frame once.
- **Variables:** the alert's own `{message}` and `{value}` can appear in the scene.

### 6.3 Priorities and the attention language

Make severity readable at a glance, consistently:

| Priority | Default look | Behaviour |
|---|---|---|
| ambient | calm transitions (slides, fades), normal spin | the playlist |
| info | a playful one-time entrance (drop/pluck) | plays once, then back to the playlist |
| warning | Window shade entrance and a blinking keyword | repeats every N minutes until acknowledged |
| critical | Bounce or Dissolve, all-blink, held still (no spin) | stays on screen until acknowledged; more criticals queue behind it |

The **scene engine** keeps a stack: a higher priority interrupts, an equal one queues, and when an alert ends, the globe **resumes the interrupted scene**. That's the same idea as the IP-change display we built, made general.

---

## 7. Buttons (physical input)

The base has buttons (LEFT, DOWN, SELECT, UP, possibly more). Plan:

1. **Hardware check (Phase 4a):** trace how the base buttons are wired (a switch matrix or one line each) and whether we can read them with the ESP32. GPIO32, GPIO33 and 34–39 are free on Header 1; 34–39 need external pull-ups. This needs a multimeter and maybe a small adapter board.
2. **Actions** are configurable per button and per context:
   - while an alert is showing: **SELECT** = acknowledge, **DOWN** = snooze 15 min,
   - otherwise: **UP/DOWN** = next/previous playlist page, **SELECT** = "show details" (e.g. the full message of the last alert), **long-press SELECT** = do not disturb 1 h,
   - **custom:** play any saved scene, or call a webhook (e.g. toggle a light through Home Assistant).
3. The web UI gets a **Buttons** page with a live "press a button to test" indicator.

---

## 8. Charts and "graphics" (within a text-only display)

Honest expectations: the font is fixed and there are no pixels. Possible techniques, from simple to clever:

- **Big number + label**, e.g. `CPU 42%`, alternating with a trend arrow (`↑` or `^` depending on the charset).
- **Bar meters** built from characters: `[#####·····]`, or glyphs of different widths if the charset has them (Phase 0 will show what exists above `0x7F`; many such fonts include block or bar characters).
- **Sparkline as a sequence:** step a bar over 10–20 frames at ~5 fps to "draw" a trend, using the frame pipeline we already have.
- **Spinning ticker** of several stats around the whole globe (the globe already spins; use it).
- **Custom glyphs, if any command allows them.** That's an open question for the unknown command bytes (`01`–`03`, `07`–`1F`). If one turns out to load characters, real mini-graphs become possible. It's a research item, not a promise.

---

## 9. Phases

Each phase ends with something usable.

### Phase 0: Measure the remaining unknowns (≈1–2 sessions, mostly with the camera)
- Film the **Character set** test (0x20–0xFF) → a character map and the **font bitmap** for the simulator.
- Film the **Text length** tests → visible width, wrap and scroll behaviour.
- Decode the **0x05 blink and shift** bytes and `04` scrolling; find the **minimum frame gap**.
- Sweep the **unknown command bytes** for anything like custom glyphs, brightness or speed.
- *Done when:* `docs/protocol.md` has the charset table, the visible width, the blink/shift meaning and a font file.

### Phase 1: Frame compiler and simulator (the foundation)
- Firmware: a `globe_frame` API (text + entrance + style + duration) → bytes; an effect table generated from `effects.json`; REST `POST /api/v1/frame`.
- Web: rebuild the UI with Preact and Vite; the **simulator** component (front window + unrolled band) animating all 38 effects from the catalogue; calibrate timings from the existing videos.
- Designer MVP: one frame, text box with width meter, transition gallery, live preview, **Send to globe**.
- *Done when:* you can design a frame without knowing any command, and the preview matches the globe.

### Phase 2: Scenes, library and playlist
- Multi-frame scenes, timeline, templates, undo/redo, save, export and import.
- On-device **scene engine** with a playlist and variable substitution (`{time}`, `{date}`…); the clock becomes a normal scene.
- Dashboard v1: now showing (WebSocket mirror), playlist editing, quick message.
- *Done when:* the globe runs your own rotation all day without the browser open.

### Phase 3: Sources, rules and alerts
- Sources: webhook (with a token), HTTP poll with a JSON path, ping/TCP; then MQTT.
- Rules builder, priorities with interrupt and resume, acknowledge and snooze, quiet hours, activity log.
- Ready-made recipes: Uptime Kuma, Home Assistant, GitHub Actions, a disk/CPU script for your PCs.
- *Done when:* a server going down shows on the globe within a minute, and you can acknowledge it from the dashboard.

### Phase 4: Buttons
- 4a hardware tracing and wiring; 4b input driver (debounce, long-press); 4c configurable actions and the test page.
- *Done when:* SELECT acknowledges an alert and UP/DOWN pages through the playlist.

### Phase 5: Visuals and polish
- Bars, sparklines and multi-stat tickers (based on the Phase 0 charset; custom glyphs if discovered).
- Security (token for write APIs, optional login), settings backup and restore, OTA from the UI, firmware health page.

---

## 10. Technical notes and risks

| Topic | Plan / risk |
|---|---|
| Flash space | The app is 0.95 MB of 1.5 MB; web bundle target < 250 KB gzipped in the 512 KB SPIFFS; user data in the 256 KB partition (scenes are small). Fine. |
| RAM | An HTTPS poll needs ~40–50 KB for TLS; the WROVER-E has 8 MB of PSRAM we haven't enabled yet. Enable it in Phase 3. |
| Simulator fidelity | Depends on capturing the font and timings well. Mitigated by the Live mode and the "≈" badge until calibrated. |
| Web UI rewrite | A build step (Node/Vite) enters the repo. Keep the current page as the fallback until the new one reaches parity. |
| Security | It's LAN-only, but webhooks can change the display, so tokens are needed for write APIs; no internet exposure. |
| Unknown protocol corners | Phase 0 is cheap; if custom glyphs don't exist, charts stay character-based. |
| Buttons | Depends on the base's wiring; worst case, add our own buttons to Header 1. |

---

## 11. Questions to talk through

1. **What should it watch first?** e.g. NAS/server up/down, your PCs' CPU/disk, internet outage, CI builds, Home Assistant events (doorbell, laundry), calendar reminders.
2. **Do you use Home Assistant, Uptime Kuma, or MQTT already?** That decides which source to build first.
3. **How loud should "critical" be?** Hold the screen until acknowledged, or time out after N minutes?
4. **Is a build step for the web UI okay** (Node + Vite in the repo)?
5. **Where does the globe live**, and who else sees it? (That affects quiet hours and what's OK to display.)
6. **The buttons:** are they the original base's buttons, and is opening the base OK?
