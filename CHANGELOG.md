# Release notes

What changed in each version of the InfoGlobe, newest first. The globe's web app shows these under
Settings, About. The version itself is in `VERSION`; see "Making a release" in `README.md`.

## 0.9.0-beta (2026-11-15)

The first numbered version: everything since the globe got its own web app, gathered into one beta.

### What the globe shows
- One list on Home is the whole story: what the globe shows, in what order and for how long. Add a message
  by typing it, edit its words, entrance and time right in the list, reorder with arrows, delete.
- Countdowns to a day ("XMAS IN 41 DAYS", then "TOMORROW", then "TODAY"), added from the same list. They are
  rows of their own: the clock no longer has a countdown inside it.
- Date, world clock, sunrise and sunset, moon phase and day progress are rows of their own too, waiting
  under "Switched off" on Home: the clock is just the clock.
- Ready-made starting points (Decode, Typewriter, Ticker, Alert, Sprites…) from the list's add row.
- A kitchen timer: counts down on the globe ("TEA 4:59"), flashes TIME'S UP, and goes back by itself.
- Quick message: something shown right now, once, over the rotation.
- Live values in any message: the time and date, weather, the nearest plane, countdowns, pushed values.

### Looks
- New animations: split-flap (a departure board), sparkle and wave, alongside decode, typewriter, count,
  bounce, scanner, sprites, automaton and your own frames.
- Clock studio styles, now including Departure board, Odometer and Dissolve; extras such as the date,
  the moon, sunrise and sunset, world clocks and a countdown; chimes.
- All 38 of the globe's own transitions, previewed with your text before it goes to the globe.

### The globe
- A display switch in the app bar blanks the globe and holds everything; a night schedule switches it
  off and on by itself.
- Weather from Open-Meteo, updated every 15 minutes.
- Planes: interrupts about a minute before a plane passes where it can be seen or heard.
- Set times: a message at a time of day (GOOD MORNING at 7:00).
- Saving a message no longer restarts what is on the globe.

### The app
- Three pages: Home, Clock and Settings. Rarely needed things (animation pace, scripts, firmware,
  the access token) are folded away under Settings, More.
- An open page reloads itself when the globe has a newer version of it.
- Works on a phone.

## Before 0.9.0-beta

The history from before versions were numbered, in short (the details are in git):

- **November 2026**: reverse-engineered the globe's infrared protocol, its character set and all 38
  transitions from camera footage (`docs/`); a frame compiler and REST API; the scene engine that runs
  the rotation on the globe; the web designer with a simulator using the globe's real font; live values,
  weather and plane feeds; the access token and the live WebSocket link; Ethernet alongside Wi-Fi;
  ESP-IDF 6.1.
- **2023 and early 2026**: the original firmware: a clock over the IR link, TCP and HTTP control, OTA.
