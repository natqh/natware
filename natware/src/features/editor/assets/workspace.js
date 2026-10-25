"use strict";
// natware editor: tabs, the script hub (ScriptBlox plus your own saved scripts), the console and
// preferences. Your scripts, favorites and preferences stay in localStorage on this device.
// Host replies describe transport receipt only.
// Opening a script from the hub only puts its text into a tab.
const $ = (s, root = document) => root.querySelector(s);
const $$ = (s, root = document) => Array.from(root.querySelectorAll(s));

// ---- icons: Lucide (lucide-static 0.460.0, ISC), the set the external menu draws -------------
const ICONS = {
  'badge-check': '<path d="M3.85 8.62a4 4 0 0 1 4.78-4.77 4 4 0 0 1 6.74 0 4 4 0 0 1 4.78 4.78 4 4 0 0 1 0 6.74 4 4 0 0 1-4.77 4.78 4 4 0 0 1-6.75 0 4 4 0 0 1-4.78-4.77 4 4 0 0 1 0-6.76Z"/><path d="m9 12 2 2 4-4"/>',
  'calendar': '<path d="M8 2v4"/><path d="M16 2v4"/><rect width="18" height="18" x="3" y="4" rx="2"/><path d="M3 10h18"/>',
  'clock': '<circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>',
  'eye': '<path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"/><circle cx="12" cy="12" r="3"/>',
  'gamepad-2': '<line x1="6" x2="10" y1="11" y2="11"/><line x1="8" x2="8" y1="9" y2="13"/><line x1="15" x2="15.01" y1="12" y2="12"/><line x1="18" x2="18.01" y1="10" y2="10"/><path d="M17.32 5H6.68a4 4 0 0 0-3.978 3.59c-.006.052-.01.101-.017.152C2.604 9.416 2 14.456 2 16a3 3 0 0 0 3 3c1 0 1.5-.5 2-1l1.414-1.414A2 2 0 0 1 9.828 16h4.344a2 2 0 0 1 1.414.586L17 18c.5.5 1 1 2 1a3 3 0 0 0 3-3c0-1.545-.604-6.584-.685-7.258-.007-.05-.011-.1-.017-.151A4 4 0 0 0 17.32 5z"/>',
  'globe': '<circle cx="12" cy="12" r="10"/><path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/><path d="M2 12h20"/>',
  'key-round': '<path d="M2.586 17.414A2 2 0 0 0 2 18.828V21a1 1 0 0 0 1 1h3a1 1 0 0 0 1-1v-1a1 1 0 0 1 1-1h1a1 1 0 0 0 1-1v-1a1 1 0 0 1 1-1h.172a2 2 0 0 0 1.414-.586l.814-.814a6.5 6.5 0 1 0-4-4z"/><circle cx="16.5" cy="7.5" r=".5" fill="currentColor"/>',
  'rotate-ccw': '<path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/>',
  'shield-check': '<path d="M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z"/><path d="m9 12 2 2 4-4"/>',
  'sliders-horizontal': '<line x1="21" x2="14" y1="4" y2="4"/><line x1="10" x2="3" y1="4" y2="4"/><line x1="21" x2="12" y1="12" y2="12"/><line x1="8" x2="3" y1="12" y2="12"/><line x1="21" x2="16" y1="20" y2="20"/><line x1="12" x2="3" y1="20" y2="20"/><line x1="14" x2="14" y1="2" y2="6"/><line x1="8" x2="8" y1="10" y2="14"/><line x1="16" x2="16" y1="18" y2="22"/>',
  'thumbs-down': '<path d="M17 14V2"/><path d="M9 18.12 10 14H4.17a2 2 0 0 1-1.92-2.56l2.33-8A2 2 0 0 1 6.5 2H20a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-2.76a2 2 0 0 0-1.79 1.11L12 22a3.13 3.13 0 0 1-3-3.88Z"/>',
  'thumbs-up': '<path d="M7 10v12"/><path d="M15 5.88 14 10h5.83a2 2 0 0 1 1.92 2.56l-2.33 8A2 2 0 0 1 17.5 22H4a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2h2.76a2 2 0 0 0 1.79-1.11L12 2a3.13 3.13 0 0 1 3 3.88Z"/>',
  'bookmark-plus': '<path d="m19 21-7-4-7 4V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v16z"/><line x1="12" x2="12" y1="7" y2="13"/><line x1="15" x2="9" y1="10" y2="10"/>',
  'check': '<path d="M20 6 9 17l-5-5"/>',
  'chevron-down': '<path d="m6 9 6 6 6-6"/>',
  'clipboard-paste': '<path d="M15 2H9a1 1 0 0 0-1 1v2c0 .6.4 1 1 1h6c.6 0 1-.4 1-1V3c0-.6-.4-1-1-1Z"/><path d="M8 4H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2M16 4h2a2 2 0 0 1 2 2v2M11 14h10"/><path d="m17 10 4 4-4 4"/>',
  'code-xml': '<path d="m18 16 4-4-4-4"/><path d="m6 8-4 4 4 4"/><path d="m14.5 4-5 16"/>',
  'copy': '<rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/>',
  'download': '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" x2="12" y1="15" y2="3"/>',
  'eraser': '<path d="m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21"/><path d="M22 21H7"/><path d="m5 11 9 9"/>',
  'folder-open': '<path d="m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2"/>',
  'info': '<circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/>',
  'library-big': '<rect width="8" height="18" x="3" y="3" rx="1"/><path d="M7 3v18"/><path d="M20.4 18.9c.2.5-.1 1.1-.6 1.3l-1.9.7c-.5.2-1.1-.1-1.3-.6L11.1 5.1c-.2-.5.1-1.1.6-1.3l1.9-.7c.5-.2 1.1.1 1.3.6Z"/>',
  'maximize-2': '<polyline points="15 3 21 3 21 9"/><polyline points="9 21 3 21 3 15"/><line x1="21" x2="14" y1="3" y2="10"/><line x1="3" x2="10" y1="21" y2="14"/>',
  'minus': '<path d="M5 12h14"/>',
  'play': '<polygon points="6 3 20 12 6 21 6 3"/>',
  'plug': '<path d="M12 22v-5"/><path d="M9 8V2"/><path d="M15 8V2"/><path d="M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8Z"/>',
  'plus': '<path d="M5 12h14"/><path d="M12 5v14"/>',
  'search': '<circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>',
  'settings-2': '<path d="M20 7h-9"/><path d="M14 17H5"/><circle cx="17" cy="17" r="3"/><circle cx="7" cy="7" r="3"/>',
  'square-terminal': '<path d="m7 11 2-2-2-2"/><path d="M11 13h4"/><rect width="18" height="18" x="3" y="3" rx="2" ry="2"/>',
  'star': '<path d="M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 1.595 1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 2.123 0 0 0-.611 1.878l.882 5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 2.122 0 0 0-1.973 0L6.396 21.01a.53.53 0 0 1-.77-.56l.881-5.139a2.122 2.122 0 0 0-.611-1.879L2.16 9.795a.53.53 0 0 1 .294-.906l5.165-.755a2.122 2.122 0 0 0 1.597-1.16z"/>',
  'trash-2': '<path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/><line x1="10" x2="10" y1="11" y2="17"/><line x1="14" x2="14" y1="11" y2="17"/>',
  'x': '<path d="M18 6 6 18"/><path d="m6 6 12 12"/>',
};
function icon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  svg.innerHTML = ICONS[name] || ICONS['code-xml'];
  return svg;
}
function hydrate(root = document) { root.querySelectorAll('[data-icon]').forEach(n => n.replaceChildren(icon(n.dataset.icon))); }
function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}
hydrate();

// ---- state: same storage key and shape as before, so saved scripts and preferences carry over --
const STORE = 'natware.workspace.v2';
const diagnostics = window.NatwareDiagnostics;
let saved = {};
let storageLoadError = null, storageWriteFailed = false;
try {
  const raw = localStorage.getItem(STORE);
  try {
    saved = JSON.parse(raw || '{}');
    if (!saved || typeof saved !== 'object' || Array.isArray(saved)) throw new Error('Workspace must be an object');
  } catch (error) {
    // Preserve the original data before a future edit writes a recovered workspace.
    if (raw) localStorage.setItem(STORE + '.recovery', raw);
    throw error;
  }
} catch (error) { storageLoadError = diagnostics.errorText(error); saved = {}; }
let tabs = Array.isArray(saved.tabs) ? saved.tabs.filter(t => t && typeof t.id === 'string' && typeof t.name === 'string' && typeof t.source === 'string') : [];
if (!tabs.length) tabs = [{ id: crypto.randomUUID(), name: 'Script.lua', source: '-- Your next idea starts here.\nprint("hi")\n' }];
let activeId = tabs.some(t => t.id === saved.activeId) ? saved.activeId : tabs[0].id;
let custom = Array.isArray(saved.custom) ? saved.custom.filter(s => s && typeof s.id === 'string' && typeof s.name === 'string' && typeof s.source === 'string') : [];
let favorites = new Set(Array.isArray(saved.favorites) ? saved.favorites : []);
// Snapshots of starred ScriptBlox scripts, so Favorites shows them without going online.
let starred = saved.starred && typeof saved.starred === 'object' ? saved.starred : {};
let prefs = { fontSize: 14, wordWrap: false, minimap: false, smooth: true, ...(saved.prefs || {}) };
prefs.fontSize = Number.isFinite(Number(prefs.fontSize)) ? Math.max(10, Math.min(28, Number(prefs.fontSize))) : 14;
for (const key of ['wordWrap', 'minimap', 'smooth']) prefs[key] = typeof prefs[key] === 'boolean' ? prefs[key] : key === 'smooth';
const savedHub = saved.hub && typeof saved.hub === 'object' ? saved.hub : {};
let hubPrefs = { sort: 'views', access: 'any', status: 'any', scope: 'any', price: 'free', verified: false };
for (const k of ['sort', 'access', 'status', 'scope', 'price']) if (typeof savedHub[k] === 'string') hubPrefs[k] = savedHub[k];
if (typeof savedHub.verified === 'boolean') hubPrefs.verified = savedHub.verified;
if (savedHub.flags) {
  if (savedHub.flags.keyless) hubPrefs.access = 'keyless';
  if (savedHub.flags.working) hubPrefs.status = 'working';
  if (savedHub.flags.universal) hubPrefs.scope = 'universal';
  if (savedHub.flags.verified) hubPrefs.verified = true;
}
let consoleCollapsed = !!saved.consoleCollapsed;

