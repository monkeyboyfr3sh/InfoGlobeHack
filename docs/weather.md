# Weather

The globe fetches the weather for its saved location by itself and keeps it as live values, so a frame
whose text is `{weather}` (or `{temp} {sky}`) shows `61 CLOUDY` and updates on its own.

- Firmware: `hello_world/main/src/feed_weather.c` (one low-priority task, started by `feeds_start()` once the clock has synced).
- Web: the **Weather** card on the Data page (`web/src/components/WeatherCard.jsx`); value names and samples for the
  designer in `web/src/lib/feeds/weather.js`.
- Source: [Open-Meteo](https://open-meteo.com/) forecast API, free and keyless (weather data by Open-Meteo.com, CC BY 4.0).

## Turning it on

Data page → Weather: find the town (the search uses Open-Meteo's geocoding from the browser) or enter coordinates.
The feed is **on as soon as a location is saved**; the card also switches it off and picks the units.
The location is the same `lat`/`lon` the clock uses for `{sunrise}`/`{sunset}`.
**Create weather scene** saves a ready-made scene `weather` and offers to add it to the playlist.

## Values

Whole numbers, uppercase words, only glyphs the globe's font has (no `%` or degree sign: add `PCT` or nothing).

| Value | Example | Notes |
|---|---|---|
| `{weather}` | `61 CLOUDY` | temperature and sky; just `61` when the sky code is unknown |
| `{temp}` | `61` | now, °F or °C |
| `{feels}` | `58` | apparent temperature |
| `{sky}` | `CLOUDY` | see the table below; `CLEAR` at night where the day says `SUNNY` |
| `{hi}` `{lo}` | `64` `48` | today (the location's own day) |
| `{rain}` | `20` | chance of precipitation today, 0-100 (`{bar:rain}` draws it) |
| `{humid}` | `72` | relative humidity, 0-100 |
| `{wind}` | `8` | mph, or km/h with `"units":"c"` |
| `{winddir}` | `SW` | where the wind comes from: N NE E SE S SW W NW |
| `{tsky}` `{thi}` `{tlo}` | `RAIN` `66` `50` | tomorrow |

The weather values expire 45 minutes (three polls) after the last good answer, so stale weather is never shown as
current. **Write `{var:temp}` rather than `{temp}` in scenes that should degrade cleanly:** with no value (feed off,
offline, expired) `{var:temp}` shows `--`, while a bare `{temp}` is left in the text as typed (the engine can't tell
a missing value from a typo), which the globe draws as ` temp ` because it has no `{` `}` glyphs. The ready-made
scene and the Clock extra use `{var:...}`. Two more values explain the state; they never expire:

| Value | Meaning |
|---|---|
| `wx_status` | `OK`; `OFF` (turned off); `NO LOCATION`; `OFFLINE` (no answer: network or DNS); `BAD LOCATION` (Open-Meteo answered 400); `ERROR` (another HTTP error or an answer it couldn't read) |
| `wx_updated` | unix time of the last good answer |

When the feed is off or has no location it fetches nothing and deletes all of its values (except `wx_status`).

### Sky words (WMO weather code → `{sky}`, `{tsky}`)

| Code | Word | | Code | Word |
|---|---|---|---|---|
| 0, 1 | `SUNNY` (`CLEAR` at night) | | 61, 63 | `RAIN` |
| 2 | `PT CLOUDY` | | 65, 82 | `HEAVY RAIN` |
| 3 | `CLOUDY` | | 80, 81 | `SHOWERS` |
| 45, 48 | `FOG` | | 71, 73, 77, 86 | `SNOW` |
| 51, 53, 55 | `DRIZZLE` | | 75 | `HEAVY SNOW` |
| 56, 57, 66, 67 | `ICY RAIN` | | 85 | `FLURRIES` |
| 95 | `STORM` | | 96, 99 | `HAIL` |

The longest is 10 characters, so `{weather}` is at most 14 (`-40 HEAVY SNOW`).

## Settings

Saved with `POST /api/v1/settings` (merged into `/notes/settings.json`):

| Key | Values | Default |
|---|---|---|
| `lat`, `lon` | numbers | none: no location, no weather |
| `weather` | `true` / `false` | on (only `false` turns it off) |
| `units` | `"f"` (°F, mph) / `"c"` (°C, km/h) | `"f"` |
| `place` | `{"name":"Chicago, Illinois, United States","lat":41.85,"lon":-87.65}` | web card only: the name it shows while `lat`/`lon` still match |

```sh
curl -X POST http://<globe>/api/v1/settings -d '{"lat":41.88,"lon":-87.63,"units":"f"}'
```

## Timing and traffic

- One HTTPS GET every **15 minutes** (Open-Meteo's current conditions move in 15-minute steps), about 950 bytes,
  asking only for the fields above. That is 96 requests a day; the free tier allows 10,000.
- After a failure it retries after 1 minute, then 2, 4, 8 and 15.
- It checks the settings once a second. When the location, units or on/off change it fetches straight away
  (within a second or two); other saves (e.g. the aircraft feed's) don't trigger a fetch unless the last try failed.
- Needs: the globe's network to reach `https://api.open-meteo.com` (checked against the built-in certificate bundle),
  and the clock synced (feeds start after NTP).

The request (Fahrenheit; Celsius drops the last two parameters):

```
https://api.open-meteo.com/v1/forecast?latitude=41.8800&longitude=-87.6300
  &current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,is_day
  &daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max
  &forecast_days=2&timezone=auto&temperature_unit=fahrenheit&wind_speed_unit=mph
```

## The ready-made scene

| Text | Example | Look |
|---|---|---|
| `{var:weather}` | `61 CLOUDY` | rises in (16), held, 8 s; fades (3) when the weather changes |
| `HI {var:hi} LO {var:lo}` | `HI 64 LO 48` | letters drop in (26), held, 6 s |
| `FEELS {var:feels}  RAIN {var:rain} PCT` | `FEELS 58  RAIN 20 PCT` | wiper (12), spins, 8 s |
| `TOMORROW {var:tsky} {var:thi}/{var:tlo}` | `TOMORROW RAIN 66/50` | slides in (0), spins, 8 s |

30 seconds a round; **Add to playlist** gives it 30 s (and items without a time 30 s each). With worst-case values
every frame stays within 35 characters and the held ones fit the front window with typical values
(`web/src/lib/feeds/weather.test.js`). The Clock studio's **Also show → Weather** alternates the time with `{var:weather}`.

## Testing the parser on the PC

`tools/weather-test/weather_test.c` compiles the parsing half of `feed_weather.c` (everything above
`#ifndef WEATHER_HOST_TEST`) with gcc and checks it against two saved real answers (`chicago-f.json`: °F/mph, a clear
night; `bergen-c.json`: °C/km/h, drizzle) and edge cases (nulls, unknown codes, rounding below zero, error answers,
the request line fitting esp_http_client's 512-byte buffer). Build the firmware once first so cJSON is downloaded:

```sh
gcc -Wall -Wextra -I hello_world/managed_components/espressif__cjson/cJSON \
    tools/weather-test/weather_test.c hello_world/managed_components/espressif__cjson/cJSON/cJSON.c -lm -o weather_test
./weather_test
```
