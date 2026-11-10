# HTTP API

Everything the globe serves on port 80. Implemented in `hello_world/main/src/api_v1.c` (REST API v1),
`live.c` (WebSocket), `auth.c` (access token) and `http_server_task.c` (pages and the older routes).
The web app's client is `web/src/lib/api.js`.

In the examples, `GLOBE=http://192.168.0.71` (the address the globe shows when it joins the network).

## Basics

- Bodies are JSON (`Content-Type: application/json`), up to 2 KB unless a route says otherwise.
- Success: the requested JSON, or `{"ok":true}` for actions.
- Errors: an HTTP status and `{"ok":false,"error":"Text you can show to a person"}`.
- Every `/api/v1` response allows cross-origin requests (`Access-Control-Allow-Origin: *`, headers
  `Content-Type, Authorization, X-Globe-Token`), so the web app can run on a PC (`npm run dev`) against the real globe.
- LAN only. There is no TLS; don't expose the globe to the internet.

## Access token

Optional. **No token set** (the default): everything works as it always did. **Token set**: every request that
changes something must carry it; reading stays open.

| Needs the token (when one is set) | Open |
|---|---|
| every `POST` and `DELETE` under `/api/v1/` (frame, clear, scenes, playlist, play, resume, next, settings, values, auth) | every `GET` under `/api/v1/` |
| `POST /text` `/raw` `/button` `/mode` `/ir` `/notes` | `GET /` `/designer` `/mode` `/ir` `/notes` |
| `POST /ota` (firmware upload; checked before anything is erased) | `GET /api/v1/ws` (WebSocket, read-only) |

Send it in one of these (the first one present is the one checked):

```sh
curl -X POST $GLOBE/api/v1/next -H "Authorization: Bearer $TOKEN"
curl -X POST $GLOBE/api/v1/next -H "X-Globe-Token: $TOKEN"
curl -X POST "$GLOBE/api/v1/next?token=$TOKEN"      # for webhook senders that can't set headers
```

Without it, or with a wrong one:

```
HTTP/1.1 401 Unauthorized
WWW-Authenticate: Bearer realm="InfoGlobe"
{"ok":false,"error":"Wrong access token"}
```

- Tokens are 8 to 64 characters from `A-Z a-z 0-9 . _ ~ -` (all safe in a URL as they are, so `?token=` needs no
  encoding). The web app's *Generate* makes 20 random letters and digits.
- Compared in constant time. Failed attempts are logged (without the query string) but not rate-limited.
- Stored in NVS (namespace `auth`, key `token`), not in `settings.json`, so it is never in `GET /api/v1/settings`
  or an export. No endpoint returns it.
- A `?token=` URL can end up in the sender's logs and history: prefer a header where the sender allows one.

### `GET /api/v1/auth`

```sh
curl $GLOBE/api/v1/auth -H "Authorization: Bearer $TOKEN"
# {"required":true,"valid":true,"minLength":8,"maxLength":64}
```

`required`: a token is set. `valid`: the token sent with *this* request is right (always `true` when none is
required); a safe way to check a token. Older firmware answers 404 (no tokens).

### `POST /api/v1/auth`

```sh
curl -X POST $GLOBE/api/v1/auth -d '{"token":"my-new-token-1"}'                                  # set (none set yet)
curl -X POST $GLOBE/api/v1/auth -H "Authorization: Bearer my-new-token-1" -d '{"token":"other-token-2"}'  # change
curl -X POST $GLOBE/api/v1/auth -H "Authorization: Bearer other-token-2" -d '{"token":""}'        # remove
# {"ok":true,"required":true}
```

Needs the current token when one is set. 400 for a token that breaks the rules.

### Lost token

- **Hold the globe's button for 10 seconds.** The globe clears the token and shows `TOKEN CLEARED` (the display
  is then in manual mode; a short press brings the clock back). The press itself toggles the clock as a short
  press always has. The button is read once the globe has the time from the network, which it needs anyway.
