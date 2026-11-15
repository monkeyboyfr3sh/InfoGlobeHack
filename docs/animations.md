# Custom animations

The globe has 38 transitions of its own (`effects.json`) and nothing else that moves. Custom animations add motion by
sending a stream of **still frames** (command `05`, text + blink byte + shift byte), one per step, from the scene
engine on the ESP32. The same idea already works on the real globe as the pendulum clock (`"motion": "pendulum"`),
which re-sends the same text at a new shift.

- Firmware: `hello_world/main/src/globe_anim.c` (pure C: what to show at step *k*) and the engine section of
  `scene_engine.c` (when to send it).
- Designer preview: `web/src/lib/anim.js`, a line-for-line mirror. `node hello_world/test_host/run.mjs` checks that
  both produce the same steps (text, shift, blink, step time, done) for ~2,500 steps over 40 animation set-ups.

**Status: designed and simulated, not yet seen on the real globe.** See [What to check on the globe](#what-to-check-on-the-globe).

## In a scene

A frame gets an optional `"anim"` object. Everything else in the frame keeps its meaning:

```json
{"text": "HELLO WORLD", "anim": {"type": "decode"}, "after": "hold", "seconds": 6}
```

- **Finite** animations (`decode`, `typewriter`, `count`, `flap`, `sequence` with `"loop": false`) are the frame's entrance:
  they end on the full text, then the frame settles into its `after` style (spin, hold, flash, instant) as if it had
  just appeared. The frame's `entrance` transition is ignored.
- **Looping** animations (`bounce`, `scanner`, `sprites`, `automaton`, `sparkle`, `wave`, `sequence`) run for as long as the frame is
  shown. The text never spins; `after: "hold"` keeps its blinking characters, `"flash"` blinks every step, `"spin"`
  and `"instant"` don't apply. `motion` and `onChange` don't apply either.
- **Live values** keep working: the text is filled in again for every step, so a decode that runs across a minute
  ends on the new minute, and `{clock}`'s colon still blinks (at step granularity).
- **`onChange`** can name a finite animation, played from the old text into the new one when a live value changes:
  `"onChange": "decode"` (flip-clock style, but decoding), `"onChange": "flap"` (each character flips on from the old
  one, a departure board), `"onChange": "count"` (numbers roll from the old value), or
  an object such as `"onChange": {"type": "count", "mode": "digits"}`. A name takes the frame's own `anim` options
  when the types match. Numbers and `"random"` still mean globe transitions.
- Frames without `anim` (and without an animation `onChange`) behave exactly as before.
- `/api/v1/now` reports the text of the step that is on the globe.
- "Send this frame" in the designer plays an animated frame as a short scene (animations need the scene engine).

## The animations

Every type takes `"ms"`, the time per step, and `"seed"` (any number; changes the random parts). Defaults below.
Steps run on a clock; a step is never shorter than its frame's airtime (see [Timing](#timing)), so with longer
text an animation runs slower than `ms` asks.

### decode (finite)

```json
{"type": "decode", "ms": 110, "lead": 4, "order": "lr", "noise": "match"}
```

Random characters that lock into the real text, Matrix style. For `lead` steps everything is noise, then one
character locks per step (a few per step for text over 16 characters, so it never takes more than 16 lock steps).
Spaces stay spaces. `order`: `lr` left to right, `rl` right to left, `random`. `noise`: `match` scrambles letters
with letters of the same case and digits with digits and leaves punctuation (`:` `.`) in place, which keeps a clock
readable as a clock; `symbols` uses `# $ * < > = ? /`; `mixed` uses all of A-Z, 0-9 and those symbols.

`HELLO WORLD`: `FZURM CBNWF`, `XVDZJ YABGG`, ... `HELLO GSJRT`, ... `HELLO WORLD`, 15 steps, about 2.1 s.
`12:34 PM`: 12 steps, about 1.4 s. The noise is the same every time for the same text (it is seeded from it).

### typewriter (finite)

```json
{"type": "typewriter", "ms": 140, "cursor": "#"}
```

One character per step, with a cursor in front: `#    `, `H#   `, `HE#  `, ... `HELLO`. There is no underscore
glyph; `cursor` can be `#` (a dense block, the default), `-`, `.`, any other single glyph, or `"none"`. The text is
padded with spaces to its full length so it never moves while it grows. `HELLO`: 6 steps, 0.8 s.

### count (finite)

```json
{"type": "count", "ms": 70, "steps": 12, "mode": "number"}
```

Every number in the text rolls to its value instead of jumping; other characters show as they are. As an entrance
numbers count up from 0; as `onChange` they roll from the old value (numbers are matched by position: the first
number with the first number). `mode`: `number` counts up or down with an ease-out (`CPU 10`, `18`, `24`, ... `41`,
`42`); `digits` turns each digit forward like an odometer wheel. Numbers keep their width (`05` keeps its zero,
other short values are padded with spaces); runs of more than 9 digits are left alone. Best with pushed values:
`{"text": "CPU {var:cpu}", "onChange": "count"}`.

### bounce (loops)

```json
{"type": "bounce", "ms": 100, "period": 3000, "pause": 100, "amp": 0}
```

Still text that glides side to side by its shift byte and slows into each end (smoothstep easing, then `pause` ms at
rest): a smoother sibling of the pendulum. `period` is one trip there and back. Shift 0 is as far right as text can
go, so the reach either side of the centre is at most the text's length in dot columns; short text is therefore
padded to 8 cells, the clock's length, whose 0..16 range the pendulum has shown on the globe. `amp` sets the reach
in dot columns (0 = auto: fill the ~72-column window, limited as above; a larger `amp` pads the text further, which
costs airtime). Starts in the centre. At the default about 9 frames a second, about 2 columns per step mid-swing.

### scanner (loops)

```json
{"type": "scanner", "ms": 110, "width": 10, "trail": "", "bg": " "}
```

The frame's text (keep it short: `*`, `<=>`, `O`) sweeps back and forth across a field of `width` cells, a whole
cell per step: an idle or "thinking" look. Text with `<` or `>` in it turns round at the ends (`=>` comes back as
`<=`; `(` and `)` swap too). `trail`: up to 3 glyphs behind it, nearest first; `{"text": "O", "trail": "o."}` gives a
KITT-style scanner (`.oO` going right, `Oo.` coming back). `bg`: the background glyph (space, `.` or `-`).
Empty text sweeps a `*`.

### sprites (loops)

```json
{"type": "sprites", "sprite": "pacman", "ms": 150, "width": 10}
```

Character-cell cartoons in a field of `width` cells:

- `pacman`: `C` and `O` (mouth open and shut) chomping along ` . . . . .`, leaving it empty, then it all comes
  back. With text in the frame Pac-Man eats your text instead.
- `chase`: `=>` closes in on a dawdling `>`, which dashes off; the chaser follows it out.
- `pulse`: `. o O o .` rings rippling out from the middle.
- `fish`: `><>` swims across, turns (`<><`), swims back.

### automaton (loops)

```json
{"type": "automaton", "rule": 30, "width": 35, "ms": 400, "start": "single", "gens": 120, "live": "#", "dead": " "}
```

A one-dimensional cellular automaton (Wolfram rule `rule`, 0-255) on a ring of `width` cells, a new generation per
step, drawn with `live` and `dead`. 35 cells nearly close the band, so it wraps the globe: a generative screensaver.
`start`: `single` (one live cell in the middle) or `random`. Every `gens` generations it starts again, and whenever it
dies out or fills completely it restarts from noise. Good rules: 30 (chaos), 90 (Sierpinski triangles), 110
(gliders, best with `random`), 150, 184 (traffic). A 35-character frame takes 330 ms to send, hence the 400 ms step.

### flap (finite)

```json
{"type": "flap", "ms": 70, "steps": 12, "order": "lr"}
```

A departure board: every letter, small letter and digit flips forward through its own alphabet (`A`-`Z`, `a`-`z`,
`0`-`9`), one flip per step, and stops on the text; other characters just appear. As an entrance the cells start one
after another (`order`: `lr`, `rl` or `random`, as in decode), each from blank, a few to `steps` flips short of its
letter (seeded from the text, so the same every time). As `onChange` every cell starts at once from what it showed:
`9:59` to `10:00` turns the `5` through `6 7 8 9 0` while the others land in one flip. The Clock page's
"Departure board" style is `{time} {ampm}` with `"onChange": "flap"`.

### sparkle (loops)

```json
{"type": "sparkle", "ms": 150, "density": 3}
```

The text holds still while `*` stars flash over it here and there, a new scatter every step: each cell has a
`density` in 40 chance (1-20) of being a star. The scatter is seeded without the text, so a ticking clock keeps
sparkling evenly. With no text, a field of `width` cells of stars: a little night sky.

### wave (loops)

```json
{"type": "wave", "ms": 110, "crest": 2, "lead": 4, "order": "lr"}
```

A ripple `crest` letters wide (1-8) runs along the text, swapping capitals and small letters under it
(`Hello World`, `hEllo World`, `HeLlo World`, ...), then `lead` steps of calm before the next. Other characters are
left alone, so the text never moves. `order`: `lr` or `rl`. Reads best on mixed-case text.

### sequence (loops, or finite)

```json
{"type": "sequence", "texts": ["( o o )", "( o o )", "( o o )", "( - - )"], "ms": 300, "loop": true}
```

The generic one: your own list of texts shown in turn, `ms` each. Live values work in the texts. `"loop": false`
plays the list once and then shows the frame's own text with its `after` style (a home-made entrance). Each text is
centred by its own length, so texts of the same length stay in place (pad with spaces: `"LOADING   "`,
`"LOADING.  "`, ...). An empty list just shows the frame's text.

## Timing

- **Airtime:** 1 ms per bit, so a still frame of *n* characters (*n* + 5 bytes) is on the IR link for
  (*n* + 5) × 8 ms plus the idle gap (10 ms by default): 8 characters 114 ms, 12 characters 146 ms, 35 characters 330 ms.
- **A step lasts max(`ms`, airtime, `animMinMs`).** The airtime uses the frame as actually sent (after live values) and
  the IR gap currently set, so a longer gap slows animations down instead of piling frames up.
- **Steps run on a clock.** Each step is due a whole step time after the previous one was due, so tick jitter
  doesn't accumulate. If the engine falls more than a step behind (a slow tick, a send that blocked), it skips steps
  (up to 16 at a time, never the last step of a finite animation) instead of sending a burst; if it is still behind
  it restarts the clock from now. It never queues frames faster than the link sends them, so the 2-deep display queue
  doesn't block the display loop. `scene_engine_tick` stays non-blocking.
- Times are rounded up to whole 10 ms ticks.
- A finite animation's last step (the full text) stays up for its step time, then the frame's `after` style is sent.
- A frame's `seconds` includes its animation; a shorter frame cuts it off (the designer warns).
- **Tuning on the device:** every animation has `ms`; `POST /api/v1/settings {"animMinMs": 250}` sets a floor for every
  step of every animation (0 = off, max 5000; read when the playlist or a scene is next saved, or at boot) for when the
  globe turns out to need more time between still frames; the IR gap (Explorer) slows everything.

## Limits (validated when a scene is parsed)

| Option | Range | | Option | Range |
|---|---|---|---|---|
| `ms` | 20-60000 | | `width` | 3-35 (chase 6+, fish 5+) |
| `lead` | 0-30 | | `amp` | 0-60 |
| `steps` | 1-60 | | `rule` | 0-255 |
| `period` | 400-20000 | | `gens` | 2-250 |
| `pause` | 0-5000 | | `seed` | 0-4294967295 |

Out-of-range numbers are clamped; unknown names, unknown option values and glyphs the globe can't draw (`cursor`,
`bg`, `trail`, `live`, `dead`) are ignored and the default stays. Every step is at most 35 characters. `sequence`
takes at most 16 texts of up to 63 bytes each (before live values); on the globe all sequence texts of one scene
share a 4 KB pool, and texts beyond it are dropped with a log warning. Memory: the engine's scene buffer
(`eng_scene_t`) is 9448 bytes with the pool (was 3236), allocated once, from PSRAM when there is some.

