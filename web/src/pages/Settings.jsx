import { useEffect, useRef, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { TOKEN_RE, TOKEN_RULES, generateToken } from '../lib/request.js';
import { Choice, Row, LocationPicker, Switch, hasLocation } from '../components/Common.jsx';
import { Mini } from '../components/Mini.jsx';
import { cleanTime, showTime, ensureScene } from '../lib/rotation.js';
import { fillValues, setVars, setLocation } from '../lib/values.js';
import { VALUES as WX_VALUES, STATUS as WX_STATUS, weatherScene, ago } from '../lib/feeds/weather.js';
import { VALUES as AC_VALUES, PLANE_SCENE } from '../lib/feeds/aircraft.js';
import { RELEASES, VERSION, inline } from '../lib/releases.js';
import { makeBackup, restoreBackup, checkBackup, download } from '../lib/backup.js';

// Settings: everything about the globe that isn't what it shows. The display and its
// night schedule, where it is, the weather and plane feeds, and, folded away, the
// rarely needed: animation pace, scripts, firmware and the access token.

function AccessToken({ toast }) {
  const [info, setInfo] = useState(null);          // {required, valid}; false = couldn't ask
  const [draft, setDraft] = useState('');
  const [show, setShow] = useState(false);
  const [busy, setBusy] = useState(false);
  const host = api.GLOBE || location.origin;
  const mine = api.getToken();
  const load = () => api.getAuth().then(setInfo, () => setInfo(false));
  useEffect(() => { load(); }, []);

  const apply = async (token, done) => {
    setBusy(true);
    try { await api.setAuthToken(token); setDraft(''); toast(true, done); await load(); }
    catch (e) { toast(false, e.message); }
    setBusy(false);
  };
  const save = (e) => {
    e.preventDefault();
    if (!TOKEN_RE.test(draft)) { toast(false, `Tokens are ${TOKEN_RULES}`); return; }
    apply(draft, info && info.required ? 'Token changed. This browser has the new one.' : 'Token set. Changes now need it; this browser has it.');
  };
  const remove = () => {
    if (confirm('Remove the access token? Anyone on your network will be able to change the globe again.')) {
      apply('', 'Token removed. Anyone on your network can make changes.');
    }
  };
  const useHere = async (e) => {           // the globe has a token this browser doesn't know (yet)
    e.preventDefault();
    try {
      const r = await api.getAuth(draft.trim());
      if (!r.valid) { toast(false, 'That is not the globe\'s token.'); return; }
      api.saveToken(draft.trim()); setDraft(''); toast(true, 'This browser now has the token.'); load();
    } catch (err) { toast(false, err.message); }
  };
  const forget = () => { api.saveToken(''); toast(true, 'Forgotten on this browser.'); load(); };

  const shown = show && mine ? mine : 'YOUR-TOKEN';
  const state = info === null ? <p class="muted">Checking…</p>
    : info === false ? <div class="problem">Couldn't ask the globe. It may be offline, or its firmware predates access tokens.</div>
    : !info.required ? <div class="problem">Off: anyone on your network can change what the globe shows, its scenes and settings, and its firmware.</div>
    : info.valid ? <p class="token-state ok">On. This browser has the token, so changes from here work.</p>
    : <p class="token-state">On. This browser doesn't have the token; it will ask for it when you change something.</p>;

  return (
    <section class="token-card">
      <div class="card-head"><h2>Access token</h2>{info && <span class={`pill ${info.required ? 'playlist' : 'manual'}`}>{info.required ? 'On' : 'Off'}</span>}</div>
      <p class="hint">With a token set, the globe only accepts changes that carry it. Looking stays open.</p>
      {state}

      {info && info.required && !info.valid ? (
        <form class="row token-form" onSubmit={useHere}>
          <input type="password" value={draft} onInput={(e) => setDraft(e.currentTarget.value)} placeholder="The globe's token"
            aria-label="The globe's token" autocomplete="current-password" />
          <button class="btn primary" type="submit" disabled={!draft.trim()}>Use on this browser</button>
        </form>
      ) : info && (
        <form class="row token-form" onSubmit={save}>
          <input type={show ? 'text' : 'password'} value={draft} onInput={(e) => setDraft(e.currentTarget.value.trim())}
            placeholder={info.required ? 'New token' : 'Choose a token'} aria-label="New token" autocomplete="new-password" maxLength={64} />
          <button class="btn" type="button" onClick={() => { setDraft(generateToken()); setShow(true); }} title="20 random letters and digits">Generate</button>
          <button class="btn primary" type="submit" disabled={busy || !draft}>{info.required ? 'Change token' : 'Set token'}</button>
        </form>
      )}
      {info && <p class="muted small">{TOKEN_RULES}. Write it down: a lost token can only be cleared at the globe (see below).</p>}

      <details class="token-more">
      <summary>Using the token from scripts and webhooks</summary>
      <p class="muted small">Send it in a header, or in the URL when the sender can't set headers (Home Assistant, Uptime Kuma, ...):</p>
      <pre class="bytes">{`curl -X POST ${host}/api/v1/values -H "Authorization: Bearer ${shown}" -d '{"cpu":42}'
curl -X POST "${host}/api/v1/values?token=${shown}" -d '{"cpu":42}'`}</pre>
      <p class="muted small">A token in a URL can end up in the sender's logs and history; use a header where you can.</p>
      </details>

      <h3 class="token-sub">Lost it?</h3>
      <p class="muted small">
        Hold the globe's button for 10 seconds. The globe clears the token and shows <b>TOKEN CLEARED</b>; a short press
        brings the clock back. Then set a new one here.
      </p>

      <div class="card-foot">
        {mine && <label class="check"><input type="checkbox" checked={show} onChange={(e) => setShow(e.currentTarget.checked)} /> Show token in examples</label>}
        <span style="flex:1" />
        {mine && <button class="btn" onClick={forget}>Forget on this browser</button>}
        {info && info.required && <button class="btn" onClick={remove} disabled={busy}>Remove token</button>}
      </div>
    </section>
  );
}

/* ---------- the globe's display: the switch and the night schedule ---------- */

function Display({ settings, save, display }) {
  const [typed, setTyped] = useState({});            // the boxes, while they aren't what the globe has
  const offAt = cleanTime(settings.displayOffAt), onAt = cleanTime(settings.displayOnAt);
  const scheduled = !!offAt && !!onAt;
  const shown = { off: typed.off || offAt || '23:00', on: typed.on || onAt || '07:00' };
  const nightNow = (t) => {
    const m = (x) => +x.slice(0, 2) * 60 + +x.slice(3), d = new Date(), n = d.getHours() * 60 + d.getMinutes();
    return m(t.off) < m(t.on) ? n >= m(t.off) && n < m(t.on) : n >= m(t.off) || n < m(t.on);
  };
  const put = (t) => save({ displayOffAt: t.off, displayOnAt: t.on },
    `Off at ${showTime(t.off)}, back on at ${showTime(t.on)}${nightNow(t) ? '. It is inside those hours now, so the display is going dark' : ''}`);
  const toggle = (next) => (next ? put(shown) : save({ displayOffAt: '', displayOnAt: '' }, 'Night schedule off'));
  const setTime = (which) => (e) => {
    const v = cleanTime(e.currentTarget.value);
    if (!v) return;
    const t = { ...shown, [which]: v };
    setTyped(t);
    if (scheduled && t.off !== t.on) put(t);
  };
  return (
    <section class="card" id="display">
      <div class="card-head"><h2>Display</h2><span class={`pill ${display.on ? 'playlist' : 'off'}`}>{display.on ? 'On' : 'Off'}</span></div>
      <Row label="Display" hint="off blanks the globe; everything is kept">
        <label class="feature-switch"><Switch on={display.on} onChange={(on) => display.set(on).catch(() => false)} label="Display on" />
          <span class="muted">{display.on ? 'On' : 'Off: dark until you switch it on'}</span></label>
      </Row>
      <Row label="Night schedule" hint="it switches itself off and back on">
        <div class="row">
          <Switch on={scheduled} onChange={toggle} label="Night schedule" />
          <span>Off from</span>
          <input type="time" value={shown.off} onInput={setTime('off')} aria-label="Switch off at" />
          <span>until</span>
          <input type="time" value={shown.on} onInput={setTime('on')} aria-label="Switch back on at" />
        </div>
        <p class="muted small" style="margin:8px 0 0">
          {scheduled ? `On: dark from ${showTime(offAt)} to ${showTime(onAt)}, every day. ` : ''}Switched by hand in between, it stays that way until the next of the two times.
        </p>
      </Row>
    </section>
  );
}

/* ---------- where the globe is: weather, planes and sunrise all use it ---------- */

/** The globe's name on the network. The router adds its own domain (.lan, .home), so only the name part is kept. */
const HOST_RE = /^[a-z0-9]([a-z0-9-]{0,30}[a-z0-9])?$/;
function NetworkName({ settings, save }) {
  const now = settings.hostname || 'infoglobe';
  const [draft, setDraft] = useState(now);
  const typed = draft.trim().toLowerCase();
  const name = typed.split('.')[0], domain = typed.includes('.') ? typed.slice(name.length) : '';
  const ok = HOST_RE.test(name);
  const submit = (e) => {
    e.preventDefault();
    if (!ok || name === now) return;
    save({ hostname: name }, `Renamed to ${name}. The globe asks the router for its address again, under the new name`).then((done) => { if (done) setDraft(name); });
  };
  return (
    <Row label="Name on the network" hint="how computers and phones find it">
      <form class="row" onSubmit={submit}>
        <input type="text" class="host-name" value={draft} maxLength={40} spellcheck={false} autocomplete="off" aria-label="Name on the network"
          onInput={(e) => setDraft(e.currentTarget.value)} />
        <button class="btn small primary" type="submit" disabled={!ok || name === now}>Save</button>
      </form>
      <p class="muted small" style="margin:6px 0 0">
        {!ok ? 'Letters, digits and dashes only, up to 32.'
          : <>Open it as <a href={`http://${name}.local/designer`}>{name}.local</a>, or through your router as <b class="mono">{name}</b>
            {domain ? <> or <b class="mono">{name}{domain}</b></> : ' (often with .lan or .home after it)'}.
            {domain ? ` The ${domain} comes from your router, so the globe keeps just “${name}”.` : ''}</>}
      </p>
    </Row>
  );
}

function Place({ settings, save, onSettings, toast }) {
  const units = settings.units === 'c' ? 'c' : 'f';
  return (
    <section class="card" id="place">
      <div class="card-head"><h2>Where the globe is</h2></div>
      <Row label="Location" hint="for weather, planes and sunrise">
        <LocationPicker settings={settings} toast={toast} onSaved={onSettings} />
      </Row>
      <Row label="Units">
        <Choice label="Units" value={units} options={[['f', '°F and mph'], ['c', '°C and km/h']]} onPick={(v) => save({ units: v }, v === 'f' ? 'Fahrenheit and mph' : 'Celsius and km/h')} />
      </Row>
      <NetworkName settings={settings} save={save} />
    </section>
  );
}

/** "Change what it shows" and "Play it now" for a feed's scene, with the scene's messages as they read now. */
function FeedScene({ id, frames, label, toast }) {
  return (
    <Row label="What it shows">
      <ol class="recipe small">{frames.map((f, i) => <li key={i}><span class="mono">{fillValues(f.text, undefined, undefined, true)}</span></li>)}</ol>
      <div class="row" style="margin-top:8px">
        <a class="btn small" href={`#/scenes/${id}`}>Change what it shows</a>
        <button class="btn small" onClick={() => api.playScene(id, 0).then(() => toast(true, `Playing ${label} once`), (e) => toast(false, e.message))}>▶ Play it now</button>
      </div>
    </Row>
  );
}

/** The feed's values for your own messages ({var:temp}...), folded away. */
function FeedValues({ list, values, unitOf = () => '' }) {
  return (
    <details class="sub feed-values">
      <summary>Use these in your own messages</summary>
      <table class="values left">
        <tbody>
          {list.map((v) => (
            <tr key={v.name}><td><code>{`{var:${v.name}}`}</code></td><td class="mono">{values[v.name] ?? '–'}{values[v.name] !== undefined && unitOf(v.name) ? ` ${unitOf(v.name)}` : ''}</td><td class="muted small">{v.label.replace(/ \(.*\)$/, '')}</td></tr>
          ))}
        </tbody>
      </table>
      <p class="muted small">Or pick them from “+ Live” on any message.</p>
    </details>
  );
}

/* ---------- weather ---------- */

const UNIT = { temp: '°', feels: '°', hi: '°', lo: '°', thi: '°', tlo: '°', humid: '%', rain: '%' };

function WeatherCard({ settings, values, toast }) {
  const units = settings.units === 'c' ? 'c' : 'f';
  const status = values.wx_status, st = WX_STATUS[status];
  const unitOf = (name) => (name === 'wind' ? (units === 'c' ? 'km/h' : 'mph') : UNIT[name] === '°' ? `°${units.toUpperCase()}` : UNIT[name] || '');
  return (
    <section class="card" id="weather">
      <div class="card-head"><h2>Weather</h2><span class="muted small">switch it on or off on <a href="#/home">Home</a></span></div>
      {hasLocation(settings) && st && st.why && status !== 'OK' && status !== 'NO LOCATION' && <div class="problem">{st.why}</div>}
      {!hasLocation(settings) && <div class="problem">Set the location above and the weather starts.</div>}
      {values.weather && (
        <div class="now-box">
          <Mini frame={{ text: values.weather, after: 'hold', shift: -1 }} height={34} />
          <div>
            <b class="mono">{values.weather}</b>
            <span class="muted small">{values.hi !== undefined && `High ${values.hi}, low ${values.lo}. `}{values.wx_updated && `Updated ${ago(values.wx_updated)}, every 15 minutes.`}</span>
          </div>
        </div>
      )}
      <FeedScene id="weather" frames={weatherScene().frames} label="the weather" toast={toast} />
      <FeedValues list={WX_VALUES} values={values} unitOf={unitOf} />
      <p class="muted small" style="margin:10px 0 0">From Open-Meteo (free, no sign-up). It gets the globe’s location.</p>
    </section>
  );
}

/* ---------- planes ---------- */

const RANGES = [3, 5, 10, 15, 25];
// when a plane interrupts (feed_aircraft.c "aircraftNotice"): it looks about a minute along each plane's track
const NOTICE = [[0, 'Never'], [1, 'Right overhead'], [2, 'I’ll see or hear it'], [3, 'Anything in my sky']];
const NOTICE_HINT = ['', 'Only planes about to pass almost straight over the globe.',
  'Planes about to pass high enough in your sky to spot, or close enough to hear. It goes off about a minute before they get there.',
  'Also planes lower on the horizon or further off: more interruptions.'];
const CEILINGS = [[0, 'Any height'], [20000, 'Below 20,000 ft'], [10000, 'Below 10,000 ft'], [5000, 'Below 5,000 ft']];
const km = (nm) => Math.round(nm * 1.852);
const num = (v, d) => (Number.isFinite(v) ? v : d);
// "UAL123 2.4" + a Unix time, as the globe notes its last alert and its closest approach
const noted = (text, at) => {
  const m = /^(.+) (\d+(?:\.\d+)?)$/.exec(text || ''), t = Number(at);
  if (!m) return null;
  const when = t > 1e9 ? new Date(t * 1000) : null;
  const sameDay = when && when.toDateString() === new Date().toDateString();
  return { who: m[1], nm: m[2], when: when ? `${sameDay ? '' : `${when.toLocaleDateString([], { weekday: 'short' })} `}${when.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' })}` : '' };
};

function PlanesCard({ settings, values, save, toast }) {
  const on = settings.aircraft === true;
  const radius = num(settings.aircraftRadius, 10);
  const notice = Number.isFinite(settings.aircraftNotice) ? settings.aircraftNotice : settings.aircraftAlert === 0 ? 0 : 2;
  const alertFt = num(settings.aircraftAlertFt, 0);
  const lastAlert = noted(values.plane_alert, values.plane_alert_at), closest = noted(values.plane_closest, values.plane_close_at);
  const st = values.plane_status, n = parseInt(values.planes, 10);
  const toggle = async (next) => {
    try {
      if (next) await ensureScene('plane');          // so the overhead alert has something to play
      await save({ aircraft: next }, next ? 'Watching the sky' : 'Stopped watching for planes');
    } catch (e) { toast(false, e.message); }
  };
  return (
    <section class="card" id="planes">
      <div class="feature-head">
        <div>
          <h2>Planes</h2>
          <p class="muted small">Interrupts when a plane is about to fly over, like <span class="mono">UAL123 SFO-ORD B738</span>.</p>
        </div>
        <label class="feature-switch"><span>{on ? 'Watching' : 'Off'}</span><Switch on={on} onChange={toggle} label="Watch for planes" /></label>
      </div>
      {on && !hasLocation(settings) && <div class="problem">Set the location above and it starts looking.</div>}
      {on && st === 'OFFLINE' && <div class="problem">The flight trackers aren’t answering right now. The globe keeps trying.</div>}
      {on && st === 'OK' && (
        <div class="now-box plain">
          <div>
            <b class="mono">{values.planeline}</b>
            {n > 0 && <span class="small">{values.planetrip} · {values.planewhere}</span>}
            <span class="muted small">{n > 0 ? `${n} in the air within ${radius} nm (${km(radius)} km)` : `Nothing in the air within ${radius} nm right now`}</span>
          </div>
        </div>
      )}
      {on && <>
        <Row label="Interrupt for" hint="plays the plane scene once, then goes back">
          <Choice label="Interrupt for" value={notice} options={NOTICE} onPick={(v) => save({ aircraftNotice: v }, 'Saved')} />
          {notice > 0 && <p class="muted small" style="margin:8px 0 0">{NOTICE_HINT[notice]}{radius < 10 ? ' “Look within” 10 nm or more gives it time to see fast jets coming.' : ''}</p>}
          {notice > 0 && (
            <p class="muted small" style="margin:6px 0 0">
              {lastAlert ? <>Last time: <b class="mono">{lastAlert.who}</b>{lastAlert.when ? ` at ${lastAlert.when}` : ''}, {lastAlert.nm} nm away. </> : 'It hasn’t interrupted since the globe last started. '}
              {closest && <>Closest any plane has come: <b class="mono">{closest.who}</b>, {closest.nm} nm{closest.when ? ` (${closest.when})` : ''}.</>}
            </p>
          )}
        </Row>
        {notice > 0 && (
          <Row label="Only planes">
            <Choice label="Only planes" value={alertFt} options={CEILINGS} onPick={(v) => save({ aircraftAlertFt: v }, 'Saved')} />
          </Row>
        )}
        <Row label="Look within" hint="nautical miles around the globe">
          <Choice label="Look within" value={radius} options={RANGES.map((r) => [r, `${r} nm`])} onPick={(v) => save({ aircraftRadius: v }, 'Saved')} />
        </Row>
        <FeedScene id="plane" frames={PLANE_SCENE.frames} label="the plane scene" toast={toast} />
        <FeedValues list={AC_VALUES} values={values} />
        <p class="muted small" style="margin:10px 0 0">
          Aircraft from <a href="https://adsb.lol" target="_blank" rel="noopener noreferrer">adsb.lol</a> or <a href="https://adsb.fi" target="_blank" rel="noopener noreferrer">adsb.fi</a>,
          routes from <a href="https://www.adsbdb.com" target="_blank" rel="noopener noreferrer">adsbdb</a>. They get the globe’s location, rounded to about 1 km.
        </p>
      </>}
    </section>
  );
}

/* ---------- the version, and what changed in each one ---------- */

const Inline = ({ text }) => inline(text).map((p, i) => (p.b ? <b key={i}>{p.text}</b> : p.code ? <code key={i}>{p.text}</code> : p.text));

function Notes({ blocks }) {
  const out = [];
  let list = null;
  blocks.forEach((b, i) => {
    if (b.kind === 'li') { if (!list) { list = []; out.push(<ul key={i}>{list}</ul>); } list.push(<li key={i}><Inline text={b.text} /></li>); return; }
    list = null;
    out.push(b.kind === 'h' ? <h4 key={i}>{b.text}</h4> : <p key={i}><Inline text={b.text} /></p>);
  });
  return <div class="notes">{out}</div>;
}

function About() {
  const [fw, setFw] = useState(null);                 // {version, built}; false = older firmware without a version
  useEffect(() => { api.getVersion().then(setFw, () => setFw(false)); }, []);
  const [latest, ...older] = RELEASES;
  return (
    <section class="card" id="about">
      <div class="card-head"><h2>About</h2><span class="pill scene">v{VERSION}</span></div>
      <p class="muted small" style="margin:0 0 4px">
        This page is version <b>{VERSION}</b>.{' '}
        {fw ? (fw.version === VERSION ? `The globe’s firmware is the same version, built ${fw.built}.` : `The globe’s firmware is ${fw.version}: flash the globe to bring them in line.`)
          : fw === false ? 'The globe’s firmware is from before versions were numbered.' : ''}
      </p>
      {latest && (
        <div class="release">
          <h3>What’s new in {latest.version}{latest.date ? <span class="muted small"> · {latest.date}</span> : null}</h3>
          <Notes blocks={latest.blocks} />
        </div>
      )}
      {older.length > 0 && (
        <details class="sub releases">
          <summary>Earlier versions</summary>
          {older.map((r) => (
            <div class="release" key={r.version}>
              <h3>{r.version}{r.date ? <span class="muted small"> · {r.date}</span> : null}</h3>
              <Notes blocks={r.blocks} />
            </div>
          ))}
        </details>
      )}
    </section>
  );
}

/* ---------- backup and restore ---------- */

function Backup({ toast }) {
  const [busy, setBusy] = useState('');
  const file = useRef(null);
  const save = async () => {
    setBusy('Reading the globe…');
    try {
      const b = await makeBackup();
      download(`infoglobe-backup-${new Date().toISOString().slice(0, 10)}.json`, b);
      toast(true, `Backup saved: ${Object.keys(b.scenes).length} scenes, the rotation and the settings`);
    } catch (e) { toast(false, `Could not make the backup: ${e.message}`); }
    setBusy('');
  };
  const restore = async (f) => {
    try {
      const b = JSON.parse(await f.text());
      const { scenes } = checkBackup(b);
      const when = b.made ? ` from ${new Date(b.made).toLocaleString()}` : '';
      if (!confirm(`Restore this backup${when}? Its ${Object.keys(scenes).length} scenes, the rotation and the settings replace what the globe has. Scenes that aren't in the backup are kept.`)) return;
      const r = await restoreBackup(b, setBusy);
      toast(true, `Restored ${r.scenes} scenes${r.playlist ? ', the rotation' : ''}${r.settings ? ' and the settings' : ''}. Reloading…`);
      setTimeout(() => location.reload(), 1500);
    } catch (e) { toast(false, `Could not restore: ${e.message}`); }
    setBusy('');
  };
  return (
    <Row label="Backup" hint="your scenes, the rotation and the settings, in one file">
      <div class="row">
        <button class="btn small" onClick={save} disabled={!!busy}>Download a backup</button>
        <button class="btn small" onClick={() => file.current.click()} disabled={!!busy}>Restore from a file…</button>
        {busy && <span class="muted small">{busy}</span>}
      </div>
      <input ref={file} type="file" accept=".json,application/json" hidden onChange={(e) => { const f = e.currentTarget.files[0]; if (f) restore(f); e.currentTarget.value = ''; }} />
      <p class="muted small" style="margin:6px 0 0">The access token isn’t in it. Worth doing before you change much, or to set up a second globe.</p>
    </Row>
  );
}

/* ---------- rarely needed: folded away ---------- */

const PACES = [[0, 'Fastest'], [150, 'Normal'], [300, 'Slow'], [600, 'Very slow']];

function More({ settings, save, toast }) {
  const pace = Number.isFinite(settings.animMinMs) ? settings.animMinMs : 0;
  const paceValue = PACES.reduce((best, [v]) => (Math.abs(v - pace) < Math.abs(best - pace) ? v : best), 0);
  const host = api.GLOBE || location.origin;
  return (
    <details class="card more" id="more">
      <summary>More: backup, animation pace, access token, scripts and firmware</summary>
      <Backup toast={toast} />
      <Row label="Animation pace" hint="how fast animations like split-flap step">
        <Choice label="Animation pace" value={paceValue} options={PACES} onPick={(v) => save({ animMinMs: v }, 'Animation pace saved')} />
      </Row>
      <Row label="From scripts" hint="home automation, a phone shortcut">
        <pre class="bytes">{`curl -X POST ${host}/api/v1/timer -d '{"seconds":300,"label":"TEA"}'
curl -X POST ${host}/api/v1/values -d '{"cpu":42}'`}</pre>
        <p class="muted small" style="margin:6px 0 0">A pushed value shows in any message as <code>{'{var:cpu}'}</code>. The whole API is in <code>docs/api.md</code>.</p>
      </Row>
      <Row label="Updating" hint="over the network, no USB needed">
        <pre class="bytes">node tools/deploy.mjs</pre>
        <p class="muted small" style="margin:6px 0 0">
          From the project on your computer, after <code>idf.py build</code>: it sends the new firmware and this page,
          waits for the globe to restart and checks the version. A firmware that can’t get back on the network is
          undone by the globe itself. The globe is also at <a href="http://infoglobe.local/designer">infoglobe.local</a>.
          {' '}<a href={`${api.GLOBE}/`}>The classic page</a> has the protocol lab and a plain firmware upload.
        </p>
      </Row>
      <AccessToken toast={toast} />
    </details>
  );
}

export function Settings({ toast, display, section }) {
  const [settings, setSettings] = useState(null);
  const [values, setValues] = useState({});
  useEffect(() => {
    api.getSettings().then((s) => { setSettings(s); if (hasLocation(s)) setLocation({ lat: s.lat, lon: s.lon }); }, () => setSettings({}));
    return api.subscribeValues((v) => { setValues(v || {}); setVars(v); });
  }, []);
  // #/settings/planes and the like: open on that card
  useEffect(() => {
    if (!settings || !section) return;
    const el = document.getElementById(section);
    if (el) { if (el.tagName === 'DETAILS') el.open = true; el.scrollIntoView({ block: 'start' }); }
  }, [settings, section]);
  const onSettings = (patch) => setSettings((s) => ({ ...s, ...patch }));
  const save = async (patch, msg) => {
    try { await api.saveSettings(patch); onSettings(patch); if (msg) toast(true, msg); return true; }
    catch (e) { toast(false, `Could not save: ${e.message}`); return false; }
  };
  if (!settings) return <div class="page"><section class="card"><p class="muted">Loading…</p></section></div>;
  return (
    <div class="page settings">
      <Display settings={settings} save={save} display={display} />
      <Place settings={settings} save={save} onSettings={onSettings} toast={toast} />
      <WeatherCard settings={settings} values={values} toast={toast} />
      <PlanesCard settings={settings} values={values} save={save} toast={toast} />
      <More settings={settings} save={save} toast={toast} />
      <About />
    </div>
  );
}
