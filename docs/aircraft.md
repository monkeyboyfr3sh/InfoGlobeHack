# Aircraft overhead

The globe can tell you which aircraft is nearest to it, `UAL123 SFO-ORD B738`, and interrupt the playlist when one
flies right over. Implemented in `hello_world/main/src/feed_aircraft.c`; the web side is the Aircraft card on the
Data page (`web/src/components/AircraftCard.jsx`) and the designer values in `web/src/lib/feeds/aircraft.js`.

It is **off by default**: switch it on in the Aircraft card (or `POST /api/v1/settings {"aircraft": true}`). It needs
the globe's location (`lat`, `lon`), which the Clock page and the weather card save.

## Values

Write these in any frame's text, like the clock values. They update every poll (about 15 s); while nothing is in range
the line says so and the rest are `--`. All text is capitals with only glyphs the globe's font has (accents dropped:
`MONTREAL`, `SAN JOSE DEL CABO`).

| Value | Example | Notes |
|---|---|---|
| `{planeline}` | `UAL123 SFO-ORD B738` | flight, route, type; unknown parts left out (`N12345 C172`); `NO PLANES` when none; never over 35 characters |
| `{planewhere}` | `LOOK NE 2.4 NM 10800 FT` | `OVERHEAD 10800 FT` within 0.5 nm; `NONE WITHIN 10 NM` |
| `{planetrip}` | `TO CHICAGO` | `FROM DENVER` when it is landing near you (the far end is the interesting one); `HEADING SW` without a route; `QUIET SKIES` |
| `{plane}` | `UAL123` | callsign, else registration, else the ICAO address |
| `{planeroute}` `{planefrom}` `{planeto}` | `SFO-ORD` `SFO` `ORD` | IATA codes (ICAO when there is none); `--` when the route is unknown or doesn't fit the aircraft |
| `{planetype}` | `B738` | ICAO type designator |
| `{planealt}` | `10800` | feet, barometric, to the nearest 100 (can be slightly negative when landing) |
| `{planespd}` | `314` | ground speed, knots |
| `{planedist}` | `2.4` | nautical miles from the globe (ground distance) |
| `{planedir}` | `NE` | where to look from the globe (8 compass points) |
| `{planehdg}` | `SE` | where the aircraft is going |
| `{planes}` | `7` | aircraft in the air within the range |

Numbers have no units, so bars work: `{bar:planealt:40000}`, `{bar:planedist:10}`.
`plane_status` (no expiry, for the web card) is `OK`, `NO LOCATION` or `OFFLINE`; it and all values are removed when
the feed is switched off. Values expire four polls after the last good answer, so stale data never stays up.

Two aircraft at similar distances don't make the display flip: the one shown stays until another is 0.5 nm closer.

## Settings

Saved in `/notes/settings.json` through `POST /api/v1/settings` (the card does this). The feed reacts within a second.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `aircraft` | `false` | | poll at all |
| `aircraftRadius` | `10` | 2-25 nm | how far to look |
| `aircraftNotice` | `2` | 0-3 | the overhead alert: `0` never, `1` right overhead, `2` when it will be seen or heard, `3` anything in the sky |
| `aircraftAlertFt` | `0` | 0-60000 ft | alert only for aircraft below this altitude; `0` = any height |
| `aircraftAlert` | | 0-25 nm | the old alert distance; only its `0` (never) still counts, and only without `aircraftNotice` |
| `aircraftPoll` | `15` | 10-300 s | seconds between polls (not in the card) |

## Overhead alert

When an aircraft is about to be seen or heard from the globe's location (and is below `aircraftAlertFt`, if set), the
globe plays the scene with id **`plane`** once (`scene_engine_play("plane", 0)`), then returns to the playlist.

"About to be seen or heard": each aircraft's track and speed are carried forward 75 seconds, and the closest it comes
in that time is judged by `aircraftNotice`:

| `aircraftNotice` | Seen: at least this high in the sky, within this far in a straight line | Heard: within, however low |
|---|---|---|
| `1` right overhead | 55 degrees, 7 nm | 1 nm |
| `2` seen or heard (default) | 35 degrees, 7 nm | 2 nm |
| `3` anything in the sky | 20 degrees, 10 nm | 3.5 nm |

So a jet at cruise heading over the house rings about a minute before it gets there, the same jet crossing 8 nm away
does not, and neither does a plane sitting at an airfield down the road. Altitudes are taken as height above the
ground, which is right near sea level and generous in the mountains.