## What to check on the globe

Nothing here has been seen on the real display. What is proven: re-sending `05` frames at a new shift (the
pendulum) and sending new still text. What is not: sending *new* still text several times a second. Suggested
order, simplest and most informative first:

1. **Rapid re-sends of still text** (the risk everything else depends on). Play the "Animation: decode" template.
   Does each new `05` frame replace the last one cleanly, or does the globe blank, flash or redraw visibly in between?
   If it flickers, try `"ms": 250` and then `animMinMs` to find the shortest clean step.
2. **Rate.** Bounce (`{"text": "HELLO", "anim": {"type": "bounce"}}`) runs at the link limit (114 ms per frame).
   Check the IR stats on the classic page (frames/s, link busy %; also in the serial log) and that the motion is
   even, with no frames lost or doubled.
3. **Position of padded text.** Typewriter and decode pad to the final length so the text shouldn't move; confirm
   it doesn't jump between the first step and the settled frame (`after: hold` and `after: spin`).
4. **Bounce range and smoothness.** Short text is padded to 8 cells and moves over shift 0..16 like the pendulum.
   Does it look smoother than the pendulum? Tune `period`, `pause`, `ms`; try `amp` 12 to see if wider travel works.
5. **Scanner and sprites** (10-cell frames, 130-150 ms). Whole-cell steps should read as lamps (scanner) and
   cartoons; check `C`/`O` for Pac-Man's mouth and `><>`/`<><` for the fish in the real font.
6. **Blink and flash during loops.** Does re-sending restart the blink cycle? If so, `after: "flash"` on a looping
   animation (and `blink` characters with `after: "hold"`) will look always on, since frames come faster than the
   ~1 Hz blink.
7. **count and the decode clock.** `onChange: "count"` with a pushed value (`POST /api/v1/values {"cpu": 57}`),
   and the Clock studio's Decode style at the minute change; check the colon blink isn't disturbed.
8. **Automaton** (35-character frames, 330 ms each). Does a 35-character still frame wrap the band, and does
   shift 35 keep it centred? This is the first use of full-length still text; it may need a different `width`.
9. **sequence with texts of different lengths.** Shift = length is assumed to centre any length (only 8 characters
   were measured); if it doesn't, texts will jump and should be padded to one length.

Most at risk, in order: anything if (1) shows flicker; then the automaton (long still frames, slow), sequences with
mixed lengths, blink during loops, and bounce smoothness at ~9 frames a second.
