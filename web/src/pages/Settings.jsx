import { useEffect, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { TOKEN_RE, TOKEN_RULES, generateToken } from '../lib/request.js';
import { Choice, Row, LocationPicker } from '../components/Common.jsx';

// Settings: the globe's access token for now; other settings can join as cards.

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
    <section class="card">
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

const PACES = [[0, 'Fastest'], [150, 'Normal'], [300, 'Slow'], [600, 'Very slow']];

function Globe({ toast }) {
  const [settings, setSettings] = useState(null);
  useEffect(() => { api.getSettings().then(setSettings, () => setSettings({})); }, []);
  const save = async (patch, msg) => {
    try { await api.saveSettings(patch); setSettings((s) => ({ ...s, ...patch })); toast(true, msg); }
    catch (e) { toast(false, `Could not save: ${e.message}`); }
  };
  const units = settings && settings.units === 'c' ? 'c' : 'f';
  const pace = settings && Number.isFinite(settings.animMinMs) ? settings.animMinMs : 0;
  const paceValue = PACES.reduce((best, [v]) => (Math.abs(v - pace) < Math.abs(best - pace) ? v : best), 0);
  return (
    <>
      <section class="card">
        <div class="card-head"><h2>The globe</h2></div>
        <Row label="Location" hint="for weather, planes and sunrise">
          <LocationPicker settings={settings} toast={toast} onSaved={(p) => setSettings((s) => ({ ...s, ...p }))} />
        </Row>
        <Row label="Units">
          <Choice label="Units" value={units} options={[['f', '°F and mph'], ['c', '°C and km/h']]} onPick={(v) => save({ units: v }, v === 'f' ? 'Fahrenheit and mph' : 'Celsius and km/h')} />
        </Row>
      </section>
      <section class="card">
        <div class="card-head"><h2>Speed and brightness</h2></div>
        <Row label="Animation pace" hint="how fast custom animations step">
          <Choice label="Animation pace" value={paceValue} options={PACES} onPick={(v) => save({ animMinMs: v }, 'Animation pace saved')} />
        </Row>
        <Row label="Time on screen">
          <span class="muted">Set per item on <a href="#/home">Home</a>: the seconds box beside each thing that is switched on.</span>
        </Row>
        <Row label="Brightness and spin speed">
          <span class="muted">Fixed inside the globe. None of the commands it accepts changes them, so there is nothing to set.</span>
        </Row>
      </section>
    </>
  );
}

export function Settings({ toast }) {
  return (
    <div class="page">
      <Globe toast={toast} />
      <AccessToken toast={toast} />
    </div>
  );
}