let ed = null, currentPage = 'editor', hubView = 'browse', logFilter = 'all';
let logs = [], toastTimer = 0, storeTimer = 0;
const models = new Map(), viewStates = new Map();
const active = () => tabs.find(t => t.id === activeId);
const reducedMotion = () => !prefs.smooth || matchMedia('(prefers-reduced-motion: reduce)').matches;

function writeStore() {
  clearTimeout(storeTimer);
  storeTimer = 0;
  try {
    localStorage.setItem(STORE, JSON.stringify({ tabs, activeId, custom, favorites: [...favorites], starred, prefs, hub: hubPrefs, consoleCollapsed }));
    if (storageWriteFailed) log('Workspace storage is available again', 'info', 'storage.recovered');
    storageWriteFailed = false;
    return true;
  } catch (error) {
    storageWriteFailed = true;
    diagnostics.record('error', 'storage.write_failed', 'Workspace changes could not be saved', { error: diagnostics.errorText(error) });
    toast('Your workspace could not be saved on this device.', 'info');
    return false;
  }
}
function persist() {
  clearTimeout(storeTimer);
  storeTimer = setTimeout(writeStore, 150);
}
// The native window calls this before it closes so the last keystrokes are kept.
window.natwareFlush = () => {
  const workspaceSaved = storeTimer ? writeStore() : !storageWriteFailed;
  diagnostics.record(workspaceSaved ? 'info' : 'error', 'workspace.flush', 'Workspace close flush', { workspaceSaved });
  diagnostics.flush();
  return workspaceSaved;
};
addEventListener('pagehide', () => window.natwareFlush());

// ---- toast ---------------------------------------------------------------------------------
function toast(text, kind = 'check') {
  const t = $('#toast');
  clearTimeout(toastTimer);
  t.querySelector('i').replaceChildren(icon(kind === 'info' ? 'info' : 'check'));
  t.querySelector('span').textContent = text;
  t.classList.add('visible');
  toastTimer = setTimeout(() => t.classList.remove('visible'), 2800);
}

// ---- combo: the menu's dropdown (frame + chevron, opaque popup of 28px rows) -------------------
function combo(root, { options, value, onChange, label, align = 'right' }) {
  root.classList.toggle('left', align === 'left');
  const button = el('button', 'combo-button');
  button.type = 'button';
  button.setAttribute('aria-haspopup', 'listbox');
  button.setAttribute('aria-expanded', 'false');
  if (label) button.setAttribute('aria-label', label);
  const shown = el('span', 'combo-value');
  const chevron = el('i'); chevron.append(icon('chevron-down'));
  button.append(shown, chevron);
  const popup = el('div', 'combo-popup');
  popup.setAttribute('role', 'listbox');
  popup.hidden = true;
  root.replaceChildren(button, popup);
  let current = value;
  const open = show => {
    popup.hidden = !show;
    button.setAttribute('aria-expanded', String(show));
    if (show) popup.querySelector('[aria-selected="true"]')?.focus();
  };
  const render = () => {
    shown.textContent = options.find(o => o.value === current)?.label ?? '';
    popup.replaceChildren(...options.map(o => {
      const option = el('button', 'combo-option');
      option.type = 'button';
      option.setAttribute('role', 'option');
      option.setAttribute('aria-selected', String(o.value === current));
      option.append(el('span', '', o.label), icon('check'));
      option.addEventListener('click', () => {
        open(false);
        button.focus();
        if (o.value !== current) { current = o.value; render(); onChange(current); }
      });
      option.addEventListener('keydown', e => {
        if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); (e.key === 'ArrowDown' ? option.nextElementSibling : option.previousElementSibling)?.focus(); }
        if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); open(false); button.focus(); }
      });
      return option;
    }));
  };
  button.addEventListener('click', () => open(popup.hidden));
  document.addEventListener('mousedown', e => { if (!root.contains(e.target)) open(false); });
  render();
  return { close: () => open(false), set: v => { current = v; render(); } };
}

// ---- console: one output log, shown compact under the editor and full on its own page ----------
const logViews = [
  { host: $('[data-log="compact"]'), filter: () => 'all' },
  { host: $('[data-log="full"]'), filter: () => logFilter },
];
function logRow(e, fresh) {
  const row = el('div', 'log-row ' + e.level + (fresh ? ' new' : ''));
  row.dataset.seq = String(e.seq || 0);
  row.title = [e.at || e.time, e.event || 'console.message', JSON.stringify(e.details || {})].join('\n');
  row.append(el('span', 'log-time', e.time), el('span', 'log-text', e.text));
  return row;
}
function logEmpty() {
  return el('div', 'log-empty', logs.length ? 'No messages at this level' : 'Nothing logged yet');
}
function nearBottom(host) { return host.scrollHeight - host.scrollTop - host.clientHeight < 28; }
function renderLogView(view) {
  const items = logs.filter(e => view.filter() === 'all' || e.level === view.filter());
  view.host.replaceChildren(...(items.length ? items.map(e => logRow(e, false)) : [logEmpty()]));
  view.host.scrollTop = view.host.scrollHeight;
}
function log(text, level = 'info', event = 'console.message', details = {}) {
  if (!diagnostics.record(level, event, String(text), details))
    appendConsole({ time: new Date().toISOString(), text: String(text), level });
}
function appendConsole(e) {
  logs.push(e);
  if (logs.length > 500) logs.shift();
  for (const v of logViews) {
    if (v.filter() !== 'all' && v.filter() !== e.level) continue;
    const stick = nearBottom(v.host);
    v.host.querySelector('.log-empty')?.remove();
    v.host.append(logRow(e, true));
    while (v.host.firstElementChild && Number(v.host.firstElementChild.dataset.seq) < (logs[0].seq || 0)) v.host.firstElementChild.remove();
    if (stick) v.host.scrollTop = v.host.scrollHeight;
  }
}
window.addEventListener('natware:diagnostic', e => {
  if (e.detail.event !== 'console.cleared') appendConsole(e.detail);
});
$$('[data-clear-console]').forEach(b => b.addEventListener('click', () => {
  logs = [];
  diagnostics.record('info', 'console.cleared', 'Visible console cleared; diagnostic history retained');
  logViews.forEach(renderLogView);
  toast('Console cleared');
}));
$('[data-copy-console]').addEventListener('click', async () => {
  const text = logs.filter(e => logFilter === 'all' || e.level === logFilter)
    .map(e => `${e.at || e.time} [${e.level}] ${e.event || 'console.message'}: ${e.text}${e.details && Object.keys(e.details).length ? ' ' + JSON.stringify(e.details) : ''}`).join('\n');
  try { await navigator.clipboard.writeText(text); toast('Console copied'); } catch { toast('Select the lines and press Ctrl+C to copy.', 'info'); }
});
combo($('#log-level'), {
  label: 'Show messages', align: 'left', value: 'all',
  options: [{ value: 'all', label: 'All messages' }, { value: 'info', label: 'Info' }, { value: 'success', label: 'Success' }, { value: 'warning', label: 'Warnings' }, { value: 'error', label: 'Errors' }, { value: 'debug', label: 'Debug' }],
  onChange: v => { logFilter = v; renderLogView(logViews[1]); },
});
function setConsoleCollapsed(collapsed) {
  consoleCollapsed = collapsed;
  $('#inline-console').classList.toggle('collapsed', collapsed);
  $('#toggle-console').setAttribute('aria-expanded', String(!collapsed));
  $('#toggle-console').title = collapsed ? 'Expand console' : 'Collapse console';
  persist();
}
$('#toggle-console').addEventListener('click', () => setConsoleCollapsed(!consoleCollapsed));
$('#expand-console').addEventListener('click', () => showPage('console'));

// ---- navigation ------------------------------------------------------------------------------
const TITLES = { editor: 'Editor', hub: 'Script hub', console: 'Console' };
function moveNavIndicator() {
  const current = $(`.nav[data-page="${currentPage}"]`);
  $('.nav-indicator').style.setProperty('--y', current.offsetTop + 'px');
}
function showPage(page) {
  if (!TITLES[page]) return;
  const changed = page !== currentPage;
  currentPage = page;
  document.body.dataset.page = page;
  $$('.page').forEach(p => p.classList.toggle('active', p.id === 'page-' + page));
  $$('.nav[data-page]').forEach(n => n.dataset.page === page ? n.setAttribute('aria-current', 'page') : n.removeAttribute('aria-current'));
  moveNavIndicator();
  const title = $('#page-title');
  if (changed && !reducedMotion()) {
    title.classList.add('swap');
    setTimeout(() => { title.textContent = TITLES[page]; title.classList.remove('swap'); }, 120);
  } else title.textContent = TITLES[page];
  if (page !== 'hub') { closeDrawer(); setFilterPanel(false); }
  if (page === 'hub') hubShown();
  if (page === 'console') { const h = logViews[1].host; h.scrollTop = h.scrollHeight; }
  if (page === 'editor' && ed) requestAnimationFrame(() => ed.layout());
}
$$('.nav[data-page]').forEach(b => b.addEventListener('click', () => showPage(b.dataset.page)));

