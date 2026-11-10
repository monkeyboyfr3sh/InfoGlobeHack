import { useEffect, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { TOKEN_RE, TOKEN_RULES, generateToken } from '../lib/request.js';

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
      <p class="hint">
        With a token set, the globe only accepts changes that carry it: showing or clearing text, scenes, the playlist,
        settings, pushed values, the Control &amp; Explorer page and firmware updates. Looking stays open: the
        dashboard, what's showing, values and the live view work without it. The token is kept on the globe
        (not in exported settings) and in this browser.
      </p>
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

      <h3 class="token-sub">Scripts and webhooks</h3>
      <p class="muted small">Send it in a header, or in the URL when the sender can't set headers (Home Assistant, Uptime Kuma, ...):</p>
      <pre class="bytes">{`curl -X POST ${host}/api/v1/values -H "Authorization: Bearer ${shown}" -d '{"cpu":42}'
curl -X POST "${host}/api/v1/values?token=${shown}" -d '{"cpu":42}'`}</pre>
      <p class="muted small">A token in a URL can end up in the sender's logs and history; use a header where you can.</p>

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

export function Settings({ toast }) {
  return (
    <div class="settings-page">
      <AccessToken toast={toast} />
    </div>
  );
}