- Or over USB, without touching scenes and notes: `parttool.py --port COMx erase_partition --partition-name=nvs`
  (erases the whole NVS partition: the token and saved Wi-Fi/PHY calibration data; the Wi-Fi settings come from
  the firmware config, so the globe reconnects and recalibrates).

## Display

| Route | What |
|---|---|
| `POST /api/v1/frame` | Show a designed frame now. Pauses the playlist (manual mode) unless `keepClock` |
| `POST /api/v1/clear` | Blank the display (manual mode) |
| `GET /api/v1/effects` | The 38 transitions: `[{id, name, group, durationMs, sameAs?}]` |

`POST /api/v1/frame` body: `{"text":"SERVER OK", "entrance":"Pincer"|2|null, "after":"spin"|"hold"|"flash"|"instant",
"blink":2, "shift":-1, "keepClock":false}`. Text up to 35 characters after encoding (accents become the globe's
own characters). Answer: `{"ok":true,"bytes":"00 00 ...","then":"05 ...","thenAfterMs":1500,"unsupported":0}`
(the protocol bytes sent, and the follow-up frame sent once the entrance has played).

```sh
curl -X POST $GLOBE/api/v1/frame -d '{"text":"DINNER IS READY","entrance":37,"after":"flash"}'
curl -X POST $GLOBE/api/v1/clear
```

## Scenes

A scene is a list of frames: `{"name":"Server check","frames":[{"text":"CPU {cpu}","entrance":0,"after":"hold",
"blink":0,"shift":-1,"seconds":6}, ...]}` (up to 24 frames, text up to 95 characters before values are filled in;
clock options such as `onChange`, `motion`, `blinkColon` are in [live-values.md](live-values.md)).
Ids use `a-z 0-9 -`, up to 23 characters. Stored on the `notes` partition, so they survive reflashing.

| Route | What |
|---|---|
| `GET /api/v1/scenes` | `[{id, name, frames, builtin?}]` (frames = count) |
| `GET /api/v1/scenes/<id>` | The scene JSON |
| `POST /api/v1/scenes/<id>` | Save (body up to 16 KB) |
| `DELETE /api/v1/scenes/<id>` | Delete |

```sh
curl -X POST $GLOBE/api/v1/scenes/server -d '{"name":"Server","frames":[{"text":"CPU {cpu}","after":"hold"}]}'
curl $GLOBE/api/v1/scenes
```

## Playlist and playing

| Route | What |
|---|---|
| `GET /api/v1/playlist` | `{"items":[{"scene":"clock","seconds":0}], "chime":{"scene":id,"every":60}?}` |
| `POST /api/v1/playlist` | Save (up to 24 items, body up to 4 KB) and switch to playlist mode |
| `POST /api/v1/play` | `{"scene":"id","seconds":30}`, or a quick message `{"frames":[...],"seconds":30}` (saved as scene `quick`). `seconds` 0 = one pass. Then back to the playlist |
| `POST /api/v1/resume` | Back to the playlist now (ends manual text and play-now) |
| `POST /api/v1/next` | Next playlist item |

```sh
curl -X POST $GLOBE/api/v1/play -d '{"scene":"server","seconds":60}'
curl -X POST $GLOBE/api/v1/resume
```

## Now showing

`GET /api/v1/now`:

```json
{"mode":"playlist","scene":"clock","name":"Clock","frame":0,"frames":1,"text":"12:00",
 "item":1,"itemRemaining":12,"itemSeconds":30}
```

`mode` is `playlist`, `scene` (play-now) or `manual` (text sent from the designer or classic page; then only
`text`). `item`, `itemRemaining`, `itemSeconds` only with a multi-item playlist. The same object is pushed over the
WebSocket below.

## Settings

| Route | What |
|---|---|
| `GET /api/v1/settings` | Everything in `settings.json`, e.g. `{"lat":41.88,"lon":-87.63}` plus the feeds' options |
| `POST /api/v1/settings` | Merge these keys (body up to 2 KB); the clock and feeds pick them up at once |