// ---- tabs ------------------------------------------------------------------------------------
function moveTabIndicator(instant = false) {
  const host = $('#tabs'), bar = host.querySelector('.tab-indicator'), tab = host.querySelector('.tab.active');
  if (!tab) return;
  if (instant) bar.style.transition = 'none';
  bar.style.setProperty('--x', tab.offsetLeft + 'px');
  bar.style.setProperty('--w', tab.offsetWidth + 'px');
  if (instant) { bar.offsetWidth; bar.style.transition = ''; }
  const left = tab.offsetLeft - 24, right = tab.offsetLeft + tab.offsetWidth + 24;
  if (left < host.scrollLeft) host.scrollLeft = left;
  else if (right > host.scrollLeft + host.clientWidth) host.scrollLeft = right - host.clientWidth;
}
function renderTabs(instant = false) {
  const host = $('#tabs'), bar = host.querySelector('.tab-indicator');
  const items = tabs.map(t => {
    const tab = el('div', 'tab' + (t.id === activeId ? ' active' : ''));
    tab.setAttribute('role', 'tab');
    tab.setAttribute('aria-selected', String(t.id === activeId));
    tab.tabIndex = t.id === activeId ? 0 : -1;
    tab.title = t.name;
    const close = el('span', 'tab-close');
    close.setAttribute('role', 'button');
    close.setAttribute('aria-label', 'Close ' + t.name);
    close.title = 'Close (Ctrl+W)';
    close.append(icon('x'));
    close.addEventListener('click', e => { e.stopPropagation(); closeTab(t.id); });
    tab.append(el('span', 'tab-name', t.name), close);
    tab.addEventListener('click', () => selectTab(t.id));
    tab.addEventListener('auxclick', e => { if (e.button === 1) { e.preventDefault(); closeTab(t.id); } });
    tab.addEventListener('keydown', e => {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); selectTab(t.id); }
      if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
        const i = tabs.findIndex(x => x.id === t.id) + (e.key === 'ArrowRight' ? 1 : -1);
        if (tabs[i]) { selectTab(tabs[i].id, false); $('#tabs .tab.active')?.focus(); }
      }
    });
    return tab;
  });
  host.replaceChildren(bar, ...items);
  requestAnimationFrame(() => moveTabIndicator(instant));
}
$('#tabs').addEventListener('wheel', e => {
  if (Math.abs(e.deltaY) > Math.abs(e.deltaX)) { e.currentTarget.scrollLeft += e.deltaY; e.preventDefault(); }
}, { passive: false });
function selectTab(id, focusEditor = true) {
  if (ed && models.has(activeId)) viewStates.set(activeId, ed.saveViewState());
  activeId = id;
  if (ed) {
    if (!models.has(id)) models.set(id, monaco.editor.createModel(active().source, 'lua'));
    ed.setModel(models.get(id));
    if (viewStates.has(id)) ed.restoreViewState(viewStates.get(id));
    if (focusEditor) ed.focus();
  } else $('#fallback-editor').value = active().source;
  renderTabs();
  persist();
}
function newTab(source = '', name = 'Untitled.lua', hubId = null) {
  const tab = { id: crypto.randomUUID(), name, source, hubId };
  tabs.push(tab);
  selectTab(tab.id);
  showPage('editor');
  return tab;
}
function closeTab(id) {
  const index = tabs.findIndex(t => t.id === id);
  if (index < 0) return;
  const wasActive = id === activeId;
  tabs.splice(index, 1);
  if (models.has(id)) { models.get(id).dispose(); models.delete(id); }
  viewStates.delete(id);
  if (!tabs.length) tabs.push({ id: crypto.randomUUID(), name: 'Untitled.lua', source: '' });
  if (wasActive) selectTab(tabs[Math.min(index, tabs.length - 1)].id);
  else { renderTabs(); persist(); }
}
$('#new-tab').addEventListener('click', () => newTab());

function setSource(text) {
  if (ed) {
    ed.executeEdits('workspace', [{ range: ed.getModel().getFullModelRange(), text, forceMoveMarkers: true }]);
    ed.pushUndoStop();
  } else {
    $('#fallback-editor').value = text;
    active().source = text;
    persist();
  }
}
const fallback = $('#fallback-editor');
fallback.value = active().source;
fallback.addEventListener('input', () => { active().source = fallback.value; persist(); });
fallback.addEventListener('keydown', e => {
  if (e.key !== 'Tab') return;
  e.preventDefault();
  fallback.setRangeText('    ', fallback.selectionStart, fallback.selectionEnd, 'end');
  fallback.dispatchEvent(new Event('input'));
});

// ---- Monaco ----------------------------------------------------------------------------------
function editorUnavailable() {
  log('Code highlighting could not load. Plain text editing still works.', 'warning');
}
function monacoOptions() {
  const size = Number(prefs.fontSize);
  return {
    fontSize: size, lineHeight: Math.round(size * 1.6),
    wordWrap: prefs.wordWrap ? 'on' : 'off', minimap: { enabled: prefs.minimap, renderCharacters: false, scale: 1 },
    smoothScrolling: !reducedMotion(), cursorSmoothCaretAnimation: reducedMotion() ? 'off' : 'on', cursorBlinking: reducedMotion() ? 'blink' : 'smooth',
  };
}
function startMonaco() {
  if (typeof require === 'undefined') { editorUnavailable(); return; }
  require.config({ paths: { vs: 'https://cdn.jsdelivr.net/npm/monaco-editor@0.45.0/min/vs' } });
  require(['vs/editor/editor.main'], () => {
    monaco.editor.defineTheme('natware', {
      base: 'vs-dark', inherit: true,
      rules: [
        { token: '', foreground: 'CDCDD5' },
        { token: 'keyword', foreground: 'F7F7FA' },
        { token: 'identifier', foreground: 'CDCDD5' },
        { token: 'string', foreground: 'A2D6B1' },
        { token: 'string.escape', foreground: 'BFE6CA' },
        { token: 'number', foreground: 'E6C98F' },
        { token: 'comment', foreground: '868691' },
        { token: 'delimiter', foreground: '95959F' },
        { token: 'operator', foreground: 'ABABB5' },
      ],
      colors: {
        'editor.background': '#00000000', 'editor.foreground': '#CDCDD5', 'editorGutter.background': '#00000000',
        'editorLineNumber.foreground': '#5E5E68', 'editorLineNumber.activeForeground': '#CDCDD5',
        'editor.lineHighlightBackground': '#FFFFFF07', 'editor.lineHighlightBorder': '#00000000',
        'editor.selectionBackground': '#FFFFFF2A', 'editor.inactiveSelectionBackground': '#FFFFFF16',
        'editor.selectionHighlightBackground': '#FFFFFF10', 'editor.wordHighlightBackground': '#FFFFFF0E', 'editor.wordHighlightStrongBackground': '#FFFFFF14',
        'editorCursor.foreground': '#F8F8FA',
        'editorIndentGuide.background1': '#FFFFFF0B', 'editorIndentGuide.activeBackground1': '#FFFFFF20', 'editorWhitespace.foreground': '#FFFFFF16',
        'editorBracketMatch.background': '#FFFFFF12', 'editorBracketMatch.border': '#FFFFFF2E',
        'editor.findMatchBackground': '#FFFFFF38', 'editor.findMatchHighlightBackground': '#FFFFFF1C',
        'editorWidget.background': '#141418', 'editorWidget.border': '#FFFFFF16', 'editorWidget.foreground': '#F0F0F3',
        'editorSuggestWidget.background': '#141418', 'editorSuggestWidget.border': '#FFFFFF16', 'editorSuggestWidget.selectedBackground': '#FFFFFF12',
        'editorHoverWidget.background': '#141418', 'editorHoverWidget.border': '#FFFFFF16',
        'input.background': '#FFFFFF0C', 'input.border': '#FFFFFF10', 'focusBorder': '#FFFFFF2A',
        'scrollbar.shadow': '#00000000', 'scrollbarSlider.background': '#FFFFFF16', 'scrollbarSlider.hoverBackground': '#FFFFFF26', 'scrollbarSlider.activeBackground': '#FFFFFF33',
        'minimap.background': '#00000000', 'editorStickyScroll.background': '#0E0E12', 'widget.shadow': '#00000066',
        'menu.background': '#141418', 'menu.selectionBackground': '#FFFFFF14', 'menu.border': '#FFFFFF16',
      },
    });
    fallback.hidden = true;
    const model = monaco.editor.createModel(active().source, 'lua');
    models.set(activeId, model);
    ed = monaco.editor.create($('#editor-host'), {
      model, theme: 'natware', ...monacoOptions(),
      fontFamily: "Consolas, 'Cascadia Mono', monospace", fontLigatures: false,
      padding: { top: 12, bottom: 12 }, lineNumbersMinChars: 3, lineDecorationsWidth: 14, glyphMargin: false,
      folding: true, showFoldingControls: 'mouseover', foldingHighlight: false,
      renderLineHighlight: 'all', roundedSelection: true, cursorWidth: 2,
      overviewRulerLanes: 0, overviewRulerBorder: false, hideCursorInOverviewRuler: true,
      scrollBeyondLastLine: false, automaticLayout: true, fixedOverflowWidgets: true,
      scrollbar: { verticalScrollbarSize: 10, horizontalScrollbarSize: 10, verticalSliderSize: 6, horizontalSliderSize: 6, useShadows: false, alwaysConsumeMouseWheel: false },
      renderWhitespace: 'selection', tabSize: 4, stickyScroll: { enabled: false },
      bracketPairColorization: { enabled: false }, guides: { indentation: true, bracketPairs: false },
      suggest: { showWords: false }, quickSuggestions: { other: false, comments: false, strings: false }, contextmenu: true,
    });
    ed.onDidChangeModelContent(() => { const t = active(); if (t && ed.getModel() === models.get(t.id)) { t.source = ed.getValue(); persist(); } });
    ed.onDidChangeCursorPosition(e => { $('#cursor-position').textContent = `Ln ${e.position.lineNumber}, Col ${e.position.column}`; });
    if (currentPage === 'editor') ed.focus();
  }, editorUnavailable);
}

