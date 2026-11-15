#!/usr/bin/env node
// Update the globe over the network: the firmware, then the web app, then a check
// that the globe came back with the version in VERSION. Nothing needs to be
// plugged into this computer. Build first (idf.py build in hello_world/; it also
// rebuilds the web app when web/src changed).
//
//   node tools/deploy.mjs                       the globe at http://infoglobe.local
//   node tools/deploy.mjs --globe infoglobe.lan   by the router's name for it (Settings, name on the network)
//   node tools/deploy.mjs --globe 192.168.0.71  another address
//   node tools/deploy.mjs --web-only            just the web app (no restart)
//   node tools/deploy.mjs --firmware-only
//   --token <token>, or INFOGLOBE_TOKEN, when the globe has an access token set.
//   INFOGLOBE_GLOBE sets the default address.
//
// The firmware goes first: a globe on firmware from before network web app
// updates learns them from it. A new firmware is on probation until it is back
// on the network (http_server_task.c); if it isn't, the globe goes back by itself.

import { readFileSync, statSync } from 'node:fs';
import { createSocket } from 'node:dgram';
import { networkInterfaces } from 'node:os';

/* "infoglobe.local" -> its addresses, asked on every network adapter here.
 * Windows' own lookup asks on one adapter only, and a VPN adapter can be that
 * one; asking ourselves finds the globe wherever it is on a local network. */
function mdnsLookup(name, ms = 2500) {
  const qname = Buffer.concat([...name.split('.').map((p) => Buffer.concat([Buffer.from([p.length]), Buffer.from(p)])), Buffer.from([0])]);
  const query = Buffer.concat([Buffer.from([0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]), qname, Buffer.from([0, 1, 0x80, 1])]);
  const found = new Set();
  const skip = (b, i) => { for (;;) { const l = b[i]; if (l === 0) return i + 1; if ((l & 0xc0) === 0xc0) return i + 2; i += l + 1; } };
  const read = (b) => {
    try {
      let i = 12;
      for (let q = b.readUInt16BE(4); q > 0; q--) i = skip(b, i) + 4;
      for (let n = b.readUInt16BE(6) + b.readUInt16BE(8) + b.readUInt16BE(10); n > 0; n--) {
        i = skip(b, i);
        const type = b.readUInt16BE(i), len = b.readUInt16BE(i + 8);
        if (type === 1 && len === 4) found.add([...b.subarray(i + 10, i + 14)].join('.'));
        i += 10 + len;
      }
    } catch { /* not a reply we understand */ }
  };
  const ours = Object.values(networkInterfaces()).flat().filter((a) => a && a.family === 'IPv4' && !a.internal).map((a) => a.address);
  return new Promise((done) => {
    const socks = ours.map((addr) => {
      const s = createSocket({ type: 'udp4', reuseAddr: true });
      s.on('message', read);
      s.on('error', () => s.close());
      s.bind(0, addr, () => { try { s.setMulticastInterface(addr); s.send(query, 5353, '224.0.0.251'); } catch { /* adapter without multicast */ } });
      return s;
    });
    setTimeout(() => { socks.forEach((s) => { try { s.close(); } catch { /* closed */ } }); done([...found]); }, ms);
  });
}