```sh
curl -X POST $GLOBE/api/v1/settings -d '{"lat":41.88,"lon":-87.63}'
```

Settings are readable by anyone on the network and travel with exports: don't put secrets in them.

## Pushed values

| Route | What |
|---|---|
| `GET /api/v1/values` | `{"cpu":"42","build":"PASS"}` (live keys only; all values are text) |
| `POST /api/v1/values[?ttl=600]` | Set values (body up to 4 KB). Text and numbers set, `true`/`false` become `YES`/`NO`, `null` removes. With `ttl` they vanish that many seconds after the last update |

Names: `a-z 0-9 _`, up to 15 characters; values up to 47 characters; up to 64 values. Not saved to flash.
Show them in a frame with `{cpu}` or `{bar:cpu}` (see [live-values.md](live-values.md)).

```sh
curl -X POST "$GLOBE/api/v1/values?ttl=600" -d '{"cpu":42,"build":"PASS"}'
curl -X POST "$GLOBE/api/v1/values?token=$TOKEN" -d '{"door":"OPEN"}'      # webhook style, with a token set
```

## Live mirror: `GET /api/v1/ws` (WebSocket)

```sh
websocat ws://192.168.0.71/api/v1/ws
```

Read-only, no token needed. Text frames, JSON:

```json
{"type":"now","now":{...same as GET /api/v1/now...}}
{"type":"values","values":{...same as GET /api/v1/values...}}
```

- Both arrive right after connecting, then again whenever they change. The clock's blinking colon changes `now`
  twice a second and a playlist countdown once a second, so expect a few messages a second while the clock shows.
- `now` is repeated after 15 s without any message, so a client can treat 40 s of silence as a dead link.
- The globe sends a WebSocket ping every 15 s (browsers answer by themselves) and drops a client it hasn't heard
  from for 45 s, or one that can't take a message within 300 ms.
- At most **2** clients at a time. One more is closed with code **1013** (try again later): poll `GET /api/v1/now`
  instead. The web app does this by itself, retries the WebSocket later, and gives its slot back after a minute in
  a background tab.
- Frames from the client are ignored (up to 128 bytes; bigger ones close the connection). A plain `GET` without the
  upgrade gets `426`.

## Pages

| Route | What |
|---|---|
| `GET /` | The classic Control & Explorer page (`hello_world/main/data/index.html`) |
| `GET /designer` | The web app (`web/`, built into `designer.html.gz`) |

## Older routes (used by the classic page)

Plain-text `OK` answers. Every `POST` needs the token when one is set; the `401` has the JSON shape above.

| Route | What |
|---|---|
| `POST /text` | `{"text":"HELLO","blink":false}`: show text (0x05 format) |
| `POST /raw` | `{"hex":"0500..."}`: send raw protocol bytes |
| `POST /button` | `{"button":1}` sends quick button 1's text; `{"button":1,"config":true,"command":"HI"}` sets it |
| `GET`/`POST /mode` | `{"time_mode":true}`: playlist (clock) on/off |
| `GET`/`POST /ir` | IR link stats `{gap_ms,fps,busy_pct,frames,test}`; POST `{"gap_ms":10}` or `{"test":true}` |
| `GET`/`POST /notes` | Protocol explorer notes; POST `{"key":"fx:3","note":"..."}` (empty note deletes; up to 16 KB) |
| `POST /ota` | Firmware upload: the raw `hello_world.bin` as the body. Reboots when done |

```sh
curl -X POST $GLOBE/ota -H "Authorization: Bearer $TOKEN" --data-binary @build/hello_world.bin
```

## Connections

The ESP32 has 10 sockets in all. The web server keeps up to 5 connections (plus 3 of its own), leaving room for the
feeds' HTTPS fetches. When a new connection needs room, the least recently used one is closed; live WebSockets
count as used on every tick, so idle HTTP connections go first.