// ---- dialogs -----------------------------------------------------------------------------------
function openDialog(d) {
  d.classList.remove('closing');
  if (!d.open) d.showModal();
}
function closeDialog(d, value = 'cancel') {
  if (!d.open || d.classList.contains('closing')) return;
  d.returnValue = value;
  if (reducedMotion()) { d.close(value); return; }
  d.classList.add('closing');
  setTimeout(() => { d.classList.remove('closing'); d.close(value); }, 190);
}
$$('dialog').forEach(d => {
  d.addEventListener('cancel', e => { e.preventDefault(); closeDialog(d); });
  d.addEventListener('mousedown', e => {
    if (e.target !== d) return;
    const r = d.getBoundingClientRect();
    if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) closeDialog(d);
  });
  $$('[data-close]', d).forEach(b => b.addEventListener('click', () => closeDialog(d)));
});
function confirmAction(title, message, label, callback) {
  const d = $('#confirm-dialog');
  $('#confirm-title').textContent = title;
  $('#confirm-message').textContent = message;
  $('#confirm-ok').textContent = label;
  d.onclose = () => { if (d.returnValue === 'confirm') callback(); };
  openDialog(d);
  $('#confirm-ok').focus();
}
$('#confirm-form').addEventListener('submit', e => { e.preventDefault(); closeDialog($('#confirm-dialog'), 'confirm'); });

