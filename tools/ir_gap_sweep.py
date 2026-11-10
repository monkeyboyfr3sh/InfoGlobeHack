#!/usr/bin/env python3
"""Find the smallest reliable IR idle gap between frames (docs/protocol.md, open questions).

Film the globe while this runs. For each gap it shows a label ("GAP 05") for a
few seconds, then runs the firmware's IR test pattern for --seconds: still
frames "TEST 000", "TEST 001", ... sent back to back as fast as the link takes
them (13 bytes each, so 104 ms on air plus the gap). On the video a gap is
reliable when the numbers count up one by one with nothing skipped, garbled or
stuck. Skipped numbers, stray characters or a blank globe mean it is too short.

Usage (Python 3.8+, standard library only; run from anywhere):
  python tools/ir_gap_sweep.py --globe http://192.168.0.71 --dry-run    # print the plan, send nothing
  python tools/ir_gap_sweep.py --globe http://192.168.0.71
  python tools/ir_gap_sweep.py --globe http://192.168.0.71 --gaps 6,5,4,3,2,1,0 --seconds 20
  python tools/ir_gap_sweep.py --globe http://192.168.0.71 --token SECRET   (or set INFOGLOBE_TOKEN)

The globe's address can also come from INFOGLOBE_URL. Stop at any time with
Ctrl+C: it stops the test pattern, puts the previous gap back and returns the
globe to its playlist if it was playing one. The new gap is never saved; a
reboot also restores the configured one (menuconfig: IR idle gap).

Tips for filming: 60 fps or more, the front of the globe filling the frame,
and say or show the time when you start. The console prints a timestamp for
every step and the globe's own frame counter, to line the video up with.
"""

import argparse
import json
import os
import signal
import sys
import time
import urllib.error
import urllib.request

FRAME_BITS = 13 * 8     # "TEST nnn" as a 05 frame: 05 00 <8 chars> 00 <blink> <shift>


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
    ap.add_argument('--gaps', default='10,8,6,5,4,3,2,1,0', help='gaps to try in ms, in order (default 10,8,6,5,4,3,2,1,0)')
    ap.add_argument('--seconds', type=float, default=12, help='how long to run the test pattern at each gap (default 12)')
    ap.add_argument('--label-seconds', type=float, default=3, help='how long to show the GAP label before each run (default 3)')
    ap.add_argument('--dry-run', action='store_true', help='read the globe state and print the plan, change nothing')
    args = ap.parse_args()
    if not args.globe:
        ap.error('give the globe address with --globe http://<ip> or set INFOGLOBE_URL')
    try:
        gaps = [int(g) for g in args.gaps.split(',') if g.strip()]
    except ValueError:
        ap.error('--gaps must be whole numbers, e.g. 10,8,6,4,2')
    if not gaps or any(g < 0 or g > 500 for g in gaps):
        ap.error('gaps must be 0-500 ms (the firmware limit)')

    globe = Globe(args.globe, args.token, args.dry_run)
    ir = globe.call('GET', '/ir')
    if ir.get('test'):
        sys.exit('The IR test pattern is already running (someone else is testing?). Stop it first: POST /ir {"test": false}')
    prev_gap = ir.get('gap_ms')
    if not isinstance(prev_gap, int):
        sys.exit(f'Unexpected answer from GET /ir: {ir}')
    mode = globe.call('GET', '/mode')
    was_playing = bool(mode.get('time_mode'))
    total = len(gaps) * (args.seconds + args.label_seconds + 1)
    print(f'{stamp()}  globe {globe.base}: gap now {prev_gap} ms, playlist {"on" if was_playing else "off"}')
    print(f'          plan: gaps {gaps} ms, {args.seconds:g} s each, about {total / 60:.1f} min in all')

    # Ctrl+Break on Windows and SIGTERM elsewhere end the run the same way as Ctrl+C.
    def stop(*_):
        raise KeyboardInterrupt
    for name in ('SIGTERM', 'SIGBREAK'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), stop)

    rows = []
    try:
        for g in gaps:
            expect_fps = 1000.0 / (FRAME_BITS + g)
            print(f'{stamp()}  GAP {g:2d} ms  (at most {expect_fps:.1f} frames/s)')
            globe.call('POST', '/ir', {'gap_ms': g})
            globe.call('POST', '/api/v1/frame', {'text': f'GAP {g:02d}', 'after': 'hold'})
            time.sleep(args.label_seconds)
            before = globe.call('GET', '/ir').get('frames', 0)
            t0 = time.time()
            globe.call('POST', '/ir', {'test': True})
            time.sleep(args.seconds)
            mid = globe.call('GET', '/ir')
            globe.call('POST', '/ir', {'test': False})
            sent = (mid.get('frames', 0) - before) if not args.dry_run else 0
            rows.append((g, expect_fps, mid.get('fps', 0), sent / max(time.time() - t0, 0.001)))
            print(f'{stamp()}    sent {sent} frames; link {mid.get("fps", 0):.1f} frames/s, busy {mid.get("busy_pct", 0):.0f} %')
            time.sleep(1)
    except KeyboardInterrupt:
        print(f'\n{stamp()}  stopped')
    except RuntimeError as e:
        print(f'\n{stamp()}  {e}')
    finally:
        # Put everything back, retrying in case the network hiccups.
        for desc, path, body in (('stop the test pattern', '/ir', {'test': False}),
                                 (f'restore the gap to {prev_gap} ms', '/ir', {'gap_ms': prev_gap})):
            try:
                globe.call('POST', path, body, retries=3)
                print(f'{stamp()}  {desc}: done')
            except RuntimeError as e:
                print(f'{stamp()}  could not {desc}: {e}. Do it by hand: POST {path} {json.dumps(body)}')
        if was_playing:
            try:
                globe.call('POST', '/api/v1/resume', {}, retries=3)
                print(f'{stamp()}  playlist resumed')
            except RuntimeError as e:
                print(f'{stamp()}  could not resume the playlist: {e}')

    if rows:
        print('\n  gap ms   max fps   link fps (globe)   sent/s (PC clock)')
        for g, e, f, s in rows:
            print(f'  {g:6d}   {e:7.1f}   {f:16.1f}   {s:13.1f}')
        print('\n  The globe counts frames it sent, not frames the display understood:')
        print('  only the video tells you which gaps work. Note the smallest gap where')
        print('  every TEST number appears, and add a few ms of margin.')


if __name__ == '__main__':
    main()
