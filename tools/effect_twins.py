#!/usr/bin/env python3
"""Are effects 4, 5 and 19 really duplicates? Play suspect effects back to back for the camera
(docs/protocol.md, open questions).

For each group (default: 0 vs 4, 1 vs 5, and 16 17 18 19) it shows a title
("SAME? 00 04"), then for every effect in the group, --repeat times:
  1. a still "old" text with the effect number:  FX04 ABCDEFG
  2. the "new" text arriving with that effect:     FX04 1234567
Every effect gets the same old and new text, so on video the only difference
is the transition itself, and the number on screen says which one it is.
The new text is sent with "after": "spin", so the globe plays the whole
transition (the follow-up still frame of "hold" would cut it short if the
duration estimate in effects.json is too low).

Usage (Python 3.8+, standard library only):
  python tools/effect_twins.py --globe http://192.168.0.71 --dry-run    # print the plan, send nothing
  python tools/effect_twins.py --globe http://192.168.0.71
  python tools/effect_twins.py --globe http://192.168.0.71 --groups 0,4 16,19 --repeat 3
  python tools/effect_twins.py --globe http://192.168.0.71 --all-twins  # also 6/7, 12/20, 13/21
  python tools/effect_twins.py --globe http://192.168.0.71 --token SECRET   (or set INFOGLOBE_TOKEN)

The globe's address can also come from INFOGLOBE_URL. Stop at any time with
Ctrl+C; the globe goes back to its playlist if it was playing one.

What to look for: which way the new text arrives (left/right, up/down),
what happens to the old text (overwritten letter by letter, wiped, pushed,
dropped), the speed, and how it ends (still, or spinning, and which way).
For 16-19 note both directions for each: the table says 16 in up/out down,
17 in down/out up, 18 in down/out down, and 19 (unsure) in up/out up.
"""

import argparse
import json
import os
import signal
import sys
import time
import urllib.error
import urllib.request

DEFAULT_GROUPS = [[0, 4], [1, 5], [16, 17, 18, 19]]
OTHER_TWINS = [[6, 7], [12, 20], [13, 21]]      # marked "looks like" in protocol.md as well


class Globe:
    def __init__(self, base, token, dry_run):
        self.base = base.rstrip('/')
        self.token = token
        self.dry_run = dry_run

    def call(self, method, path, body=None, retries=0):
        if self.dry_run and method != 'GET':
            print(f'    (dry run) {method} {path} {json.dumps(body) if body is not None else ""}')
            return {}
        data = json.dumps(body).encode() if body is not None else None
        headers = {'Content-Type': 'application/json'} if data else {}
        if self.token:
            headers['Authorization'] = f'Bearer {self.token}'
        for attempt in range(retries + 1):
            try:
                req = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)
                with urllib.request.urlopen(req, timeout=10) as r:
                    text = r.read().decode('utf-8', 'replace')
                    try:
                        return json.loads(text)
                    except ValueError:
                        return {'text': text}
            except urllib.error.HTTPError as e:
                detail = e.read().decode('utf-8', 'replace')[:200]
                raise RuntimeError(f'{method} {path} failed: HTTP {e.code} {detail}') from e
            except (urllib.error.URLError, OSError) as e:
                if attempt == retries:
                    raise RuntimeError(f'{method} {path} failed: {e}') from e
                time.sleep(0.5)


def stamp():
    return time.strftime('%H:%M:%S')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--globe', default=os.environ.get('INFOGLOBE_URL'), help='e.g. http://192.168.0.71 (or INFOGLOBE_URL)')
    ap.add_argument('--token', default=os.environ.get('INFOGLOBE_TOKEN'), help='API token, if the globe requires one (or INFOGLOBE_TOKEN)')
    ap.add_argument('--groups', nargs='+', metavar='A,B[,C...]', help='effects to compare, e.g. 0,4 1,5 16,17,18,19 (the default)')
    ap.add_argument('--all-twins', action='store_true', help='also compare 6/7, 12/20 and 13/21')
    ap.add_argument('--repeat', type=int, default=2, help='how many times to play each group (default 2)')
    ap.add_argument('--hold', type=float, default=2.5, help='seconds the old text stays before each transition (default 2.5)')
    ap.add_argument('--wait', type=float, help='seconds to watch each transition (default: 1.5 x its estimated length, at least +3 s)')
    ap.add_argument('--dry-run', action='store_true', help='read the globe state and print the plan, change nothing')
    args = ap.parse_args()
    if not args.globe:
        ap.error('give the globe address with --globe http://<ip> or set INFOGLOBE_URL')
    try:
        groups = [[int(x) for x in g.split(',')] for g in args.groups] if args.groups else [list(g) for g in DEFAULT_GROUPS]
    except ValueError:
        ap.error('--groups takes comma-separated effect numbers, e.g. 0,4 16,19')
    if args.all_twins:
        groups += OTHER_TWINS
    if any(n < 0 or n > 37 for g in groups for n in g):
        ap.error('effects are 0-37')
    if args.repeat < 1:
        ap.error('--repeat must be at least 1')

    globe = Globe(args.globe, args.token, args.dry_run)
    fx = globe.call('GET', '/api/v1/effects')
    length = {e['id']: e.get('durationMs', 4500) for e in fx} if isinstance(fx, list) else {}
    was_playing = bool(globe.call('GET', '/mode').get('time_mode'))

    def wait_for(n):
        ms = length.get(n, 4500)
        return args.wait if args.wait is not None else max(ms * 1.5, ms + 3000) / 1000

    total = sum(3 + args.repeat * sum(args.hold + wait_for(n) for n in g) for g in groups)
    print(f'{stamp()}  globe {globe.base}: playlist {"on" if was_playing else "off"}')
    print(f'          plan: {" | ".join(" vs ".join(map(str, g)) for g in groups)}, {args.repeat} rounds, about {total / 60:.1f} min')

    def stop(*_):
        raise KeyboardInterrupt
    for name in ('SIGTERM', 'SIGBREAK'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), stop)

    try:
        for g in groups:
            title = 'SAME? ' + ' '.join(f'{n:02d}' for n in g)
            print(f'{stamp()}  {title}')
            globe.call('POST', '/api/v1/frame', {'text': title, 'after': 'hold'})
            time.sleep(3)
            for r in range(args.repeat):
                for n in g:
                    globe.call('POST', '/api/v1/frame', {'text': f'FX{n:02d} ABCDEFG', 'after': 'hold'})
                    time.sleep(args.hold)
                    print(f'{stamp()}    round {r + 1}: effect {n} ({length.get(n, "?")} ms estimated)')
                    globe.call('POST', '/api/v1/frame', {'text': f'FX{n:02d} 1234567', 'entrance': n, 'after': 'spin'})
                    time.sleep(wait_for(n))
        print(f'{stamp()}  done')
    except KeyboardInterrupt:
        print(f'\n{stamp()}  stopped')
    except RuntimeError as e:
        print(f'\n{stamp()}  {e}')
    finally:
        if was_playing:
            try:
                globe.call('POST', '/api/v1/resume', {}, retries=3)
                print(f'{stamp()}  playlist resumed')
            except RuntimeError as e:
                print(f'{stamp()}  could not resume the playlist: {e}. Do it by hand: POST /api/v1/resume')


if __name__ == '__main__':
    main()
