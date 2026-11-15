# Live values

Write these in any frame's text; the globe fills them in and re-sends the frame when the value changes.
Implemented once in C for the globe (`hello_world/main/src/live_values.c` for the syntax, lookup order and pushed values,
`clock_values.c` for everything time-based) and mirrored in `web/src/lib/values.js` for the designer's preview.
`npm test` in `web/` compiles that C on the PC and checks both against the same test vectors
(`web/src/lib/values.vectors.json`), so a change to one side that the other doesn't get fails the tests.

## Clock and calendar

| Value | Example | Notes |
|---|---|---|
| `{clock}` | `07:05 PM` | colon blinks; `{clock:24}` 24 h, `{clock:s}` with seconds, `{clock:x}` no AM/PM, `{clock:n}` no blink (combinable: `{clock:24s}`) |
| `{time}` `{time24}` `{ampm}` `{sec}` | `7:05` `19:05` `PM` `42` | |
| `{date}` `{day}` `{dd}` `{month}` `{mon}` `{year}` | `Mon 02 Nov` `Monday` `02` `November` `Nov` `2026` | |
| `{doy}` `{week}` | `306` `45` | day of year, ISO week (`01`-`53`; 1 Jan 2027 is week `53` of 2026) |
| `{fuzzy}` | `QUARTER PAST TEN` | nearest five minutes (from :57:30 on it is the next hour); `NOON`, `MIDNIGHT` |
| `{words}` | `TEN TWENTY EIGHT` | `TEN O'CLOCK`, `TEN OH FIVE` |
| `{roman}` | `X:XXVIII` | minute 0 is `N` (nulla) |
| `{binary}` | `10110:100110` | 5-bit hour (24 h), 6-bit minute |
| `{beat}` | `432` | Swatch internet time (.beats, UTC+1 everywhere) |
| `{decimal}` | `9:47` | French decimal time, 10 h x 100 min |
| `{unix}` | `1793680742` | |
| `{bar:day}` (`min`, `hour`, `week`, `month`, `year`) | `######----` | 10-character progress bar; `{bar}` is the day |
| `{pct:day}` (same periods) | `43` | the globe has no `%` glyph |
| `{moon}` `{moonpct}` | `WAXING GIBBOUS` `73` | mean lunar month; `{moonpct}` is the lit fraction |
| `{sunrise}` `{sunset}` | `6:42` | needs the location (`POST /api/v1/settings {"lat":..,"lon":..}`); `--:--` without one, and in polar day or night |
| `{tz:nyc}` | `11:39` | nyc chi den la hnl lon par ber ath dxb del sgp hkg tyo syd akl utc |
| `{until:12-25}` / `{until:17:00}` | `53` / `1:23` | days to a date (`M-D` or `MM-DD`), or time to a time of day (`H:MM` or `HH:MM`) |
| `{countdown:12-25}` / `{countdown:17:00}` | `IN 41 DAYS` / `IN 1:23` | the same, in words: `TOMORROW` and `TODAY` for a date, `NOW` for a time. Put a label in front: `XMAS {countdown:12-25}` |
| `{ip}` | `192.168.0.71` | |

Days, hours and bars follow the local wall clock: on the day the clocks go forward, `{until:03:00}` at 1:30 says `1:30`
although only 30 minutes are left, and `{bar:day}` jumps by an hour.
The world clock (`{tz:...}`) uses each city's fixed daylight-saving rule (US, EU, Australia, New Zealand, or none),
checked against the IANA time zones for 2024-2030; if a country changes its rules, update `CITIES` in `clock_values.c`.

## Pushed values

Anything on the network can put values on the globe with `POST /api/v1/values` (recipes: [pushed-values.md](pushed-values.md));
the weather and aircraft feeds publish theirs the same way ([weather.md](weather.md), [aircraft.md](aircraft.md)).
Names are 1-15 of `a-z 0-9 _`; values are text up to 47 characters, kept in RAM (not saved) until they expire (`?ttl=`) or are removed.

| Value | Example | Notes |
|---|---|---|
| `{var:cpu}` | `42` | the value, or `--` when nothing has set it or it expired |
| `{cpu}` | `42` | the same by its bare name, for any name that isn't built in; **left as typed** when not set |
| `{bar:cpu}` | `####------` | 10-character bar of a 0-100 number; empty bar when not set or not a number |
| `{bar:print:250}` | `#####-----` | the same, out of 250 (any positive number) |
| `{pct:cpu}` `{pct:print:250}` | `42` `50` | whole percent, clamped to 0-100; `--` when not set or not a number |

- Built-in names win: a pushed value called `clock` or `day` doesn't change `{clock}` or `{bar:day}`.
  `{bar:min}` ... `{bar:year}` are always the clock's periods.
- Numbers are read like JavaScript's `parseFloat`: `42`, `-5`, `33.3`, `1e2`, ` 7`, `12abc` (12) count; `abc`, `0x10`, `inf` don't.
- Prefer `{var:x}` in scenes that should degrade cleanly: a bare `{x}` that isn't set stays in the text, and the globe has no
  `{` `}` glyphs, so it shows as ` x `.

## Syntax

`{name}` or `{name:argument}`: the name is 1-15 of `a-z 0-9 _`, the argument 1-24 characters other than `{` `}`.
Anything else (`{Clock}`, `{clock:}`, an unknown name) stays in the text as typed.

## Frame options for clocks (scene JSON)

`"blinkColon": {"period": 1000, "look": "hide"|"dot"}` blinks the colon of `{clock}` `{time}` `{time24}` (period = full
cycle in ms; 0 or anything under 200 = steady); without it only `{clock}` blinks, once a second.
`"onChange": <effect> | "random"` plays a transition when a value changes (flip clock); `"motion": "pendulum" | "drift"`
with `"motionMs"` moves still text by re-sending it at a new position. Playlist `"chime": {"scene": id, "every": 60|30|15}`
plays a scene on the hour.
