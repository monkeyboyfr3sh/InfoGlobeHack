# Live values

Write these in any frame's text; the globe fills them in and re-sends the frame when the value changes.
Implemented in `hello_world/main/src/scene_engine.c` + `clock_values.c`, mirrored in `web/src/lib/values.js`.

| Value | Example | Notes |
|---|---|---|
| `{clock}` | `07:05 PM` | colon blinks; `{clock:24}` 24 h, `{clock:s}` with seconds, `{clock:n}` no blink (combinable: `{clock:24s}`) |
| `{time}` `{time24}` `{ampm}` `{sec}` | `7:05` `19:05` `PM` `42` | |
| `{date}` `{day}` `{dd}` `{month}` `{mon}` `{year}` | `Mon 02 Nov` `Monday` `02` `November` `Nov` `2026` | |
| `{doy}` `{week}` | `306` `45` | day of year, ISO week |
| `{fuzzy}` | `QUARTER PAST TEN` | nearest five minutes; `NOON`, `MIDNIGHT` |
| `{words}` | `TEN TWENTY EIGHT` | `TEN O'CLOCK`, `TEN OH FIVE` |
| `{roman}` | `X:XXVIII` | minute 0 is `N` (nulla) |
| `{binary}` | `10110:100110` | 5-bit hour (24 h), 6-bit minute |
| `{beat}` | `432` | Swatch internet time (.beats) |
| `{decimal}` | `9:47` | French decimal time, 10 h x 100 min |
| `{unix}` | `1793680742` | |
| `{bar:day}` (`min`, `hour`, `week`, `month`, `year`) | `######----` | 10-character progress bar |
| `{pct:day}` (same periods) | `43` | the globe has no `%` glyph |
| `{moon}` `{moonpct}` | `WAXING GIBBOUS` `73` | |
| `{sunrise}` `{sunset}` | `6:42` | needs the location (`POST /api/v1/settings {"lat":..,"lon":..}`) |
| `{tz:nyc}` | `11:39` | nyc chi den la hnl lon par ber ath dxb del sgp hkg tyo syd akl utc |
| `{until:12-25}` / `{until:17:00}` | `53` / `1:23` | days to a date, or time to a time of day |
| `{ip}` | `192.168.0.71` | |

Frame options for clocks (scene JSON): `"onChange": <effect> | "random"` plays a transition when a value
changes (flip clock); `"motion": "pendulum" | "drift"` with `"motionMs"` moves still text by re-sending it at
a new position. Playlist `"chime": {"scene": id, "every": 60|30|15}` plays a scene on the hour.
