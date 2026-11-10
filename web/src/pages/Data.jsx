import { useEffect, useState } from 'preact/hooks';
import * as api from '../lib/api.js';
import { setVars } from '../lib/values.js';
import { WeatherCard } from '../components/WeatherCard.jsx';

// Data: what the globe knows beyond the clock. Pushed values first; the
// feeds (weather, aircraft) add their own cards here.

const KEY_RE = /^[a-z0-9_]{1,15}$/;

function PushedValues({ toast }) {
  const [values, setValues] = useState(null);
  const [key, setKey] = useState('');
  const [text, setText] = useState('');
  const host = api.GLOBE || location.origin;

  const load = async () => {
    try { const v = await api.getValues(); setValues(v); setVars(v); } catch { /* offline: the status chip says so */ }
  };
  useEffect(() => {
    load();
    const t = setInterval(load, 3000);
    return () => clearInterval(t);
  }, []);

  const save = async (e) => {
    e.preventDefault();
    if (!KEY_RE.test(key)) { toast(false, 'Names use a-z, 0-9 and _ (up to 15 characters), e.g. cpu'); return; }
    try { await api.setValues({ [key]: text }); setKey(''); setText(''); await load(); toast(true, `Set ${key}. Show it with {${key}}`); }
    catch (err) { toast(false, err.message); }
  };
  const remove = async (k) => {
    try { await api.setValues({ [k]: null }); await load(); } catch (err) { toast(false, err.message); }
  };

  const names = values ? Object.keys(values).sort() : [];
  return (
    <section class="card">
      <div class="card-head"><h2>Pushed values</h2></div>
      <p class="muted small">
        Anything on your network can hand the globe a value; write <code>{'{name}'}</code> in a frame's text to show it,
        or <code>{'{bar:name}'}</code> for a 0-100 progress bar. The frame updates when the value changes.
      </p>
      {names.length === 0
        ? <p class="muted">{values ? 'Nothing has been pushed yet.' : 'Loading…'}</p>
        : (
          <table class="values">
            <thead><tr><th>Name</th><th>Value</th><th>Use in text</th><th /></tr></thead>
            <tbody>
              {names.map((k) => (
                <tr key={k}>
                  <td><b>{k}</b></td>
                  <td>{values[k]}</td>
                  <td><code>{`{${k}}`}</code></td>
                  <td><button class="btn small" onClick={() => remove(k)} aria-label={`Remove ${k}`}>Remove</button></td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      <form class="row" onSubmit={save} style="margin-top:12px">
        <input type="text" value={key} onInput={(e) => setKey(e.currentTarget.value)} placeholder="name" aria-label="Value name" style="width:130px" />
        <input type="text" value={text} maxLength={47} onInput={(e) => setText(e.currentTarget.value)} placeholder="value" aria-label="Value" style="width:200px" />
        <button class="btn primary" type="submit">Set</button>
      </form>
      <div class="card-foot">
        <span class="muted small">From a script: <code>{`curl -X POST ${host}/api/v1/values -d '{"cpu":42}'`}</code></span>
      </div>
    </section>
  );
}

export function Data({ toast }) {
  return (
    <div class="data-page">
      <PushedValues toast={toast} />
      <WeatherCard toast={toast} />
    </div>
  );
}