const root = new URL('..', import.meta.url);
const arg = (name) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : undefined; };
const has = (name) => process.argv.includes(name);
let globe = arg('--globe') || process.env.INFOGLOBE_GLOBE || 'http://infoglobe.local';
if (!/^https?:\/\//.test(globe)) globe = `http://${globe}`;
globe = globe.replace(/\/$/, '');
const token = arg('--token') || process.env.INFOGLOBE_TOKEN || '';
const doFirmware = !has('--web-only'), doWeb = !has('--firmware-only');
const want = readFileSync(new URL('VERSION', root), 'utf8').trim();
const files = {
  firmware: new URL('hello_world/build/hello_world.bin', root),
  web: new URL('hello_world/main/data/designer.html.gz', root),
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const say = (s) => console.log(s);
const fail = (s) => { console.error(`\n✗ ${s}`); process.exit(1); };
const auth = token ? { Authorization: `Bearer ${token}` } : {};

async function version() {
  try {
    const r = await fetch(`${globe}/api/v1/version`, { signal: AbortSignal.timeout(4000) });
    return r.ok ? await r.json() : { version: '(before versions)' };
  } catch { return null; }
}

async function upload(path, file, what) {
  const body = readFileSync(file);
  say(`→ ${what}: ${(body.length / 1024).toFixed(0)} KB…`);
  let r;
  try {
    r = await fetch(`${globe}${path}`, { method: 'POST', headers: { 'Content-Type': 'application/octet-stream', ...auth }, body, signal: AbortSignal.timeout(180000) });
  } catch (e) { fail(`${what}: no answer from ${globe} (${e.cause ? e.cause.code || e.cause.message : e.message})`); }
  const text = await r.text();
  if (r.status === 401) fail(`${what}: the globe has an access token. Pass --token or set INFOGLOBE_TOKEN.`);
  if (r.status === 404) return false;
  if (!r.ok) fail(`${what}: ${r.status} ${text.slice(0, 200)}`);
  return true;
}

// A .local name: look it up ourselves, and use the first address that answers.
const host = new URL(globe).hostname;
if (host.endsWith('.local')) {
  const addrs = await mdnsLookup(host);
  if (!addrs.length) fail(`Nothing answered for ${host} on this computer's networks. Is the globe on? Try --globe <its address>.`);
  for (const a of addrs) {
    globe = `http://${a}`;
    if (await version()) break;
  }
  say(`${host} is ${addrs.join(' and ')}`);
}

const before = await version();
if (!before) fail(`No globe answering at ${globe}. Is it on the network? Try --globe <its address>.`);
say(`Globe at ${globe}: ${before.version} now, sending ${want}.`);

for (const [k, f] of Object.entries(files)) {
  if ((k === 'firmware' && !doFirmware) || (k === 'web' && !doWeb)) continue;
  try { const st = statSync(f); say(`  ${k}: built ${st.mtime.toLocaleString()}`); } catch { fail(`No ${k} build at ${f.pathname}. Run idf.py build in hello_world/ first.`); }
}

if (doFirmware) {
  await upload('/ota', files.firmware, 'Firmware');
  say('  sent; the globe is restarting…');
  for (let i = 0; i < 15 && (await version()); i++) await sleep(1000);        // gone…
  let now = null;
  for (let i = 0; i < 45 && !now; i++) { await sleep(2000); now = await version(); }   // …and back
  if (!now) fail('The globe didn\'t come back within 90 s. If the new firmware can\'t get on the network it goes back to the old one by itself within about 3 minutes.');
  if (now.version !== want) fail(`The globe came back on ${now.version}, not ${want}. Was hello_world built after VERSION changed?`);
  say(`✓ Firmware ${now.version} running (built ${now.built}). Waiting for the globe to keep it…`);
  if (now.state) {
    for (let i = 0; i < 40 && now && now.state === 'probation'; i++) { await sleep(2000); now = await version(); }
    if (!now) fail('The globe went away while the new firmware was on probation: it may be going back to the one before.');
    say(now.state === 'probation' ? '  still on probation after 80 s; it goes back by itself if it cannot stay on the network.' : `✓ Kept (${now.slot}).`);
  }
}

if (doWeb) {
  if (!(await upload('/api/v1/webapp', files.web, 'Web app'))) fail('This globe\'s firmware can\'t take the web app over the network yet: run without --web-only to update the firmware first.');
  const page = await (await fetch(`${globe}/designer`)).text().catch(() => '');
  say(`✓ Web app in place${page ? '' : ' (couldn\'t read it back)'}. Open ${globe}/designer`);
}