// ---- toolbar -----------------------------------------------------------------------------------
const nexecHost = window.chrome && window.chrome.webview;
let nexecConnected = null;
let hostResponsive = false, connectionTimer = 0, connectionHeartbeat = 0, lastStatusAt = performance.now();
const pendingRequests = new Map();
const pendingOf = kind => [...pendingRequests.values()].find(r => r.kind === kind);
function renderConnection() {
  const attaching = pendingOf('attach'), executing = pendingOf('execute');
  const b = $('#attach'), execute = $('#execute'), status = $('#connection-status');
  b.querySelector('span').textContent = attaching ? 'Attaching…' : nexecConnected ? 'Attached' : 'Attach';
  b.disabled = !nexecHost || !!attaching;
  b.title = nexecConnected ? 'Backend connected' : 'Connect to Roblox';
  execute.disabled = !!executing;
  execute.querySelector('span').textContent = executing ? 'Waiting…' : 'Execute';
  execute.setAttribute('aria-busy', String(!!executing));
  status.dataset.state = !nexecHost || !hostResponsive || nexecConnected === false ? 'error' : nexecConnected ? 'ok' : 'unknown';
  status.textContent = !nexecHost ? 'Editor host unavailable'
    : !hostResponsive ? 'Waiting for editor host…'
    : attaching ? 'Attaching…'
    : nexecConnected ? 'Attached'
    : 'Not attached';
  $('#open-log-folder').disabled = !nexecHost;
}
function setNexec(connected) {
  if (connected !== nexecConnected)
    log(connected ? 'Attached' : 'Attach broken', connected ? 'info' : 'warning', 'connection.pipe', { pipeAvailable: connected });
  nexecConnected = connected;
  hostResponsive = true;
  lastStatusAt = performance.now();
  clearTimeout(connectionTimer);
  renderConnection();
}
function sendNexec(message, silent = false) {
  if (!nexecHost) { if (!silent) toast('Not attached', 'info'); return false; }
  try { nexecHost.postMessage(JSON.stringify(message)); return true; }
  catch (error) {
    hostResponsive = false;
    log('Editor host message could not be sent', 'error', 'host.send_failed', { type: message.type, requestId: message.requestId, error: diagnostics.errorText(error) });
    renderConnection();
    return false;
  }
}
function beginRequest(kind, payload = {}, timeoutMs = 15000) {
  if (pendingOf(kind)) { toast('A ' + kind + ' request is already pending', 'info'); return; }
  const requestId = crypto.randomUUID();
  const request = { requestId, kind, started: performance.now(), timer: 0 };
  pendingRequests.set(requestId, request);
  // Register before sending: the host may respond synchronously in a connected test session.
  request.timer = setTimeout(() => {
    pendingRequests.delete(requestId);
    log('No ' + kind + ' confirmation received. Outcome is unknown.', 'error', 'request.timeout', { requestId, kind, elapsedMs: Math.round(performance.now() - request.started) });
    toast(kind === 'execute' ? 'Execute timed out' : 'Attach timed out', 'info');
    renderConnection();
  }, timeoutMs);
  if (!sendNexec({ ...payload, type: 'nexec:' + kind, requestId }, true)) {
    clearTimeout(request.timer); pendingRequests.delete(requestId);
    log('Request could not be sent to the editor host', 'error', 'request.rejected', { requestId, kind });
    toast('Editor host unavailable', 'info');
  } else diagnostics.record('info', 'request.sent', 'Editor request sent', { requestId, kind, bytes: typeof payload.source === 'string' ? new TextEncoder().encode(payload.source).length : 0 });
  renderConnection();
}
function finishRequest(message, kind) {
  const request = typeof message.requestId === 'string' ? pendingRequests.get(message.requestId) : null;
  if (!request || request.kind !== kind) {
    diagnostics.record('warning', 'request.unmatched', 'Unmatched or late response ignored', { type: message.type, requestId: message.requestId || null });
    return false;
  }
  clearTimeout(request.timer); pendingRequests.delete(request.requestId);
  diagnostics.record('info', 'request.response', 'Editor host responded', { requestId: request.requestId, kind, elapsedMs: Math.round(performance.now() - request.started), state: message.state || null });
  renderConnection();
  return true;
}
if (nexecHost) {
  nexecHost.addEventListener('message', e => {
    const m = e.data;
    if (!m || typeof m !== 'object') return;
    if (m.type === 'editor:ready') {
      window.natwareBuild = m.build;
      hostResponsive = true;
      diagnostics.record('info', 'host.ready', 'Native editor initialized', { build: m.build, logPath: m.logPath });
      renderConnection();
      return;
    }
    if (m.type === 'nexec') {
      if (typeof m.connected !== 'boolean') { diagnostics.record('error', 'host.invalid_status', 'Host sent an invalid connection status'); return; }
      setNexec(m.connected);
      if (m.requestId && finishRequest(m, 'attach')) toast(m.connected ? 'Attached' : 'Attach broken', 'info');
    } else if (m.type === 'nexec:injecting') {
      diagnostics.record('info', 'connection.pending', 'Native connection attempt started', { requestId: m.requestId });
    } else if (m.type === 'nexec:injected') {
      if (!finishRequest(m, 'attach')) return;
      if (m.ok === true) {
        setNexec(true);
        log('Attached', 'info', 'connection.result', { requestId: m.requestId });
        toast('Attached', 'info');
      } else {
        log('Attach broken: ' + (m.error || 'unknown'), 'error', 'connection.failed', { requestId: m.requestId });
        toast('Attach broken (' + (m.error || 'unknown') + ')', 'info');
      }
    } else if (m.type === 'nexec:executed') {
      if (!finishRequest(m, 'execute')) return;
      if (m.ok === true) {
        toast('Executed', 'info');
        log('Script sent' + (m.name ? ' (' + m.name + ')' : ''), 'info', 'execute.sent', { requestId: m.requestId });
      } else {
        toast(m.error || 'Execute failed', 'info');
        log('Execute failed: ' + (m.error || 'unknown'), 'error', 'execute.failed', { requestId: m.requestId });
      }
    } else if (m.type === 'editor:error') {
      if (typeof m.requestId === 'string') {
        const request = pendingRequests.get(m.requestId);
        if (request) finishRequest(m, request.kind);
      }
      log(m.error || 'Editor host reported an error', 'error', 'host.error', { requestId: m.requestId });
    }
  });
  connectionTimer = setTimeout(() => {
    hostResponsive = false; renderConnection();
    log('Editor host timeout', 'error', 'connection.status_timeout');
  }, 5000);
  sendNexec({ type: 'editor:ready', sessionId: diagnostics.sessionId });
  sendNexec({ type: 'nexec:status' });
  connectionHeartbeat = setInterval(() => {
    if (performance.now() - lastStatusAt > 12000 && hostResponsive) {
      hostResponsive = false; nexecConnected = null; renderConnection();
      log('Editor host unresponsive', 'error', 'connection.host_unresponsive');
    }
    sendNexec({ type: 'nexec:status' }, true);
  }, 5000);
}
renderConnection();
$('#execute').addEventListener('click', () => {
  const t = active();
  const source = ed ? ed.getValue() : t.source;
  if (!source.trim()) { toast('This tab is empty', 'info'); return; }
  if (new TextEncoder().encode(source).length > 2 * 1024 * 1024) { toast('Script exceeds the 2 MB request limit', 'info'); return; }
  if (!nexecHost || !hostResponsive || !nexecConnected) {
    log('Not attached', 'error', 'execute.unavailable');
    toast('Not attached', 'info'); return;
  }
  beginRequest('execute', { source, name: t.name });
});
$('#attach').addEventListener('click', () => {
  beginRequest('attach', {}, 45000);
});
$('#export-diagnostics').addEventListener('click', () => {
  diagnostics.record('info', 'diagnostics.export_requested', 'Diagnostic download requested');
  diagnostics.flush();
  const report = diagnostics.snapshot();
  const url = URL.createObjectURL(new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' }));
  const link = document.createElement('a');
  link.href = url; link.download = 'natware-diagnostics-' + new Date().toISOString().replace(/[:.]/g, '-') + '.json';
  link.click(); setTimeout(() => URL.revokeObjectURL(url), 5000);
  toast('Diagnostic download requested', 'info');
});
$('#open-log-folder').addEventListener('click', () => sendNexec({ type: 'editor:logs' }));
addEventListener('pagehide', () => {
  clearInterval(connectionHeartbeat); clearTimeout(connectionTimer);
  for (const request of pendingRequests.values()) {
    clearTimeout(request.timer);
    diagnostics.record('warning', 'request.abandoned', 'Page closed with a request pending; outcome unknown', { requestId: request.requestId, kind: request.kind });
  }
  pendingRequests.clear(); diagnostics.flush();
});
$('#clear-editor').addEventListener('click', () => {
  const t = active();
  if (!t.source) { toast('This tab is already empty', 'info'); return; }
  confirmAction('Clear this tab?', `The text in ${t.name} is removed. Ctrl+Z in the editor brings it back.`, 'Clear', () => {
    setSource('');
    log('Cleared ' + t.name);
    toast('Tab cleared');
  });
});
$('#copy-script').addEventListener('click', async () => {
  try {
    await navigator.clipboard.writeText(active().source);
    toast('Copied to clipboard');
  } catch { toast('Select the code and press Ctrl+C to copy.', 'info'); }
});
$('#paste-script').addEventListener('click', async () => {
  try {
    const text = await navigator.clipboard.readText();
    if (!text) { toast('The clipboard is empty', 'info'); return; }
    if (ed) {
      ed.executeEdits('paste', [{ range: ed.getSelection(), text, forceMoveMarkers: true }]);
      ed.pushUndoStop();
      ed.focus();
    } else {
      fallback.setRangeText(text, fallback.selectionStart, fallback.selectionEnd, 'end');
      fallback.dispatchEvent(new Event('input'));
    }
  } catch { toast('Click into the editor and press Ctrl+V to paste.', 'info'); }
});
$('#open-file').addEventListener('click', () => $('#file-input').click());
$('#file-input').addEventListener('change', async e => {
  for (const f of e.target.files) {
    if (f.size > 2 * 1024 * 1024) { toast(`${f.name} is over 2 MB`, 'info'); continue; }
    try { newTab(await f.text(), f.name); log('Opened ' + f.name, 'info', 'file.opened', { bytes: f.size }); }
    catch (error) { log('File could not be opened: ' + f.name, 'error', 'file.open_failed', { error: diagnostics.errorText(error) }); toast('File could not be opened', 'info'); }
  }
  e.target.value = '';
});
async function saveFile() {
  const t = active(), name = /\.(lua|luau|txt)$/i.test(t.name) ? t.name : t.name + '.lua';
  if (window.showSaveFilePicker) {
    try {
      const handle = await showSaveFilePicker({ suggestedName: name, types: [{ description: 'Lua script', accept: { 'text/plain': ['.lua', '.luau', '.txt'] } }] });
      const out = await handle.createWritable();
      await out.write(t.source);
      await out.close();
      log('Saved ' + handle.name, 'success');
      toast('Saved ' + handle.name);
      return;
    } catch (err) {
      if (err && err.name === 'AbortError') { diagnostics.record('info', 'file.save_cancelled', 'File save cancelled'); return; }
      log('File save failed', 'error', 'file.save_failed', { error: diagnostics.errorText(err) });
      toast('File was not saved. Check diagnostics.', 'info');
      return;
    }
  }
  const url = URL.createObjectURL(new Blob([t.source], { type: 'text/plain;charset=utf-8' }));
  const a = document.createElement('a');
  a.href = url; a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
  log('Download requested for ' + name + '; file save has not been confirmed', 'info', 'file.download_requested');
  toast('File download requested', 'info');
}
$('#save-file').addEventListener('click', saveFile);

function openSaveDialog() {
  const t = active(), existing = custom.find(s => s.id === t.hubId);
  $('#save-title').textContent = existing ? 'Update in hub' : 'Save to hub';
  $('#save-submit').textContent = existing ? 'Update' : 'Save';
  $('#script-name').value = existing?.name || t.name.replace(/\.(lua|luau|txt)$/i, '');
  $('#script-description').value = existing?.description || '';
  $('#save-error').hidden = true;
  $('#script-name').closest('.field').classList.remove('invalid');
  openDialog($('#save-dialog'));
  $('#script-name').select();
}
$('#save-hub').addEventListener('click', openSaveDialog);
$('#script-name').addEventListener('input', () => { $('#save-error').hidden = true; $('#script-name').closest('.field').classList.remove('invalid'); });
$('#save-form').addEventListener('submit', e => {
  e.preventDefault();
  const name = $('#script-name').value.trim();
  if (!name) {
    $('#save-error').hidden = false;
    $('#script-name').closest('.field').classList.add('invalid');
    $('#script-name').focus();
    return;
  }
  const t = active();
  let s = custom.find(x => x.id === t.hubId);
  const updating = !!s;
  if (!s) { s = { id: crypto.randomUUID(), tag: 'My script' }; custom.unshift(s); }
  Object.assign(s, { name, description: $('#script-description').value.trim(), source: t.source, updated: Date.now() });
  t.hubId = s.id;
  t.name = name + '.lua';
  const stored = writeStore();
  renderTabs();
  renderCounts();
  if (hubView !== 'browse') renderLocal();
  if (stored) {
    closeDialog($('#save-dialog'), 'saved');
    log((updating ? 'Updated ' : 'Saved ') + name + ' in the hub.', 'success', 'hub.saved');
    toast(updating ? 'Updated in the hub' : 'Saved to the hub');
  } else log('Hub edits remain in memory; saving on this device failed', 'error', 'hub.save_failed');
});

// ---- script hub: ScriptBlox in a card grid, plus the scripts saved on this device ---------------
const SB = 'https://scriptblox.com';
const SORTS = [
  { value: 'views', label: 'Most viewed' },
  { value: 'likeCount', label: 'Most liked' },
  { value: 'createdAt', label: 'Newest' },
  { value: 'updatedAt', label: 'Recently updated' },
];
// Each facet maps straight onto a ScriptBlox query parameter.
const FACETS = {
  access: { any: null, keyless: ['key', '0'], key: ['key', '1'] },
  status: { any: null, working: ['patched', '0'], patched: ['patched', '1'] },
  scope: { any: null, universal: ['universal', '1'], game: ['universal', '0'] },
  price: { free: ['mode', 'free'], paid: ['mode', 'paid'], any: null },
};
const FACET_DEFAULTS = { access: 'any', status: 'any', scope: 'any', price: 'free' };
for (const f in FACET_DEFAULTS) if (!(hubPrefs[f] in FACETS[f])) hubPrefs[f] = FACET_DEFAULTS[f];
if (!SORTS.some(o => o.value === hubPrefs.sort)) hubPrefs.sort = 'views';
const PILL_LABELS = { keyless: 'No key', key: 'Key system', working: 'Working', patched: 'Patched', universal: 'Universal', game: 'One game', paid: 'Paid', any: 'Free and paid' };
const browse = { items: [], next: 1, total: 0, loading: false, error: false, done: false, seq: 0, controller: null, key: '' };
const cache = new Map(), details = new Map();
let searchTimer = 0, filterTimer = 0, drawerItem = null, drawerReturn = null;

const compact = n => n >= 1e6 ? (n / 1e6).toFixed(n >= 1e7 ? 0 : 1).replace(/\.0$/, '') + 'M' : n >= 1e3 ? (n / 1e3).toFixed(n >= 1e4 ? 0 : 1).replace(/\.0$/, '') + 'K' : String(n || 0);
function imageUrl(path) {
  if (!path || typeof path !== 'string') return '';
  if (path.startsWith('/')) return SB + path;
  return /^https:\/\//.test(path) ? path : '';
}
function dateText(value) {
  const d = new Date(value);
  return isNaN(d) ? '' : d.toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' });
}
function savedWhen(time) {
  if (!time) return 'Saved';
  const d = new Date(time), now = new Date();
  const day = 864e5, start = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
  if (time >= start) return 'Saved today';
  if (time >= start - day) return 'Saved yesterday';
  return 'Saved ' + d.toLocaleDateString('en-US', { month: 'short', day: 'numeric', ...(d.getFullYear() !== now.getFullYear() ? { year: 'numeric' } : {}) });
}
function highlight(target, text, query) {
  const at = query ? text.toLowerCase().indexOf(query) : -1;
  if (at < 0) { target.textContent = text; return; }
  target.replaceChildren(text.slice(0, at), el('mark', '', text.slice(at, at + query.length)), text.slice(at + query.length));
}
function fileName(title) {
  const clean = String(title || 'Script').replace(/[\\/:*?"<>|]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 48).trim();
  return (clean || 'Script') + '.lua';
}
// ScriptBlox descriptions are Markdown; the drawer shows them as plain text with bullets.
function plainText(md) {
  return String(md || '').replace(/\r/g, '')
    .replace(/^#{1,6}[ \t]*/gm, '')
    .replace(/^[ \t]*[-*+][ \t]+/gm, '\u2022 ')
    .replace(/\*\*(.+?)\*\*/g, '$1').replace(/__(.+?)__/g, '$1')
    .replace(/`([^`]+)`/g, '$1')
    .replace(/\[([^\]]+)\]\([^)]+\)/g, '$1')
    .replace(/\n{3,}/g, '\n\n').trim();
}
function withIcon(name, text, cls = '') {
  const s = el('span', cls);
  s.append(icon(name), document.createTextNode(text));
  return s;
}
function image(src, onFail) {
  const img = el('img');
  img.alt = '';
  img.loading = 'lazy';
  img.decoding = 'async';
  img.referrerPolicy = 'no-referrer';
  img.addEventListener('load', () => img.classList.add('loaded'));
  img.addEventListener('error', () => { img.remove(); onFail?.(); });
  img.src = src;
  return img;
}
function placeholder() {
  const p = el('span', 'placeholder');
  p.append(icon('code-xml'));
  return p;
}
// What a starred ScriptBlox script keeps on this device, so Favorites works offline.
function snapshot(s) {
  return { _id: s._id, slug: s.slug, title: s.title, game: { name: s.game?.name || '' }, image: s.image || s.game?.imageUrl || '', views: s.views || 0,
    verified: !!s.verified, key: !!s.key, isUniversal: !!s.isUniversal, isPatched: !!s.isPatched, scriptType: s.scriptType,
    script: typeof s.script === 'string' && s.script.length < 4000 ? s.script : undefined };
}
const remoteId = s => 'sb:' + s._id;

// ---- query -----------------------------------------------------------------------------------
function query() { return $('#hub-search').value.trim(); }
function apiUrl(page) {
  const p = new URLSearchParams({ page: String(page), max: '20', sortBy: hubPrefs.sort, order: 'desc' });
  for (const facet in FACETS) {
    const param = FACETS[facet][hubPrefs[facet]];
    if (param) p.set(param[0], param[1]);
  }
  if (hubPrefs.verified) p.set('verified', '1');
  const q = query();
  if (q) { p.set('q', q); return `${SB}/api/script/search?${p}`; }
  return `${SB}/api/script/fetch?${p}`;
}
async function fetchPage(page) {
  const url = apiUrl(page);
  if (cache.has(url)) { diagnostics.record('debug', 'hub.cache_hit', 'Using cached hub results', { page }); return cache.get(url); }
  browse.controller?.abort();
  browse.controller = new AbortController();
  const started = performance.now();
  diagnostics.record('info', 'hub.request', 'Loading hub results', { page });
  const res = await fetch(url, { signal: browse.controller.signal, headers: { Accept: 'application/json' } });
  diagnostics.record(res.ok ? 'info' : 'error', 'hub.response', 'Hub responded', { page, status: res.status, elapsedMs: Math.round(performance.now() - started) });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  const data = await res.json();
  if (!data?.result || !Array.isArray(data.result.scripts)) throw new Error('Hub returned an invalid result structure');
  const result = data.result;
  const scripts = result.scripts;
  const out = { scripts, next: result.nextPage || null, pages: result.totalPages || (scripts.length ? 1 : 0) };
  cache.set(url, out);
  if (cache.size > 40) cache.delete(cache.keys().next().value);
  return out;
}
async function loadBrowse(reset) {
  if (reset) {
    browse.seq++;
    Object.assign(browse, { items: [], next: 1, total: -1, loading: false, error: false, done: false, key: apiUrl(1) });
    renderBrowse(true);
  }
  if (browse.loading || browse.done || browse.error) return;
  const seq = browse.seq;
  browse.loading = true;
  renderMore();
  try {
    const page = await fetchPage(browse.next);
    if (seq !== browse.seq) return;
    const known = new Set(browse.items.map(s => s._id));
    const fresh = page.scripts.filter(s => s && s._id && !known.has(s._id));
    const first = !browse.items.length;
    browse.items.push(...fresh);
    if (first) browse.total = page.pages <= 1 ? page.scripts.length : page.pages * 20;
    browse.next = page.next;
    browse.done = !page.next || !page.scripts.length;
    browse.loading = false;
    if (first) renderBrowse(false); else appendCards(fresh);
  } catch (err) {
    if (seq !== browse.seq || err?.name === 'AbortError') return;
    log('Hub results could not be loaded', 'error', 'hub.request_failed', { error: diagnostics.errorText(err) });
    browse.loading = false;
    browse.error = true;
    if (!browse.items.length) renderBrowse(false); else renderMore();
  }
}

// ---- cards -----------------------------------------------------------------------------------
function toggleFavorite(id, item, remote) {
  const now = !favorites.has(id);
  if (now) favorites.add(id); else favorites.delete(id);
  if (remote) { if (now) starred[id] = snapshot(item); else delete starred[id]; }
  persist();
  renderCounts();
  $$(`[data-fav="${CSS.escape(id)}"]`).forEach(b => setStar(b, now, true));
  if (hubView === 'favorites' && !now) renderLocal();
  return now;
}
function setStar(button, on, animate) {
  button.classList.toggle('active', on);
  button.classList.toggle('pop', on && animate);
  button.title = on ? 'Remove from favorites' : 'Add to favorites';
  button.setAttribute('aria-pressed', String(on));
}
function cardShell(index, animate, label) {
  const card = el('article', 'script-card' + (animate ? ' enter' : ''));
  card.setAttribute('role', 'listitem');
  card.tabIndex = 0;
  card.setAttribute('aria-label', label);
  card.style.setProperty('--i', String(Math.min(index, 12)));
  return card;
}
function starButton(id, item, remote) {
  const star = el('button', 'card-star');
  star.dataset.fav = id;
  star.append(icon('star'));
  setStar(star, favorites.has(id), false);
  star.addEventListener('click', e => { e.stopPropagation(); toggleFavorite(id, item, remote); });
  return star;
}
function remoteCard(s, index, animate, q) {
  const card = cardShell(index, animate, s.title || 'Script');
  const media = el('div', 'card-media');
  const src = imageUrl(s.image || s.game?.imageUrl);
  if (src) media.append(image(src, () => media.prepend(placeholder())));
  else media.append(placeholder());
  if (s.isPatched) media.append(el('span', 'card-flag patched', 'Patched'));
  else if (s.scriptType === 'paid') media.append(el('span', 'card-flag', 'Paid'));
  media.append(starButton(remoteId(s), s, true));
  const body = el('div', 'card-body');
  const title = el('div', 'card-title'); highlight(title, String(s.title || 'Untitled'), q);
  const game = el('div', 'card-game'); highlight(game, s.isUniversal ? 'Universal' : (s.game?.name || 'Roblox'), q);
  const foot = el('div', 'card-foot');
  foot.append(withIcon('eye', compact(s.views)), withIcon('key-round', s.key ? 'Key' : 'No key'));
  if (s.verified) { const v = withIcon('shield-check', '', 'verified'); v.title = 'Verified creator'; foot.append(v); }
  body.append(title, game, foot);
  card.append(media, body);
  card.addEventListener('click', () => openDrawer({ kind: 'remote', item: s }, card));
  card.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); openDrawer({ kind: 'remote', item: s }, card); } });
  return card;
}
function codePreview(source, lines) {
  return el('pre', '', String(source || '').split('\n').slice(0, lines).join('\n') || '-- empty');
}
function localCard(s, index, animate, q) {
  const card = cardShell(index, animate, s.name);
  const media = el('div', 'card-media');
  media.append(codePreview(s.source, 7), starButton(s.id, s, false));
  const body = el('div', 'card-body');
  const title = el('div', 'card-title'); highlight(title, s.name, q);
  const game = el('div', 'card-game'); highlight(game, s.description || 'Saved script', q);
  const foot = el('div', 'card-foot');
  foot.append(withIcon('clock', savedWhen(s.updated).replace(/^Saved /, '')));
  body.append(title, game, foot);
  card.append(media, body);
  card.addEventListener('click', () => openDrawer({ kind: 'local', item: s }, card));
  card.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); openDrawer({ kind: 'local', item: s }, card); } });
  return card;
}
function skeletons(count) {
  return Array.from({ length: count }, () => {
    const card = el('div', 'script-card skeleton');
    const body = el('div', 'card-body');
    const a = el('span', 'bar'); a.style.width = (55 + Math.random() * 35) + '%';
    const b = el('span', 'bar'); b.style.width = (30 + Math.random() * 20) + '%'; b.style.opacity = '.6';
    body.append(a, b, el('span', 'bar'));
    body.lastChild.style.width = '40%'; body.lastChild.style.opacity = '.4';
    card.append(el('div', 'card-media'), body);
    return card;
  });
}

// ---- views -----------------------------------------------------------------------------------
function showEmpty(title, text, action, onAction) {
  $('#hub-empty').hidden = !title;
  if (!title) return;
  $('#hub-empty-title').textContent = title;
  $('#hub-empty-text').textContent = text || '';
  const b = $('#hub-empty-action');
  b.hidden = !action;
  b.textContent = action || '';
  b.onclick = onAction || null;
}
function activeFacets() {
  const list = [];
  for (const facet in FACET_DEFAULTS) if (hubPrefs[facet] !== FACET_DEFAULTS[facet]) list.push({ facet, label: PILL_LABELS[hubPrefs[facet]] });
  if (hubPrefs.verified) list.push({ facet: 'verified', label: 'Verified' });
  return list;
}
function resetFacet(facet) {
  if (facet === 'verified') hubPrefs.verified = false; else hubPrefs[facet] = FACET_DEFAULTS[facet];
}
function filtersChanged(now = false) {
  persist();
  syncFilterControls();
  clearTimeout(filterTimer);
  if (now) loadBrowse(true); else filterTimer = setTimeout(() => loadBrowse(true), 220);
}
function totalText() {
  if (browse.total < 0) return 'Searching…';
  if (browse.error && !browse.items.length) return '';
  const n = browse.total;
  return n === 0 ? 'No scripts' : (n >= 20 && !browse.done ? 'About ' : '') + compact(n) + (n === 1 ? ' script' : ' scripts');
}
function syncFilterControls() {
  $$('[data-facet]').forEach(group => {
    const value = hubPrefs[group.dataset.facet];
    $$('button', group).forEach(b => b.setAttribute('aria-selected', String(b.dataset.value === value)));
  });
  $('#facet-verified').checked = !!hubPrefs.verified;
  const active = activeFacets();
  $('#filter-badge').hidden = !active.length;
  $('#filter-badge').textContent = active.length;
  const pills = active.map(({ facet, label }) => {
    const pill = el('span', 'pill', label);
    const x = el('button'); x.append(icon('x')); x.setAttribute('aria-label', 'Remove filter ' + label);
    x.addEventListener('click', () => { resetFacet(facet); filtersChanged(true); });
    pill.append(x);
    return pill;
  });
  if (active.length > 1) {
    const reset = el('button', 'pill-reset', 'Clear all');
    reset.addEventListener('click', () => { for (const f in FACET_DEFAULTS) hubPrefs[f] = FACET_DEFAULTS[f]; hubPrefs.verified = false; filtersChanged(true); });
    pills.push(reset);
  }
  $('#active-filters').replaceChildren(...pills);
  renderTotals();
}
function renderTotals() {
  $('#hub-total').textContent = totalText();
  $('#filter-total').textContent = totalText();
}
function renderBrowse(loading) {
  const grid = $('#hub-grid');
  showEmpty(null);
  renderTotals();
  if (loading) { grid.replaceChildren(...skeletons(8)); $('#hub-more').textContent = ''; return; }
  if (browse.error && !browse.items.length) {
    grid.replaceChildren();
    showEmpty('Couldn’t reach ScriptBlox', 'Check your connection, then try again.', 'Try again', () => { browse.error = false; loadBrowse(true); });
  } else if (!browse.items.length) {
    grid.replaceChildren();
    const q = query(), filtered = activeFacets().length > 0;
    showEmpty(q ? `No scripts match “${q}”` : 'No scripts found', filtered ? 'Some filters are on. Clearing them may show more.' : 'Try a game name or a different word.',
      filtered ? 'Clear filters' : null, () => { for (const f in FACET_DEFAULTS) hubPrefs[f] = FACET_DEFAULTS[f]; hubPrefs.verified = false; filtersChanged(true); });
  } else {
    const q = query().toLowerCase();
    grid.replaceChildren(...browse.items.map((s, i) => remoteCard(s, i, true, q)));
  }
  renderMore();
}
function appendCards(items) {
  const q = query().toLowerCase();
  $('#hub-grid').append(...items.map((s, i) => remoteCard(s, i, true, q)));
  renderMore();
}
function renderMore() {
  const more = $('#hub-more');
  more.replaceChildren();
  renderTotals();
  if (hubView !== 'browse' || !browse.items.length) return;
  if (browse.loading) more.textContent = 'Loading more…';
  else if (browse.error) {
    const retry = el('button', 'text-button', 'Couldn’t load more. Try again');
    retry.addEventListener('click', () => { browse.error = false; loadBrowse(false); });
    more.append(retry);
  } else if (browse.done) more.textContent = 'End of results';
}
function renderLocal(animate = false) {
  const q = query().toLowerCase();
  const matches = s => !q || [s.name, s.title, s.description, s.game?.name].some(v => typeof v === 'string' && v.toLowerCase().includes(q));
  $('#hub-more').replaceChildren();
  let cards = [];
  if (hubView === 'saved') cards = custom.filter(matches).map((s, i) => localCard(s, i, animate, q));
  else {
    const mine = custom.filter(s => favorites.has(s.id) && matches(s));
    const remote = Object.entries(starred).filter(([id]) => favorites.has(id)).map(([, s]) => s).filter(matches);
    cards = [...mine.map((s, i) => localCard(s, i, animate, q)), ...remote.map((s, i) => remoteCard(s, mine.length + i, animate, q))];
  }
  $('#hub-grid').replaceChildren(...cards);
  if (cards.length) { showEmpty(null); return; }
  if (q) showEmpty(`Nothing matches “${query()}”`, '', 'Clear search', () => { $('#hub-search').value = ''; renderLocal(true); $('#hub-search').focus(); });
  else if (hubView === 'saved') showEmpty('No saved scripts yet', 'Use Save to hub in the editor to keep a script here.', 'Go to editor', () => showPage('editor'));
  else showEmpty('No favorites yet', 'Star a script to keep it here.', 'Browse scripts', () => setHubView('browse'));
}
function renderCounts() {
  $('[data-count="saved"]').textContent = custom.length || '';
  const favs = custom.filter(s => favorites.has(s.id)).length + Object.keys(starred).filter(id => favorites.has(id)).length;
  $('[data-count="favorites"]').textContent = favs || '';
}
function setHubView(view) {
  hubView = view;
  document.body.dataset.hub = view;
  setFilterPanel(false);
  $$('[data-view]').forEach(n => n.setAttribute('aria-selected', String(n.dataset.view === view)));
  $('#hub-search').placeholder = view === 'browse' ? 'Search scripts or games' : view === 'saved' ? 'Search saved scripts' : 'Search favorites';
  $('#page-hub').scrollTop = 0;
  if (view === 'browse') {
    if (browse.key !== apiUrl(1) || (!browse.items.length && !browse.loading)) loadBrowse(true);
    else { showEmpty(null); renderBrowse(false); }
  } else renderLocal(true);
}
function hubShown() {
  if (hubView === 'browse' && !browse.items.length && !browse.loading) loadBrowse(true);
}

// ---- details drawer ---------------------------------------------------------------------------
function tag(iconName, text, cls = '', onClick = null) {
  const t = el(onClick ? 'button' : 'span', 'tag ' + cls);
  if (iconName) t.append(icon(iconName));
  t.append(document.createTextNode(text));
  if (onClick) { t.title = 'Search for ' + text; t.addEventListener('click', onClick); }
  return t;
}
function searchFor(text) {
  closeDrawer();
  if (hubView !== 'browse') setHubView('browse');
  $('#hub-search').value = text;
  loadBrowse(true);
}
function fillRemoteDrawer(s, detail) {
  const d = detail || {};
  $('#drawer-source').textContent = 'ScriptBlox';
  const media = $('#drawer-media');
  const src = imageUrl(s.image || s.game?.imageUrl);
  if (!detail) media.replaceChildren(...(src ? [image(src, () => media.replaceChildren(placeholder()))] : [placeholder()]));
  $('#drawer-title').textContent = s.title || 'Untitled';
  const owner = $('#drawer-owner');
  owner.replaceChildren();
  if (d.owner?.username) {
    const pic = imageUrl(d.owner.profilePicture);
    if (pic) owner.append(image(pic));
    owner.append(document.createTextNode(d.owner.username));
    if (d.owner.verified) owner.append(icon('badge-check'));
  }
  const stats = [withIcon('eye', compact(d.views ?? s.views) + ' views')];
  if (detail) stats.push(withIcon('thumbs-up', compact(d.likeCount || 0)), withIcon('thumbs-down', compact(d.dislikeCount || 0)));
  const when = dateText(d.updatedAt || d.lastBump || s.lastBump || d.createdAt || s.createdAt);
  if (when) stats.push(withIcon('calendar', when));
  $('#drawer-stats').replaceChildren(...stats);
  const tags = [];
  if (s.isUniversal) tags.push(tag('globe', 'Universal'));
  else if (s.game?.name) tags.push(tag('gamepad-2', s.game.name, '', () => searchFor(s.game.name)));
  tags.push(tag('key-round', s.key ? 'Key system' : 'No key'));
  if (s.verified) tags.push(tag('shield-check', 'Verified'));
  if (s.isPatched) tags.push(tag(null, 'Patched', 'patched'));
  if (s.scriptType === 'paid') tags.push(tag(null, 'Paid'));
  for (const t of (Array.isArray(d.tags) ? d.tags : []).slice(0, 8)) if (typeof t === 'string' && t.trim()) tags.push(tag(null, t.trim(), '', () => searchFor(t.trim())));
  $('#drawer-tags').replaceChildren(...tags);
  $('#drawer-text').textContent = typeof d.features === 'string' ? plainText(d.features).slice(0, 1200) : '';
  const code = d.script || s.script || '';
  $('#drawer-code').textContent = code ? code.split('\n').slice(0, 40).join('\n') : (detail ? 'This script has no code to show.' : 'Loading…');
  $('#drawer-delete').hidden = true;
  setStar($('#drawer-star'), favorites.has(remoteId(s)), false);
  $('#drawer-star').dataset.fav = remoteId(s);
}
function fillLocalDrawer(s) {
  $('#drawer-source').textContent = 'Saved on this device';
  $('#drawer-media').replaceChildren(codePreview(s.source, 12));
  $('#drawer-title').textContent = s.name;
  $('#drawer-owner').replaceChildren();
  $('#drawer-stats').replaceChildren(withIcon('clock', savedWhen(s.updated)), withIcon('code-xml', (s.source.split('\n').length) + ' lines'));
  $('#drawer-tags').replaceChildren();
  $('#drawer-text').textContent = s.description || '';
  $('#drawer-code').textContent = s.source.split('\n').slice(0, 40).join('\n') || '-- empty';
  $('#drawer-delete').hidden = false;
  setStar($('#drawer-star'), favorites.has(s.id), false);
  $('#drawer-star').dataset.fav = s.id;
}
async function openDrawer(entry, from) {
  drawerItem = entry;
  drawerReturn = from || null;
  $('#script-drawer .drawer-scroll').scrollTop = 0;
  if (entry.kind === 'local') fillLocalDrawer(entry.item);
  else {
    const s = entry.item;
    fillRemoteDrawer(s, details.get(s.slug));
    if (!details.has(s.slug) && s.slug) {
      try {
        const res = await fetch(`${SB}/api/script/${encodeURIComponent(s.slug)}`);
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        if (data?.script) details.set(s.slug, data.script);
        else throw new Error('Hub details response did not contain a script');
      } catch (error) { log('Script details could not be loaded', 'warning', 'hub.details_failed', { error: diagnostics.errorText(error) }); }
      if (drawerItem === entry) fillRemoteDrawer(s, details.get(s.slug) || { views: s.views });
    }
  }
  $('#script-drawer').classList.add('open');
  $('#script-drawer').setAttribute('aria-hidden', 'false');
  $('#drawer-scrim').classList.add('open');
  $('#drawer-open').focus({ preventScroll: true });
}
function closeDrawer() {
  if (!$('#script-drawer').classList.contains('open')) return;
  $('#script-drawer').classList.remove('open');
  $('#script-drawer').setAttribute('aria-hidden', 'true');
  $('#drawer-scrim').classList.remove('open');
  drawerReturn?.focus?.({ preventScroll: true });
  drawerItem = null;
}
function drawerCode() {
  if (!drawerItem) return '';
  if (drawerItem.kind === 'local') return drawerItem.item.source;
  const s = drawerItem.item, d = details.get(s.slug);
  return (d && d.script) || s.script || '';
}
$('#drawer-close').addEventListener('click', closeDrawer);
$('#drawer-scrim').addEventListener('click', closeDrawer);
$('#drawer-open').addEventListener('click', () => {
  if (!drawerItem) return;
  const { kind, item } = drawerItem;
  if (kind === 'local') {
    const existing = tabs.find(t => t.hubId === item.id);
    closeDrawer();
    if (existing) { selectTab(existing.id); showPage('editor'); }
    else { newTab(item.source, item.name + '.lua', item.id); log('Opened ' + item.name); }
    return;
  }
  const code = drawerCode();
  if (!code) { toast('That script has no code to open.', 'info'); return; }
  closeDrawer();
  newTab(code, fileName(item.title));
  log(`Opened ${item.title} from ScriptBlox.`);
});
$('#drawer-copy').addEventListener('click', async () => {
  const code = drawerCode();
  if (!code) { toast('Nothing to copy yet.', 'info'); return; }
  try { await navigator.clipboard.writeText(code); toast('Script copied'); } catch { toast('Select the code and press Ctrl+C to copy.', 'info'); }
});
$('#drawer-star').addEventListener('click', () => {
  if (!drawerItem) return;
  const { kind, item } = drawerItem;
  toggleFavorite(kind === 'local' ? item.id : remoteId(item), item, kind === 'remote');
});
$('#drawer-delete').addEventListener('click', () => {
  if (!drawerItem || drawerItem.kind !== 'local') return;
  const s = drawerItem.item;
  confirmAction(`Remove ${s.name}?`, 'It is removed from your saved scripts. Open tabs keep their text.', 'Remove', () => {
    custom = custom.filter(x => x.id !== s.id);
    favorites.delete(s.id);
    tabs.forEach(t => { if (t.hubId === s.id) t.hubId = null; });
    persist();
    closeDrawer();
    renderCounts();
    renderLocal();
    log('Removed ' + s.name + ' from the hub.');
    toast('Removed from the hub');
  });
});

// ---- filter panel ------------------------------------------------------------------------------
function setFilterPanel(open) {
  $('#filter-panel').hidden = !open;
  $('#filter-toggle').setAttribute('aria-expanded', String(open));
}
$('#filter-toggle').addEventListener('click', () => setFilterPanel($('#filter-panel').hidden));
$('#filter-done').addEventListener('click', () => { setFilterPanel(false); $('#filter-toggle').focus(); });
$('#filter-reset').addEventListener('click', () => { for (const f in FACET_DEFAULTS) hubPrefs[f] = FACET_DEFAULTS[f]; hubPrefs.verified = false; filtersChanged(); });
document.addEventListener('mousedown', e => {
  if (!$('#filter-panel').hidden && !e.target.closest('#filter-panel, #filter-toggle')) setFilterPanel(false);
});
$$('[data-facet]').forEach(group => $$('button', group).forEach(b => b.addEventListener('click', () => {
  if (hubPrefs[group.dataset.facet] === b.dataset.value) return;
  hubPrefs[group.dataset.facet] = b.dataset.value;
  filtersChanged();
})));
$('#facet-verified').addEventListener('change', e => { hubPrefs.verified = e.target.checked; filtersChanged(); });

$$('[data-view]').forEach(b => b.addEventListener('click', () => { if (hubView !== b.dataset.view) setHubView(b.dataset.view); }));
combo($('#hub-sort'), { label: 'Sort', value: hubPrefs.sort, options: SORTS, onChange: v => { hubPrefs.sort = v; filtersChanged(true); } });
$('#hub-search').addEventListener('input', () => {
  clearTimeout(searchTimer);
  if (hubView === 'browse') searchTimer = setTimeout(() => loadBrowse(true), 350);
  else renderLocal();
});
$('#hub-search').addEventListener('keydown', e => {
  if (e.key === 'Escape' && e.target.value) { e.stopPropagation(); e.target.value = ''; e.target.dispatchEvent(new Event('input')); }
  if (e.key === 'Enter' && hubView === 'browse') { clearTimeout(searchTimer); loadBrowse(true); }
});
new IntersectionObserver(entries => {
  if (entries.some(e => e.isIntersecting) && currentPage === 'hub' && hubView === 'browse' && browse.items.length) loadBrowse(false);
}, { root: $('#page-hub'), rootMargin: '0px 0px 500px 0px' }).observe($('#hub-more'));
syncFilterControls();

// ---- preferences -----------------------------------------------------------------------------------
function applyPreferences() {
  document.body.classList.toggle('reduce-motion', !prefs.smooth);
  if (ed) ed.updateOptions(monacoOptions());
  fallback.style.fontSize = prefs.fontSize + 'px';
  persist();
}
combo($('#font-size'), {
  label: 'Font size', value: Number(prefs.fontSize),
  options: [12, 13, 14, 15, 16, 18].map(n => ({ value: n, label: n + ' px' })),
  onChange: v => { prefs.fontSize = v; applyPreferences(); },
});
function readPreferences() {
  prefs = { ...prefs, wordWrap: $('#word-wrap').checked, minimap: $('#minimap').checked, smooth: $('#smooth-motion').checked };
  applyPreferences();
}
$('#word-wrap').checked = !!prefs.wordWrap;
$('#minimap').checked = !!prefs.minimap;
$('#smooth-motion').checked = !!prefs.smooth;
['word-wrap', 'minimap', 'smooth-motion'].forEach(id => $('#' + id).addEventListener('change', readPreferences));
$('#open-preferences').addEventListener('click', () => openDialog($('#settings-dialog')));

// ---- window: no native title bar, so the page carries minimize and close -------------------------
function post(message) {
  try { window.chrome?.webview?.postMessage(message); }
  catch (error) { diagnostics.record('error', 'window.message_failed', 'Window command failed', { command: message, error: diagnostics.errorText(error) }); }
}
$('#window-minimize').addEventListener('click', () => post('window:minimize'));
$('#window-close').addEventListener('click', () => { window.natwareFlush(); post('window:close'); });

// ---- keyboard --------------------------------------------------------------------------------------
addEventListener('keydown', e => {
  if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
  if (document.querySelector('dialog[open]')) return;
  const k = e.key.toLowerCase();
  const act = fn => { e.preventDefault(); e.stopPropagation(); fn(); };
  if (k === 's') act(() => e.shiftKey ? openSaveDialog() : saveFile());
  else if (k === 'o') act(() => $('#file-input').click());
  else if (k === 'k') act(() => { showPage('hub'); setTimeout(() => $('#hub-search').focus(), 60); });
  else if (k === 't') act(() => newTab());
  else if (k === 'w' && currentPage === 'editor') act(() => closeTab(activeId));
}, true);
addEventListener('keydown', e => {
  if (e.key !== 'Escape' || document.querySelector('dialog[open]')) return;
  if (!$('#filter-panel').hidden) { setFilterPanel(false); $('#filter-toggle').focus(); }
  else if ($('#script-drawer').classList.contains('open')) closeDrawer();
});
addEventListener('resize', () => { moveNavIndicator(); moveTabIndicator(true); });

// ---- start ------------------------------------------------------------------------------------------
Glass.scan();
applyPreferences();
setConsoleCollapsed(consoleCollapsed);
renderTabs(true);
renderCounts();
document.body.dataset.hub = hubView;
logViews.forEach(renderLogView);
showPage('editor');
document.fonts?.ready.then(() => { moveNavIndicator(); moveTabIndicator(true); });
if (storageLoadError) log('Stored workspace could not be loaded; check diagnostics and the recovery copy', 'warning', 'storage.load_failed', { error: storageLoadError });
log('Workspace ready.', 'info', 'workspace.ready', { tabs: tabs.length, savedScripts: custom.length, hostPresent: !!nexecHost });
