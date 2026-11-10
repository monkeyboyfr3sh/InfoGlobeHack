# Pushed values: getting your data onto the globe

Anything on your network can put a value on the globe with one HTTP request, and any scene can show it:
a frame whose text is `CPU {var:cpu}` shows `CPU 42`, and changes on its own when the value does.
The formats (`{var:x}`, `{x}`, `{bar:x}`, `{pct:x:250}`) are in [live-values.md](live-values.md); the endpoint is in
[api.md](api.md#pushed-values). This page is recipes.

In the examples the globe is at `http://192.168.0.71` (it shows its address when it joins the network); use yours.

## The rules

- `POST /api/v1/values` with a JSON object: `{"cpu": 42, "build": "PASS"}`. Text and numbers set a value,
  `true`/`false` become `YES`/`NO`, `null` removes one. The answer is `{"ok":true}`, or a 400 with the reason.
- Names: 1-15 of `a-z 0-9 _`. Values: up to 47 characters. Up to 64 values on the globe at once.
- **Values live in RAM.** They are gone after a reboot, and with `?ttl=N` they vanish N seconds after the last
  update. Use a TTL a few times your update interval, so a value whose sender died shows `--` instead of a stale number.
- The globe's font has no `%`, `+`, `;`, `@` or `[ ]`. Push `42` and write the unit in the frame.
- **Access token.** If one is set on the globe (Settings page), every change needs it: send
  `Authorization: Bearer <token>`, `X-Globe-Token: <token>` or, for senders that can't set headers, `?token=<token>`.
  See [api.md](api.md#access-token). Prefer a header: a token in the URL ends up in logs. With the current firmware,
  `?ttl=` is also ignored when the whole query string is longer than 47 characters, so `?ttl=600&token=...` only keeps
  its TTL with a token of up to 33 characters (the web app's generated tokens are 20).

## Showing a value

Make a scene in the Designer with a frame like `CPU {var:cpu}`, then add it to the playlist on the Dashboard.
Or from the command line (this saves scene `pc`; add it to the playlist on the Dashboard):

```sh
curl -X POST http://192.168.0.71/api/v1/scenes/pc -H "Content-Type: application/json" -d '{"name":"PC","frames":[
  {"text":"CPU {var:cpu}","after":"hold","seconds":4},
  {"text":"RAM {var:ram}","after":"hold","seconds":4},
  {"text":"{bar:disk}","entrance":12,"after":"hold","seconds":4}]}'
```

Keep held (still) text to about 8-12 characters; longer text should spin. Use `{var:cpu}` rather than `{cpu}`:
when the value is missing, `{var:cpu}` shows `--` while a bare `{cpu}` stays in the text.

## One-liners

curl (Linux, macOS, Git Bash, or `curl.exe` in Windows `cmd`):

```sh
curl -X POST "http://192.168.0.71/api/v1/values?ttl=600" -H "Content-Type: application/json" -d '{"cpu":42}'
curl -X POST "http://192.168.0.71/api/v1/values" -H "Authorization: Bearer $GLOBE_TOKEN" -H "Content-Type: application/json" -d '{"door":"OPEN"}'
curl -X POST "http://192.168.0.71/api/v1/values" -H "Content-Type: application/json" -d '{"door":null}'      # remove it
curl http://192.168.0.71/api/v1/values                                                                       # what's there now
```

PowerShell (Windows PowerShell 5.1 and PowerShell 7). In 5.1, `curl` is an alias for `Invoke-WebRequest`, and quotes
inside arguments to `curl.exe` get mangled, so use `Invoke-RestMethod`:

```powershell
$globe = 'http://192.168.0.71'
Invoke-RestMethod -Method Post -Uri "$globe/api/v1/values?ttl=600" -ContentType 'application/json' -Body (@{ cpu = 42 } | ConvertTo-Json)
# with a token:
Invoke-RestMethod -Method Post -Uri "$globe/api/v1/values" -ContentType 'application/json' -Headers @{ Authorization = "Bearer $env:GLOBE_TOKEN" } -Body '{"door":"OPEN"}'
```

## Python

Standard library only; the later recipes reuse this `push()`.

```python
import json, os, urllib.request

GLOBE = os.environ.get('GLOBE', 'http://192.168.0.71')
TOKEN = os.environ.get('GLOBE_TOKEN', '')          # only if the globe has an access token

def push(values, ttl=600):
    """push({'cpu': 42, 'build': 'PASS'}); None removes a value."""
    headers = {'Content-Type': 'application/json'}
    if TOKEN:
        headers['Authorization'] = f'Bearer {TOKEN}'
    req = urllib.request.Request(f'{GLOBE}/api/v1/values?ttl={ttl}', data=json.dumps(values).encode(),
                                 headers=headers, method='POST')
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.load(r)

push({'cpu': 42, 'build': 'PASS'})
```

## PC stats every few seconds

CPU, RAM and disk use as whole percents, every 5 s, expiring after 30 s if the script stops.
Needs [psutil](https://pypi.org/project/psutil/) (`pip install psutil`); works on Windows, Linux and macOS.

```python
import os, time
import psutil                                       # plus push() from above

DISK = os.path.abspath(os.sep)                      # C:\ on Windows, / elsewhere
while True:
    try:
        push({'cpu': round(psutil.cpu_percent(interval=1)),
              'ram': round(psutil.virtual_memory().percent),
              'disk': round(psutil.disk_usage(DISK).percent)}, ttl=30)
    except OSError as e:                            # globe off or unreachable: keep trying
        print('globe:', e)
    time.sleep(4)
```

For several PCs, give each its own names (`cpu_desk`, `cpu_nas`; 15 characters at most).
The same without Python, in PowerShell on Windows:

```powershell
$globe = 'http://192.168.0.71'
while ($true) {
    $cpu  = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
    $os   = Get-CimInstance Win32_OperatingSystem
    $ram  = 100 * (1 - $os.FreePhysicalMemory / $os.TotalVisibleMemorySize)
    $c    = Get-PSDrive C
    $disk = 100 * $c.Used / ($c.Used + $c.Free)
    $body = @{ cpu = [int]$cpu; ram = [int]$ram; disk = [int]$disk } | ConvertTo-Json
    try { Invoke-RestMethod -Method Post -Uri "$globe/api/v1/values?ttl=30" -ContentType 'application/json' -Body $body | Out-Null }
    catch { Write-Warning $_ }
    Start-Sleep -Seconds 5
}
```

## A progress bar for a long job

Push a number from 0 to 100 and show `{bar:print}`: at 60 it draws `######----`; `{pct:print}` gives `60`.
For a count out of something else, say which: `{bar:done:250}` is a bar of `done` out of 250.

Any script:

```sh
for i in $(seq 0 10 100); do
  curl -s -X POST "http://192.168.0.71/api/v1/values?ttl=120" -H "Content-Type: application/json" -d "{\"print\":$i}"
  sleep 1                                           # your real work here
done
curl -s -X POST http://192.168.0.71/api/v1/values -H "Content-Type: application/json" -d '{"print":null,"job":"DONE"}'
```

A 3D print from **OctoPrint** (`GET /api/job` with your API key from OctoPrint's settings; `completion` is a percent,
empty while idle):

```python
import json, time, urllib.request                  # plus push() from above

OCTOPRINT, KEY = 'http://octopi.local', 'your-api-key'
while True:
    req = urllib.request.Request(f'{OCTOPRINT}/api/job', headers={'X-Api-Key': KEY})
    with urllib.request.urlopen(req, timeout=5) as r:
        job = json.load(r)
    push({'print': round(job['progress']['completion'] or 0), 'print_state': job['state'].upper()[:47]}, ttl=120)
    time.sleep(15)
```

From **Moonraker** (Klipper, Mainsail, Fluidd). `virtual_sdcard.progress` is 0-1 by file position;
`display_status.progress` is the slicer's estimate if it sends `M73`. On most installs a LAN address is a trusted
client and needs no key; if yours asks for one, add an `X-Api-Key` header the same way as above.

```python
MOONRAKER = 'http://mainsailos.local'
while True:
    url = f'{MOONRAKER}/printer/objects/query?print_stats&virtual_sdcard'
    with urllib.request.urlopen(url, timeout=5) as r:
        st = json.load(r)['result']['status']
    push({'print': round(st['virtual_sdcard']['progress'] * 100), 'print_state': st['print_stats']['state'].upper()}, ttl=120)
    time.sleep(15)
```

A scene for it: `{"text":"{bar:print}","after":"hold"}` then `{"text":"{var:print_state}","after":"spin"}`.
I couldn't test these two against a printer: check the field names against your OctoPrint or Moonraker version.

## Home Assistant

A [`rest_command`](https://www.home-assistant.io/integrations/rest_command/) in `configuration.yaml`, with the token
(if any) in `secrets.yaml` as `globe_auth: "Bearer your-token"`:

```yaml
rest_command:
  globe_value:
    url: "http://192.168.0.71/api/v1/values?ttl={{ ttl | default(3600) }}"
    method: POST
    content_type: "application/json"
    headers:
      Authorization: !secret globe_auth            # leave out without a token
    payload: '{"{{ key }}": "{{ value }}"}'
```

Then, in an automation:

```yaml
- alias: Laundry done on the globe
  triggers:
    - trigger: numeric_state
      entity_id: sensor.washer_power
      below: 3
      for: "00:02:00"
  actions:
    - action: rest_command.globe_value
      data:
        key: laundry
        value: DONE
        ttl: 1800
```

For a reading that changes all the time (outdoor temperature), trigger on a `time_pattern` every few minutes and push
`value: "{{ states('sensor.outdoor_temperature') | round(0) | int }}"` with a TTL a few times that interval.
The automation syntax above (`triggers:`, `trigger:`, `actions:`, `action:`) is the current one; older Home Assistant
versions write `trigger:`, `platform:`, `action:` and `service:` instead.

## GitHub Actions

GitHub's own runners are on the internet and **can't reach a globe on your LAN**. This works from a
[self-hosted runner](https://docs.github.com/en/actions/hosting-your-own-runners) on the same network
(`runs-on: self-hosted`), with the token in the repository's secrets as `GLOBE_TOKEN`:

```yaml
      - name: Build result on the globe
        if: always()
        shell: bash
        run: |
          curl -fsS -m 5 -X POST "http://192.168.0.71/api/v1/values?ttl=86400" \
            -H "Authorization: Bearer ${{ secrets.GLOBE_TOKEN }}" -H "Content-Type: application/json" \
            -d '{"build":"${{ job.status == 'success' && 'PASS' || 'FAIL' }}"}' || true
```

`if: always()` runs it after a failure too; `|| true` keeps an unreachable globe from failing the build.

## Uptime Kuma and other webhooks

Webhook senders usually post their own JSON shape, which the globe refuses (it wants a flat object of names and plain
values). Use the sender's custom body option. In Uptime Kuma: add a notification of type **Webhook**, Post URL
`http://192.168.0.71/api/v1/values?ttl=86400` (add `&token=...` if the globe has one; or put
`{"Authorization": "Bearer ..."}` in Additional Headers), request body **Custom Body**:

```
{"nas": "{% if heartbeatJSON.status == 1 %}UP{% else %}DOWN{% endif %}"}
```

and a frame `NAS {var:nas}` in the playlist. Uptime Kuma's preset bodies (application/json, form-data) won't work.
The custom body uses Uptime Kuma's template variables (`heartbeatJSON`, `monitorJSON`, `msg`); I'm not certain they are
named the same in every version, so press **Test** and check `GET /api/v1/values` before relying on it.
Any other tool that can send a POST with a body you choose (cron jobs, Node-RED, n8n, scripts) works the same way.

## Claude Code: "done" and "needs you" on the globe

[Claude Code hooks](https://docs.anthropic.com/en/docs/claude-code/hooks) run a command on events: `Stop` when Claude
has finished answering, `Notification` when it needs your permission or is waiting for input. Add to
`~/.claude/settings.json` (all projects) or `.claude/settings.json` (one project):

```json
{
  "hooks": {
    "Stop": [
      { "hooks": [ { "type": "command", "command": "curl -s -m 3 -X POST 'http://192.168.0.71/api/v1/values?ttl=600' -H 'Content-Type: application/json' -d '{\"claude\":\"DONE\"}' || true" } ] }
    ],
    "Notification": [
      { "hooks": [ { "type": "command", "command": "curl -s -m 3 -X POST 'http://192.168.0.71/api/v1/values?ttl=600' -H 'Content-Type: application/json' -d '{\"claude\":\"NEEDS YOU\"}' || true" } ] }
    ]
  }
}
```

and a frame `CLAUDE {var:claude}` in the playlist (with `"onChange": 37` it plays a transition each time the word
changes). With a token, add `-H 'Authorization: Bearer your-token'`. `-m 3` and `|| true` keep a switched-off globe
from holding Claude up or showing as a hook error. The commands use POSIX shell quoting (macOS, Linux, and Windows
when Claude Code runs commands through Git Bash); I haven't checked other Windows shells. `/hooks` in Claude Code
shows what is set up.

To interrupt the playlist instead, post a quick message to `/api/v1/play`, e.g.
`{"frames":[{"text":"CLAUDE NEEDS YOU","entrance":37,"after":"flash","seconds":10}],"seconds":10}`.
That one is saved to flash each time (as scene `quick`), so keep it for events that don't fire every minute.
