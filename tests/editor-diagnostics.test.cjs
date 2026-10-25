// Run: node --test tests/editor-diagnostics.test.cjs
// JSDOM_PATH may point to an isolated installation of jsdom; no production dependency.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { TextEncoder } = require('node:util');
const { JSDOM, VirtualConsole } = require(process.env.JSDOM_PATH || 'jsdom');
const assets = path.resolve(__dirname, '../natware/src/features/editor/assets');
const html = fs.readFileSync(path.join(assets, 'editor.html'), 'utf8').replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi, '');
function editor(options = {}) {
  const browserErrors = [], sent = [], downloads = [], timeouts = new Map();
  let clock = 0, timerId = 0, onHostMessage = () => {};
  const console = new VirtualConsole();
  console.on('jsdomError', e => browserErrors.push(e));
  const dom = new JSDOM(html, { url: 'https://natware.local/editor.html', runScripts: 'outside-only', pretendToBeVisual: true, virtualConsole: console });
  const w = dom.window;
  w.TextEncoder = TextEncoder;
  w.Glass = { scan() {} };
  w.matchMedia = () => ({ matches: true });
  w.IntersectionObserver = class { observe() {} disconnect() {} };
  w.CSS = { escape: value => String(value).replace(/["\\]/g, '\\$&') };
  w.HTMLDialogElement.prototype.showModal = function () { this.open = true; };
  w.HTMLDialogElement.prototype.close = function () { this.open = false; this.dispatchEvent(new w.Event('close')); };
  w.HTMLAnchorElement.prototype.click = function () { downloads.push({ name: this.download, href: this.href }); };
  w.URL.createObjectURL = () => 'blob:diagnostics-test'; w.URL.revokeObjectURL = () => {};
  Object.defineProperty(w.performance, 'now', { value: () => clock });
  w.setTimeout = (callback, delay = 0) => { const id = ++timerId; timeouts.set(id, { callback, due: clock + delay }); return id; };
  w.clearTimeout = id => timeouts.delete(id);
  w.setInterval = (callback, delay) => { const id = ++timerId; timeouts.set(id, { callback, due: clock + delay, interval: delay }); return id; };
  w.clearInterval = w.clearTimeout;
  w.requestAnimationFrame = callback => w.setTimeout(callback, 1);
  w.fetch = options.fetch || (async () => { throw new Error('Test network unavailable'); });
  if (options.history !== undefined) w.localStorage.setItem('natware.workspace.diagnostics.v1', options.history);
  if (options.workspace !== undefined) w.localStorage.setItem('natware.workspace.v2', options.workspace);
  if (options.host) w.chrome = { webview: {
    addEventListener(type, fn) { if (type === 'message') onHostMessage = fn; },
    postMessage(raw) {
      if (options.sendError) throw new Error('Test host send failure');
      const message = JSON.parse(raw); sent.push(message);
      options.responder?.(message, value => onHostMessage({ data: value }));
    },
  } };
  w.eval(fs.readFileSync(path.join(assets, 'diagnostics.js'), 'utf8'));
  w.eval(fs.readFileSync(path.join(assets, 'workspace.js'), 'utf8'));
  const find = selector => w.document.querySelector(selector);
  const events = () => w.NatwareDiagnostics.snapshot().records;
  const advance = milliseconds => {
    const stop = clock + milliseconds;
    for (let i = 0; i < 10000; ++i) {
      const next = [...timeouts].filter(([, t]) => t.due <= stop).sort((a, b) => a[1].due - b[1].due)[0];
      if (!next) break;
      const [id, t] = next; clock = t.due;
      timeouts.delete(id); if (t.interval) timeouts.set(id, { ...t, due: clock + t.interval });
      t.callback();
    }
    clock = stop;
  };
  return { w, find, events, sent, downloads, browserErrors, advance,
    receive: message => onHostMessage({ data: message }),
    ready(connected = true) { onHostMessage({ data: { type: 'editor:ready', build: 'test-host' } }); onHostMessage({ data: { type: 'nexec', connected } }); },
    input(source) { find('#fallback-editor').value = source; find('#fallback-editor').dispatchEvent(new w.Event('input')); },
    close() { w.dispatchEvent(new w.Event('pagehide')); w.close(); },
  };
}

test('invalid stored diagnostics cannot stop editor startup', () => {
  for (const value of ['null', '{}', '"string"', 'broken json', '[null,42,{"text":"old record"}]']) {
    const e = editor({ history: value });
    assert.match(e.find('[data-log="full"]').textContent, /Workspace ready/);
    assert.equal(e.browserErrors.length, 0); e.close();
  }
});
test('no host rejects execute without generating script output', () => {
  const e = editor(); e.input('print("friendly")'); e.find('#execute').click();
  assert.match(e.find('#connection-status').textContent, /unavailable/);
  assert.ok(e.events().some(r => r.event === 'execute.unavailable'));
  assert.ok(!e.events().some(r => r.text.includes('friendly') || /Executed|Preview complete/.test(r.text)));
  assert.equal(e.find('#attach').disabled, true); e.close();
});
test('host exists but absent pipe still rejects execute', () => {
  const e = editor({ host: true }); e.ready(false); e.find('#execute').click();
  assert.equal(e.sent.filter(m => m.type === 'nexec:execute').length, 0); e.close();
});
test('execute waits for its own response and pipe reply never becomes execution success', () => {
  const e = editor({ host: true }); e.ready(); e.input('print("sensitive-unique-content")');
  e.find('#execute').click(); e.find('#execute').click();
  const requests = e.sent.filter(m => m.type === 'nexec:execute');
  assert.equal(requests.length, 1); assert.equal(e.find('#execute').disabled, true);
  e.receive({ type: 'nexec:executed', requestId: 'unrelated', ok: true });
  assert.equal(e.find('#execute').disabled, true);
  e.receive({ type: 'nexec:executed', requestId: requests[0].requestId, ok: true, name: 'Script.lua', state: 'pipe_replied' });
  assert.equal(e.find('#execute').disabled, false);
  assert.ok(e.events().some(r => r.event === 'execute.unconfirmed' && r.level === 'warning'));
  assert.ok(!JSON.stringify(e.events()).includes('sensitive-unique-content')); e.close();
});
test('execute failure reports the actual host error', () => {
  const e = editor({ host: true }); e.ready(); e.find('#execute').click();
  const request = e.sent.find(m => m.type === 'nexec:execute');
  e.receive({ type: 'nexec:executed', requestId: request.requestId, ok: false, error: 'Pipe closed during request' });
  assert.match(e.find('[data-log="full"]').textContent, /Pipe closed during request/); e.close();
});
test('timeout leaves outcome unknown and late response cannot finish a newer request', () => {
  const e = editor({ host: true }); e.ready(); e.find('#execute').click();
  const first = e.sent.find(m => m.type === 'nexec:execute'); e.advance(15001);
  assert.ok(e.events().some(r => r.event === 'request.timeout' && /unknown/.test(r.text)));
  e.ready(); e.find('#execute').click();
  e.receive({ type: 'nexec:executed', requestId: first.requestId, ok: true });
  assert.equal(e.find('#execute').disabled, true); e.close();
});
test('connection polling cannot replace a pending attach label', () => {
  const e = editor({ host: true }); e.ready(false); e.find('#attach').click();
  const request = e.sent.find(m => m.type === 'nexec:attach');
  e.receive({ type: 'nexec', connected: false });
  assert.match(e.find('#attach').textContent, /Attaching/); assert.equal(e.find('#attach').disabled, true);
  e.receive({ type: 'nexec:injected', requestId: request.requestId, ok: false, error: 'Connection refused' });
  assert.equal(e.find('#attach').disabled, false);
  assert.match(e.find('#attach').textContent, /Attach/); e.close();
});
test('a synchronous attach response is correlated correctly', () => {
  const e = editor({ host: true, responder(m, reply) { if (m.type === 'nexec:attach') reply({ type: 'nexec', connected: true, requestId: m.requestId }); } });
  e.ready(false); e.find('#attach').click(); assert.equal(e.find('#attach').disabled, false);
  assert.match(e.find('#attach').textContent, /Pipe detected/); e.close();
});
test('missing heartbeat response invalidates stale connection status', () => {
  const e = editor({ host: true }); e.ready(); e.advance(15001);
  assert.match(e.find('#connection-status').textContent, /Waiting for editor host/);
  assert.ok(e.events().some(r => r.event === 'connection.host_unresponsive')); e.close();
});
test('postMessage exceptions cannot crash editor controls', () => {
  const e = editor({ host: true, sendError: true }); e.ready(); e.find('#execute').click();
  assert.equal(e.find('#execute').disabled, false);
  assert.ok(e.events().some(r => r.event === 'host.send_failed')); assert.equal(e.browserErrors.length, 0); e.close();
});
test('malformed connected value cannot turn into pipe success', () => {
  const e = editor({ host: true }); e.ready(false); e.receive({ type: 'nexec', connected: 'false' });
  assert.match(e.find('#connection-status').textContent, /No backend pipe/); e.close();
});
test('diagnostics are batched, bounded, exportable, and redact sensitive fields', () => {
  const e = editor();
  e.w.NatwareDiagnostics.record('error', 'test.secret', 'token=abcd Bearer hidden', { source: 'private script', token: 'private token' });
  assert.equal(e.w.localStorage.getItem('natware.workspace.diagnostics.v1'), null);
  for (let i = 0; i < 2100; i++) e.w.NatwareDiagnostics.record('debug', 'test.volume', 'x'.repeat(1024));
  e.advance(601);
  const saved = e.w.localStorage.getItem('natware.workspace.diagnostics.v1');
  assert.ok(Buffer.byteLength(saved) <= 768 * 1024); assert.ok(e.events().length <= 2000);
  e.w.NatwareDiagnostics.record('error', 'test.secret', 'token=abcd Bearer hidden', { source: 'private script', token: 'private token' });
  const report = JSON.stringify(e.w.NatwareDiagnostics.snapshot());
  assert.ok(!report.includes('private script') && !report.includes('private token') && !report.includes('Bearer hidden'));
  e.find('#export-diagnostics').click(); assert.match(e.downloads[0].name, /^natware-diagnostics-/); e.close();
});
test('clearing visible console preserves diagnostic history', () => {
  const e = editor(); const before = e.events().length; e.find('[data-clear-console]').click();
  assert.ok(e.events().length > before); assert.match(e.find('[data-log="full"]').textContent, /Nothing logged/); e.close();
});
test('global JavaScript failures reach diagnostics and visible console', () => {
  const e = editor(); e.w.dispatchEvent(new e.w.ErrorEvent('error', { message: 'Test uncaught failure', filename: 'workspace.js', lineno: 42 }));
  assert.ok(e.events().some(r => r.event === 'javascript.error'));
  assert.match(e.find('[data-log="full"]').textContent, /Test uncaught failure/); e.close();
});
test('workspace quota failures never report hub saved', () => {
  const e = editor(); const original = e.w.Storage.prototype.setItem;
  e.w.Storage.prototype.setItem = function (key, value) { if (key === 'natware.workspace.v2') throw new Error('Quota exceeded'); return original.call(this, key, value); };
  e.find('#save-hub').click(); e.find('#script-name').value = 'QuotaTest';
  e.find('#save-form').dispatchEvent(new e.w.Event('submit', { cancelable: true }));
  assert.ok(e.events().some(r => r.event === 'hub.save_failed'));
  assert.ok(!e.events().some(r => r.event === 'hub.saved'));
  assert.equal(e.find('#save-dialog').open, true); assert.equal(e.w.natwareFlush(), false); e.close();
});
test('corrupt workspace has a recovery copy and editor remains usable', () => {
  const e = editor({ workspace: '{broken' });
  assert.equal(e.w.localStorage.getItem('natware.workspace.v2.recovery'), '{broken');
  assert.ok(e.events().some(r => r.event === 'storage.load_failed')); assert.equal(e.browserErrors.length, 0); e.close();
});
test('download fallback reports a request, never a confirmed file save', () => {
  const e = editor(); e.find('#save-file').click();
  assert.ok(e.events().some(r => r.event === 'file.download_requested'));
  assert.ok(!e.events().some(r => /^Saved /.test(r.text))); e.close();
});
test('native save failure does not silently turn into a fake download success', async () => {
  const e = editor(); e.w.showSaveFilePicker = async () => { throw new Error('Disk unavailable'); };
  e.find('#save-file').click(); await new Promise(resolve => setImmediate(resolve));
  assert.ok(e.events().some(r => r.event === 'file.save_failed')); assert.equal(e.downloads.length, 0); e.close();
});
test('closing with an in-flight request records the unknown outcome', () => {
  const e = editor({ host: true }); e.ready(); e.find('#execute').click(); e.w.dispatchEvent(new e.w.Event('pagehide'));
  assert.ok(e.events().some(r => r.event === 'request.abandoned'));
  assert.ok(JSON.parse(e.w.localStorage.getItem('natware.workspace.diagnostics.v1')).some(r => r.event === 'request.abandoned')); e.close();
});