Every aircraft in range is judged, not only the nearest: one that lingers close by doesn't hide the next to arrive.
While the alert plays the values describe the aircraft it is about. The Planes page shows the last alert (`plane_alert`, `plane_alert_at`) and
the closest approach since the globe started (`plane_closest`, `plane_close_at`). It does not fire:

- when no scene `plane` exists (the card creates a default one: the line flying in, the trip, where to look);
- over text someone put up (the playlist doesn't own the display), a chime or another "play now" scene; it tries again
  on the next poll while the aircraft is still close;
- for the same aircraft (ICAO address) again until it has been out of sight and earshot for 10 minutes (a trainer
  flying circuits rings once, not on every lap);
- while an earlier plane alert may still be playing (the scene's length plus 5 s).

## Data sources

| | What | Limits and terms |
|---|---|---|
| [adsb.lol](https://api.adsb.lol/docs) (first choice) | `GET https://api.adsb.lol/v2/point/{lat}/{lon}/{nm}` | free, no key; data under ODbL. Says an API key (free for feeders) may be required in future. Rate limits not published. |
| [adsb.fi](https://github.com/adsbfi/opendata) (fallback) | `GET https://opendata.adsb.fi/api/v3/lat/{lat}/lon/{lon}/dist/{nm}` | 1 request/s; personal, non-commercial use only; credit adsb.fi with a link (the card does). v2 of this endpoint is deprecated. |
| [adsbdb](https://github.com/mrjackwills/adsbdb) (routes) | `GET https://api.adsbdb.com/v0/callsign/{callsign}` | 512 requests/min per address; route data by David Taylor and Jim Mason, not to be copied into other databases. `404 {"response":"unknown callsign"}` when unknown. |

Both aircraft services answer in readsb's JSON format (`{"ac":[{hex, flight, r, t, alt_baro, gs, track, lat, lon,
category, seen_pos, ...}]}`); the parser also accepts readsb's `"aircraft"` key. airplanes.live answered `403` with a
request to e-mail them first, so it isn't used.

**Polling.** One request every 15 s (default) to one provider. If adsb.lol fails, adsb.fi is asked straight away and
used for the next 10 minutes. When both fail, the wait doubles (30 s, 1, 2, 4, 8 min, at most 10 min) and after two
failures the values say `NO PLANE DATA`. A route is looked up only when the nearest flight changes, for airline-style
callsigns (`UAL123`, not `N12345`), and cached for 6 hours (5 minutes after a failed lookup).

**Routes are checked.** Route databases list a callsign's usual route, which is often another leg or wrong. For the
seven airline flights in the air around SFO in the samples, adsbdb's route fitted one (`PVR-SFO` for a flight arriving
at SFO); the others included `BOS-IAH` for a flight climbing out of SFO and `SFO-LAS` for one landing there. A
route is shown only if the aircraft is roughly on the way (via its position at most 25 % + 60 nm longer than direct),
heading for the destination when far from both ends, not descending into the origin nor climbing out of the
destination. Otherwise it shows `--`, never a guess.

## Memory

A busy 25 nm circle measured 26-50 KB (about 450 bytes per aircraft, most of them on the ground at airports). The body
is capped at 128 KB (PSRAM); the aircraft are parsed **one at a time**, so cJSON never holds more than one aircraft:
a 118 KB answer with 275 aircraft peaks at about 3 KB of cJSON, where a full tree would take about 650 KB. An answer
over the cap shrinks the circle by a third for the next polls (down to 2 nm) and widens it again, 1 nm at a time,
while answers are small. The task has an 8 KB stack and priority 3 and logs its spare stack after the first poll.

## Privacy

With the feed on, the globe sends its location to adsb.lol (or adsb.fi) every poll, **rounded to 0.01 degree (about
1 km)**; it asks for a circle 1 nm larger and measures distances from the exact location itself. adsbdb only receives
callsigns. Nothing is sent while the feed is off.

## Testing off the device

The parsing, maths, route check and values build on a PC against trimmed real answers in
`hello_world/host_test/data/` (taken around SFO). From the repository root, after one `idf.py build` (for cJSON):

```
gcc -std=gnu11 -Wall -Wextra -O1 -DAIRCRAFT_HOST_TEST -I hello_world/main/inc \
    -I hello_world/managed_components/espressif__cjson/cJSON hello_world/host_test/feed_aircraft_test.c \
    hello_world/managed_components/espressif__cjson/cJSON/cJSON.c -lm -o aircraft_test && ./aircraft_test
```

`npm test` in `web/` checks the designer values and that the default scene's frames fit 35 characters.
