// Editor diagnostics are independent of workspace rendering so startup errors are retained.
(function (root) {
  'use strict';
  const key = 'natware.workspace.diagnostics.v1';
  const maxRecords = 2000, maxBytes = 768 * 1024;
  const started = performance.now();
  const sessionId = crypto.randomUUID();
  let sequence = 0, timer = 0, nativeTimer = 0, storageError = '', handlingError = false;
  let records = [], nativeQueue = [];
  const host = root.chrome?.webview;
  const levels = new Set(['debug', 'info', 'success', 'warning', 'error']);
  function clean(value, depth = 0) {
    if (depth > 3) return '[depth limit]';
    if (typeof value === 'string') return value.slice(0, 4096)
      .replace(/(Bearer\s+)[^\s"']+/gi, '$1[redacted]')
      .replace(/((?:token|password|secret|api[_-]?key)\s*[=:]\s*)[^\s&,"']+/gi, '$1[redacted]');
    if (value == null || typeof value === 'boolean' || typeof value === 'number') return value;
    if (Array.isArray(value)) return value.slice(0, 20).map(v => clean(v, depth + 1));
    if (typeof value === 'object') {
      const out = {};
      for (const k of Object.keys(value).slice(0, 24)) {
        out[k] = /^(source|script|authorization|cookie|password|secret|token|apiKey)$/i.test(k)
          ? '[redacted]' : clean(value[k], depth + 1);
      }
      return out;
    }
    return String(value).slice(0, 4096);
  }
  function errorText(error) {
    try { return String(error?.stack || error?.message || error || 'Unknown error').slice(0, 4096); }
    catch { return 'Error could not be serialized'; }
  }
  try {
    const stored = JSON.parse(localStorage.getItem(key) || '[]');
    if (!Array.isArray(stored)) throw new Error('Diagnostic history must be an array');
    records = stored.filter(r => r && typeof r === 'object' && typeof r.text === 'string')
      .slice(-maxRecords).map(r => clean(r));
  } catch (error) { storageError = errorText(error); }
  function flushNative() {
    clearTimeout(nativeTimer); nativeTimer = 0;
    if (!host || !nativeQueue.length) return;
    const events = nativeQueue.splice(0, 50);
    try { host.postMessage(JSON.stringify({ type: 'editor:diagnostics', sessionId, events })); }
    catch (error) { storageError = 'Native diagnostic delivery failed: ' + errorText(error); }
    if (nativeQueue.length) nativeTimer = setTimeout(flushNative, 250);
  }
  function flush() {
    clearTimeout(timer); timer = 0;
    try {
      let serialized = JSON.stringify(records);
      while (records.length && new TextEncoder().encode(serialized).length > maxBytes) {
        records.splice(0, Math.max(1, Math.floor(records.length / 8)));
        serialized = JSON.stringify(records);
      }
      // Workspace data has priority over diagnostics when the storage quota is full.
      try { localStorage.setItem(key, serialized); }
      catch (error) {
        localStorage.removeItem(key);
        records = records.slice(-100);
        localStorage.setItem(key, JSON.stringify(records));
      }
      storageError = '';
    } catch (error) { storageError = errorText(error); }
    flushNative();
    return !storageError;
  }
  function record(level, event, text, details = {}) {
    try {
      const now = new Date();
      const entry = {
        at: now.toISOString(), time: now.toLocaleTimeString('en-GB', { hour12: false }),
        elapsedMs: Math.round(performance.now() - started), sessionId, seq: ++sequence,
        level: levels.has(level) ? level : 'info', event: String(event).slice(0, 96),
        text: clean(String(text)), page: document.body?.dataset.page || 'startup', details: clean(details),
      };
      records.push(entry);
      if (records.length > maxRecords) records.splice(0, records.length - maxRecords);
      if (!timer) timer = setTimeout(flush, 600);
      if (host) {
        nativeQueue.push(entry);
        if (nativeQueue.length > 200) nativeQueue.shift();
        if (!nativeTimer) nativeTimer = setTimeout(flushNative, 250);
      }
      root.dispatchEvent(new CustomEvent('natware:diagnostic', { detail: entry }));
      return entry;
    } catch { return null; } // Logging must never prevent editing or error recovery.
  }
  function snapshot() {
    return {
      schemaVersion: 1, exportedAt: new Date().toISOString(), sessionId,
      storageError: storageError || null,
      environment: { userAgent: navigator.userAgent, online: navigator.onLine, hostPresent: !!host,
        viewport: { width: innerWidth, height: innerHeight }, build: root.natwareBuild || null },
      records: records.slice(),
    };
  }
  root.NatwareDiagnostics = Object.freeze({ record, flush, snapshot, errorText, sessionId });
  root.addEventListener('error', event => {
    if (handlingError) return;
    handlingError = true;
    try {
      const resource = event.target !== root && event.target?.tagName;
      record('error', resource ? 'resource.failed' : 'javascript.error',
        resource ? 'A page resource failed to load' : (event.message || 'JavaScript error'),
        resource ? { tag: event.target.tagName, url: String(event.target.src || event.target.href || '').split('?')[0] }
          : { filename: event.filename, line: event.lineno, column: event.colno, error: errorText(event.error) });
    } finally { handlingError = false; }
  }, true);
  root.addEventListener('unhandledrejection', event => record('error', 'javascript.rejection', 'Unhandled promise rejection', { error: errorText(event.reason) }));
  root.addEventListener('online', () => record('info', 'network.online', 'Network connection available'));
  root.addEventListener('offline', () => record('warning', 'network.offline', 'Network connection lost'));
  root.addEventListener('pagehide', () => { record('info', 'page.hide', 'Page hidden or closed'); flush(); });
  document.addEventListener('visibilitychange', () => {
    record('debug', 'page.visibility', 'Page visibility changed', { state: document.visibilityState });
    if (document.visibilityState === 'hidden') flush();
  });
  record('info', 'session.start', 'Editor page started', { hostPresent: !!host });
  if (storageError) record('warning', 'diagnostics.storage', 'Previous diagnostic history could not be loaded', { error: storageError });
})(window);
